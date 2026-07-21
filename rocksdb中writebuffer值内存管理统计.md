# WriteBufferManager 与内存追踪深度解析（ToplingDB / RocksDB）

> 本文基于本仓库源码逐行梳理，目标是讲清楚四件事：
> 1. `WriteBufferManager` 是什么、它如何**追踪** memtable 内存；
> 2. 它**如何判定**「内存不足」（阈值与触发条件）；
> 3. 内存不足时，**写路径**采取了哪些措施（Flush、Stall、解除 Stall）；
> 4. **读/查询路径**在内存不足时的行为（block cache 淘汰、`strict_capacity_limit`、`CacheReservationManager`）。

---

## 一、WriteBufferManager 是什么

`WriteBufferManager`（WBM）是一个**跨 ColumnFamily、跨 DB 共享**的 memtable 内存预算管理器。一个 DB 可以有多个 CF，多个 CF 的多个 mutable/immutable memtable 的内存用量，可以汇总到一个 WBM 实例里统一约束。

定义在 `include/rocksdb/write_buffer_manager.h`，核心字段（`write_buffer_manager.h:165-181`）：

```cpp
std::atomic<size_t> buffer_size_;     // 用户配置的总预算（0 = 不限制）
std::atomic<size_t> mutable_limit_;   // = buffer_size * 7/8，软阈值
std::atomic<size_t> memory_used_;     // 所有 memtable 已用内存（含正在 flush 的）
std::atomic<size_t> memory_active_;   // "活跃" memtable（尚未调度 flush）的内存
std::shared_ptr<Cache> cache_;        // 可选：把 memtable 内存"记账"到 block cache
std::shared_ptr<CacheReservationManager> cache_res_mgr_;
std::list<StallInterface*> queue_;    // 被挂起阻塞的 DB 列表
std::atomic<bool> allow_stall_;       // 是否允许在内存超限时 stall 写入
std::atomic<bool> stall_active_;      // 当前是否真的处于 stall 状态
```

构造函数（`memtable/write_buffer_manager.cc:21-40`）：

```cpp
WriteBufferManager(size_t _buffer_size, std::shared_ptr<Cache> cache, bool allow_stall)
    : buffer_size_(_buffer_size),
      mutable_limit_(buffer_size_ * 7 / 8),   // 关键：软阈值是预算的 7/8
      ...
      allow_stall_(allow_stall),
      stall_active_(false) {
  if (cache) {
    // Memtable 内存波动频繁，所以 delayed_decrease=true，避免反复插/删 dummy entry
    cache_res_mgr_ = std::make_shared<
        CacheReservationManagerImpl<CacheEntryRole::kWriteBuffer>>(
        cache, true /* delayed_decrease */);
  }
}
```

三个关键开关：

| 配置 | 含义 |
|---|---|
| `buffer_size_ == 0` | `enabled()` 为 false，**不限制内存**，`ShouldFlush()` 恒为 true，`memory_usage()` 无意义 |
| `cache != nullptr` | `cost_to_cache()` 为 true，把 memtable 内存"计费"到 block cache（通过插入 256KB dummy entry） |
| `allow_stall == true` | 内存超 `buffer_size_` 时允许**阻塞**写入线程（而不只是触发 flush） |

---

## 二、内存追踪：memtable 内存是怎么数出来的

### 2.1 AllocTracker —— 每个 memtable 一个记账员

每个 `MemTable` 内部持有一个 `AllocTracker mem_tracker_`（`db/memtable.h:640`），构造时绑定到该 DB 的 `WriteBufferManager*`（`db/memtable.cc:80`）。这个 tracker 又被传给底层的 `MemTableRep`（skiplist/hash 等，`db/memtable.cc:85`），**在 skiplist 每次 arena 分配时**调用 `AllocTracker::Allocate(bytes)` 计费。

`AllocTracker` 的生命周期就是 memtable 的「分配 → 冻结（immutable）→ flush 完成 → 销毁」三阶段（`memtable/alloc_tracker.cc`）：

```cpp
// 1) skiplist 每分配一块内存就计一笔
void AllocTracker::Allocate(size_t bytes) {
  if (wbm->enabled() || wbm->cost_to_cache()) {
    bytes_allocated_.fetch_add(bytes);
    wbm->ReserveMem(bytes);            // → memory_used_ += bytes, memory_active_ += bytes
  }
}

// 2) memtable 被 SwitchMemtable 冻结为 immutable 时调用
void AllocTracker::DoneAllocating() {
  wbm->ScheduleFreeMem(bytes_allocated_);  // → memory_active_ -= bytes（但 memory_used_ 不变）
  done_allocating_ = true;
}

// 3) flush 完成、immutable memtable 析构时调用
void AllocTracker::FreeMem() {
  wbm->FreeMem(bytes_allocated_);          // → memory_used_ -= bytes，并 MaybeEndWriteStall()
  freed_ = true;
}
```

> `db/memtable.h:529` 在 memtable 切换为 immutable 时调用 `mem_tracker_.DoneAllocating()`；`db/memtable.cc:169` 在析构时调用 `mem_tracker_.FreeMem()`。

### 2.2 两个计数器的语义差别（理解 WBM 的关键）

| 计数器 | 含义 | 何时增加 | 何时减少 |
|---|---|---|---|
| `memory_used_` | **所有** memtable（mutable + immutable 未释放）的总内存 | `ReserveMem` | `FreeMem`（flush 完成析构时） |
| `memory_active_` | 仅 **mutable**（还在写入、尚未调度 flush）的内存 | `ReserveMem` | `ScheduleFreeMem`（切 immutable 时立即减） |

也就是说：当一个 mutable memtable 被冻结为 immutable，`memory_active_` 立刻下降（这部分内存"已经在 flush 了"），但 `memory_used_` 要等 flush 完成、immutable 真正析构才下降。

这个差别直接决定了**两个不同力度的阈值**（见第三节）。

### 2.3 ReserveMem / FreeMem 的两条路径

WBM 内部根据是否配置了 `cache` 走两条路（`memtable/write_buffer_manager.cc:57-117`）：

```cpp
void WriteBufferManager::ReserveMem(size_t mem) {
  if (cache_res_mgr_ != nullptr) {
    ReserveMemWithCache(mem);          // 走 cache 计费
  } else if (enabled()) {
    memory_used_.fetch_add(mem);       // 仅原子加
  }
  if (enabled()) {
    memory_active_.fetch_add(mem);
  }
}
```

**纯计数模式**（无 cache）：只是一个 `std::atomic<size_t>` 累加，无锁、开销极小。

**Cache 计费模式**（`ReserveMemWithCache`，`write_buffer_manager.cc:69-85`）：在 `cache_res_mgr_mu_` 保护下更新 `memory_used_`，并调用 `CacheReservationManager::UpdateCacheReservation(new_mem_used)`，向 block cache 插入若干 **256KB 的 dummy entry**，从而"侵占"等量的 block cache 容量——等于让 memtable 内存去挤压 block cache 的可用空间。注意这里的 `Status` 被 `s.PermitUncheckedError()` 显式吞掉了（注释说 WBM 没法妥善处理 cache 计费失败，TODO）。

> dummy entry 的细节见第六节。`delayed_decrease=true` 表示即使内存下降，dummy entry 也要等用量跌到预留量的 3/4 以下才释放，避免反复插入（`cache/cache_reservation_manager.cc:69-83`）。

---

## 三、如何"知道"内存不足 —— 判定阈值

WBM 提供**三个判定函数**，对应三种力度。它们的共同基础都是把 `memory_used_` / `memory_active_` 跟 `buffer_size_` 比较——**RocksDB 完全不查询操作系统内存，"内存不足"就是这几个原子计数器越界**。

### 3.1 `ShouldFlush()` —— 该触发 flush 了吗（`write_buffer_manager.h:101-117`）

```cpp
bool ShouldFlush() const {
  if (enabled()) {
    // 条件 A：活跃 memtable 内存 > buffer_size 的 7/8
    if (mutable_memtable_memory_usage() > mutable_limit_)   // mutable_limit_ = buffer_size * 7/8
      return true;
    // 条件 B：总内存已超预算，但当前正在 flush 的部分还没超过一半
    size_t local_size = buffer_size();
    if (memory_usage() >= local_size &&
        mutable_memtable_memory_usage() >= local_size / 2)
      return true;
  }
  return false;
}
```

解读：
- **条件 A（软阈值，7/8）**：用 `memory_active_`（活跃内存）衡量。活跃内存接近预算时就提前 flush，避免等到爆。
- **条件 B（硬阈值）**：用 `memory_used_`（总量）衡量。总量已经超 `buffer_size_`，但前提是「正在 flush 的量（= used − active）还没超过一半」——否则说明 flush 已经开足马力，再触发更多 flush 反而会堆积过多 immutable memtable，得不偿失，所以**故意返回 false 按住**。

### 3.2 `ShouldStall()` —— 该阻塞写入了吗（`write_buffer_manager.h:126-142`）

```cpp
bool ShouldStall() const {
  if (!allow_stall_ || !enabled()) return false;
  return IsStallActive() || IsStallThresholdExceeded();
}

bool IsStallThresholdExceeded() const {
  return memory_usage() >= buffer_size_;   // 唯一判定：总量 >= 预算
}
```

stall 的门槛就是**总内存 >= `buffer_size_`**，且必须用户显式开启 `allow_stall=true`。一旦进入 stall，`IsStallActive()` 会持续返回 true，直到 `FreeMem` 把总量压回 `buffer_size_` 以下（见第四节）。

### 3.3 `MemTable::ShouldFlushNow()` —— 单个 memtable 内部判定（`db/memtable.cc:192-258`）

这是 memtable **自身**的判定（基于 `write_buffer_size_`，与 WBM 无关），衡量的是 skiplist + arena 实际分配字节。为了减少 arena block 粒度造成的浪费，引入 `kAllowOverAllocationRatio = 0.6`：

- 如果再分配一个 arena block 也不会超过 `write_buffer_size + 0.6 * kArenaBlockSize` → **不 flush**；
- 如果已经超过 `write_buffer_size + 0.6 * kArenaBlockSize` → **flush**；
- 中间灰色地带 → 看 arena 最后一块的填充率是否 < 1/4。

`UpdateFlushState()`（`db/memtable.cc:260-269`）调用它来把 `flush_state_` 从 `FLUSH_NOT_REQUESTED` CAS 成 `FLUSH_REQUESTED`，由 flush 线程消费。

> **三者的层级关系**：memtable 自己的 `ShouldFlushNow`（单表，arena 粒度）→ CF 层的 flush scheduler → DB/WBM 层的 `ShouldFlush`（全局预算粒度）→ WBM 层的 `ShouldStall`（阻塞粒度）。从「单表软限制」逐级升级到「全局硬阻塞」。

---

## 四、内存不足时做了哪些措施（写路径）

写路径在每个 batch 落盘后，于 `DBImpl::PreprocessWrite`（实现在 `db/db_impl/db_impl_write.cc:1190` 一带）依次检查 WBM。这是一段**有严格顺序的降级链**：

### 措施一：触发 Flush（`ShouldFlush` → `HandleWriteBufferManagerFlush`）

`db/db_impl/db_impl_write.cc:1214-1226`：

```cpp
if (UNLIKELY(status.ok() && write_buffer_manager_->ShouldFlush())) {
  InstrumentedMutexLock l(&mutex_);
  WaitForPendingWrites();
  status = HandleWriteBufferManagerFlush(write_context);
}
```

`HandleWriteBufferManagerFlush`（`db_impl_write.cc:1842-1932`）做的事：
1. **挑一个 CF**：遍历所有 CF，跳过已 drop 的；只考虑「mutable memtable 非空 **且** 没有正在 flush 的 immutable」的 CF（避免给已经在 flush 的 CF 再加压）。从中选 `GetCreationSeq` 最小（最老）的那个（`db_impl_write.cc:1861-1879`）。
2. **SwitchMemtable**：把该 CF 的 mutable memtable 冻结为 immutable，换上一个新的空 memtable（`db_impl_write.cc:1901`）。这一步会触发 `mem_tracker_.DoneAllocating()` → `ScheduleFreeMem` → `memory_active_` 立刻下降。
3. **调度后台 flush**：`GenerateFlushRequest({cfd}, FlushReason::kWriteBufferManager, ...)` → `SchedulePendingFlush` → `MaybeScheduleFlushOrCompaction`（`db_impl_write.cc:1919-1929`）。注意 flush reason 是专门的 `kWriteBufferManager`，可在 stats 里区分。

> 注释（`db_impl_write.cc:1847-1851`）说明了一个已知次优问题：在 SwitchMemtable 真正生效前，`ShouldFlush()` 会持续返回 true，多个共享同一 WBM 的 DB 可能被同时 flush，flush 得比必要的多——"次优但正确"。

### 措施二：Write Stall（`ShouldStall` → 阻塞写线程）

`db/db_impl/db_impl_write.cc:1268-1278`：

```cpp
if (UNLIKELY(status.ok() && write_buffer_manager_->ShouldStall())) {
  stats->AddDBStats(kIntStatsWriteBufferManagerLimitStopsCounts, 1, true);
  if (write_options.no_slowdown) {
    status = Status::Incomplete("Write stall");   // 快路径写：直接返回，不阻塞
  } else {
    InstrumentedMutexLock l(&mutex_);
    WriteBufferManagerStallWrites();               // 慢路径写：真的睡下去
  }
}
```

**关键点 1：`no_slowdown` 优先**。如果调用方设置了 `WriteOptions::no_slowdown=true`（要求绝不阻塞），WBM 不会让线程睡觉，而是立刻返回 `Status::Incomplete("Write stall")`，把"内存满了"这个事实抛给上层处理。

**关键点 2：阻塞是通过条件变量实现的**。`WriteBufferManagerStallWrites`（`db_impl_write.cc:2049-2068`）：

```cpp
void DBImpl::WriteBufferManagerStallWrites() {
  write_thread_.BeginWriteStall();       // 阻止新的 writer 进入写队列
  mutex_.Unlock();
  // 把自己的 wbm_stall_（一个 StallInterface）状态置为 BLOCKED
  static_cast<WBMStallInterface*>(wbm_stall_.get())->SetState(BLOCKED);
  // 注册到 WBM 的阻塞队列，然后自己 Block() 睡下
  write_buffer_manager_->BeginWriteStall(wbm_stall_.get());
  wbm_stall_->Block();                   // ← 当前线程在此等待
  mutex_.Lock();
  write_thread_.EndWriteStall();         // 醒来后恢复写队列
}
```

`WBMStallInterface`（`db/db_impl/db_impl.h:1246-1292`）实现了 `StallInterface`，核心是一个 `state_mutex_` + `state_cv_`：

```cpp
void Block() override {
  MutexLock lock(&state_mutex_);
  while (state_ == BLOCKED) {
    state_cv_.Wait();            // 睡，直到 Signal() 把 state_ 改回 RUNNING
  }
}
void Signal() override {
  MutexLock lock(&state_mutex_);
  state_ = RUNNING;              // 由 WBM 在解除 stall 时跨 DB 调用
  state_cv_.Signal();
}
```

而 `WriteBufferManager::BeginWriteStall`（`write_buffer_manager.cc:119-139`）在 `mu_` 保护下：若仍满足 `ShouldStall()`，就把这个 DB 的 `StallInterface*` 推入 `queue_` 并置 `stall_active_=true`；若期间 stall 已被解除，则立刻 `Signal()` 放行。

### 措施三：解除 Stall（`FreeMem` → `MaybeEndWriteStall`）

当 immutable memtable flush 完成被析构时，`AllocTracker::FreeMem` → `WriteBufferManager::FreeMem`（`write_buffer_manager.cc:93-101`）：

```cpp
void WriteBufferManager::FreeMem(size_t mem) {
  if (cache_res_mgr_ != nullptr) FreeMemWithCache(mem);
  else if (enabled()) memory_used_.fetch_sub(mem);
  MaybeEndWriteStall();   // ← 每次释放都检查能否解除 stall
}
```

`MaybeEndWriteStall`（`write_buffer_manager.cc:142-165`）：

```cpp
void WriteBufferManager::MaybeEndWriteStall() {
  // 若仍超阈值，啥也不做
  if (allow_stall_ && IsStallThresholdExceeded()) return;
  std::unique_lock<std::mutex> lock(mu_);
  if (!stall_active_.load()) return;
  stall_active_.store(false);
  // 把队列里所有被阻塞的 DB 全部唤醒
  for (StallInterface* wbm_stall : queue_) wbm_stall->Signal();
  cleanup = std::move(queue_);
}
```

即：**只要 `memory_used_` 跌回 `buffer_size_` 以下，所有跨 DB 被 stall 的写线程都被唤醒**。这形成完整的负反馈环：内存涨 → stall；flush 把内存压下去 → 自动唤醒。

### 措施的完整降级顺序（`PreprocessWrite` 内）

```
total_log_size > max_total_wal_size  →  SwitchWAL
ShouldFlush()                         →  HandleWriteBufferManagerFlush   (触发 flush，不阻塞)
!flush_scheduler_.Empty()             →  ScheduleFlushes                 (CF 自身判定要 flush)
write_controller_ stopped/delay       →  DelayWrite                      (compaction 落后引起的 delay)
ShouldStall()                         →  WriteBufferManagerStallWrites   (最后才阻塞)
```

可见 WBM 的 stall 是**最后兜底**：只有 flush 已经被触发、却仍不足以把内存压下来（或 flush 跟不上）时，才会真正把写线程按住睡觉。

---

## 五、查询/读路径如何处理内存不足

**核心结论**：在默认配置下，读路径**没有**像写路径那样的「内存不足就 stall/abort」机制。读路径的内存主要由 **block cache（带容量上限和淘汰）** 和 **table cache（`max_open_files` 限定句柄数）** 约束，靠**淘汰**而非**阻塞**来应对内存压力。

### 5.1 读路径完全不碰 WriteBufferManager

对 `table/`、`db/table_cache*`、`db/db_iter*`、`db/db_impl_readonly*` 全树 grep `WriteBufferManager` / `write_buffer_manager` —— **零命中**。WBM 只在写路径（`memtable.*`、`column_family.*`、`version_set.cc`、`db_impl_write.cc`、`write_stall_stats.*`）出现。也就是说：

> memtable 的内存预算（`WriteBufferManager`）和读路径的内存预算（block cache）是**两套独立的池**，唯一的交集是可选的「把 memtable 内存计费到 block cache」（第四节 dummy entry 机制）。

### 5.2 Block cache 满了怎么办：LRU 淘汰 + `strict_capacity_limit`

读一个 block 的流程（`table/block_based/block_based_table_reader.cc`）：先 `block_cache.LookupFull`（`:1328`）；未命中则从文件读出，再 `PutDataBlockToCache` → `block_cache.InsertFull`（`:1392`）。

「内存不足」在 cache 层的唯一判定是这一行（`cache/lru_cache.cc:324`，在 `EvictFromLRU` 内）：

```cpp
while ((usage_ + charge) > capacity_ && lru_.next != &lru_) {
  // 从 LRU 队首（最老）淘汰一个无外部引用的 entry
}
```

即 **`(usage_ + charge) > capacity_`**。RocksDB 不查 OS，"cache 满了"就是这个原子 `usage_`（所有 entry 的 `total_charge` 之和）越界。

`Insert` 在淘汰之后仍超容量时，行为分两种（`cache/lru_cache.cc:383-395`）：

| `strict_capacity_limit_` | `handle` 要求 | 行为 |
|---|---|---|
| **false（默认）** | 任意 | **照常插入，允许 `usage_` 超过 `capacity_`**；返回 OK。读**绝不因此失败** |
| true | 需要 handle | 返回 `Status::MemoryLimit("Insert failed due to LRU cache being full.")`（`:394`） |
| true | 不需要 handle（fire-and-forget） | 返回 OK 但**不插入**，entry 直接释放 |

所以默认配置下，block cache 满只会导致**更频繁的淘汰 → 更多 cache miss → 更多磁盘 IO**，不会让查询报错。只有用户显式把 block cache 设为 `strict_capacity_limit=true`，且 `kBlockBasedTableReader`/`kFileMetadata` 的 cache 预留失败时，才会让 `BlockBasedTable::Open` 失败并把错误传到查询（见 5.3）。

### 5.3 `CacheReservationManager` 在读路径的使用

读路径同样用 `CacheReservationManager` 把若干"非 block 的元数据内存"计费到 block cache，等于和 data block 抢容量。模板实例化见 `cache/cache_reservation_manager.cc:175-183`，读路径相关的有：

| 角色（CacheEntryRole） | 用在哪 | 失败（仅 strict 模式）处理 |
|---|---|---|
| `kBlockBasedTableReader` | `BlockBasedTable::Open` 末尾，按 `ApproximateMemoryUsage()` 预留（`block_based_table_reader.cc:805-817`） | **Open 失败**，TableReader 丢弃，错误传到读请求 |
| `kFileMetadata` | `VersionBuilder` add/unref 文件元数据（`db/version_builder.cc:795-809` / `:303-307`） | **version edit 中止**，`FileMetaData` 删除并返回 `MemoryLimit` |
| `kFilterConstruction` | filter 构造（实际是写/compaction 路径，`table/block_based/filter_policy.cc`） | 多数 `PermitUncheckedError` 吞掉；ribbon 的 banding buffer 在 strict 模式下**降级为 bloom filter**（`filter_policy.cc:660-676`） |

注意：`kWriteBuffer`（memtable）、`kCompressionDictionaryBuildingBuffer`（table builder）属于写路径，不在读路径出现。

### 5.4 其他限制读路径内存的旋钮

- **`max_open_files`** → 决定 `table_cache` 容量（不是字节，而是 **TableReader 句柄数**）。`db/db_impl/db_impl.cc:374-388` 与 `db/version_set.cc:6607` 都用 `max_open_files - 10`。每个 table reader 插入时 `charge = 1`（`db/table_cache.cc:218`），`strict_capacity_limit` 默认 false，所以超了只是 LRU 淘汰旧的 table reader、关掉对应文件，**不会让查询失败**。
- **block cache 容量** → data/index/filter block 的字节上限（默认 32MiB，`block_based_table_factory.cc:445-450`）。
- **`row_cache`** → 可选的已解码 KV 行缓存，独立池。

### 5.5 读路径里仅有的几个 `Incomplete`/`MemoryLimit`

逐个澄清，避免误读：
- `db/table_cache.cc:189` `Status::Incomplete("Table not found in table_cache, no_io is set")` —— 仅当查询带 `no_io=true` 且 table 未缓存，是"请用允许 IO 重试"的信号，**与内存无关**。
- `db/db_iter.cc:1757` `Status::Incomplete("Too many internal keys skipped.")` —— 扫描爆炸的保护阀，**与内存无关**。
- `block_based_table_reader.cc:809-816` 的 `MemoryLimit` —— 仅在 block cache `strict_capacity_limit=true` 且 `kBlockBasedTableReader` 预留失败时出现，**默认配置不会触发**。

> 结论：**默认配置下，查询永远不会因内存不足被 stall 或 abort**；最坏情况是 cache 命中率下降、IO 上升。要让读路径在内存压力下"硬失败"，需要显式开启 block cache 的 `strict_capacity_limit=true`。

---

## 六、补充：CacheReservationManager 与 dummy entry 机制

WBM（可选）和读路径元数据预留都用这套机制把"非 cache 的内存"转嫁为"cache 容量占用"：

- **粒度**：`kSizeDummyEntry = 256 * 1024`（`cache/cache_reservation_manager.h:206`）。用量向上取整到 256KB 的整数倍。
- **插入**：`IncreaseCacheReservation`（`cache_reservation_manager.cc:111-127`）循环 `cache_.Insert(GetNextCacheKey(), kSizeDummyEntry, &handle)`，每个 dummy entry `value=nullptr`、`del_cb=nullptr`（纯占位，见 `cache/typed_cache.h:85-93` 的 `PlaceholderCacheInterface`）。
- **钉住**：handle 存在 `dummy_handles_` 里 → 有外部引用 → **不会出现在 LRU 链表上，不会被 `EvictFromLRU` 淘汰**。所以这部分容量是"实打实被占住"的，会迫使其它真实 block 被淘汰。
- **延迟释放**：`delayed_decrease=true` 时，要等 `new_mem_used < cache_allocated_size_ * 3/4` 才真正删 dummy（`cache_reservation_manager.cc:69-83`）。
- **失败处理**：strict cache 下 `Insert` 返回 `MemoryLimit`，`IncreaseCacheReservation` 会原样向上返回；WBM 在 `ReserveMemWithCache` 里**用 `PermitUncheckedError()` 吞掉**（注释明说没法妥善处理，TODO）。

---

## 七、完整时序总览

### 写路径（内存从涨到回落）

```
skiplist 分配 arena block
  └─ AllocTracker::Allocate(bytes)
       └─ WBM::ReserveMem(bytes)
            ├─ memory_used_  += bytes
            └─ memory_active_ += bytes
            └─ (若配 cache) CacheReservationManager 插 256KB dummy 到 block cache

写完一个 batch，PreprocessWrite 依次判定：
  ① memory_active_ > 7/8·buffer_size  或  (memory_used_ >= buffer_size 且 active >= buffer_size/2)
       → ShouldFlush() = true → HandleWriteBufferManagerFlush → SwitchMemtable
            └─ AllocTracker::DoneAllocating → ScheduleFreeMem → memory_active_ -= bytes（立刻）
  ② memory_used_ >= buffer_size 且 allow_stall
       → ShouldStall() = true
            ├─ no_slowdown=true  → 返回 Status::Incomplete("Write stall")
            └─ 否则              → BeginWriteStall + WBMStallInterface::Block() 睡眠

后台 flush 完成，immutable memtable 析构：
  └─ AllocTracker::FreeMem(bytes)
       └─ WBM::FreeMem(bytes)
            ├─ memory_used_ -= bytes
            └─ (若配 cache) 删 dummy entry（延迟到 3/4 阈值）
            └─ MaybeEndWriteStall()
                 └─ 若 memory_used_ < buffer_size：stall_active_=false，遍历 queue_ 全部 Signal() 唤醒
```

### 读路径（默认配置，永不 stall）

```
Get/MultiGet/Iterator 读一个 block
  ├─ block_cache.LookupFull → 命中：直接用
  └─ 未命中：从文件读 → PutDataBlockToCache → block_cache.InsertFull
       └─ LRUCacheShard::InsertItem:
            ├─ EvictFromLRU：while ((usage_ + charge) > capacity_) 淘汰 LRU 最老无引用 entry
            └─ 仍超容量时：
                 ├─ strict=false（默认）：照常插入，usage 可超 capacity，读成功（只是未来更多淘汰/IO）
                 └─ strict=true：返回 MemoryLimit（仅 kBlockBasedTableReader/kFileMetadata 预留失败时才传到查询）

table 句柄数：max_open_files-10 容量的 table_cache，LRU 淘汰旧 reader，不报错
WBM 不参与读路径任何决策
```

---

## 八、关键文件索引

| 主题 | 文件:行 |
|---|---|
| WBM 类定义 | `include/rocksdb/write_buffer_manager.h` |
| WBM 实现（ReserveMem/FreeMem/Stall） | `memtable/write_buffer_manager.cc` |
| AllocTracker（memtable 计费员） | `memtable/alloc_tracker.cc` |
| MemTable 计费接入 | `db/memtable.cc:80,169` / `db/memtable.h:529,640` |
| 单 memtable flush 判定 | `db/memtable.cc:192-258`（`ShouldFlushNow`） |
| ShouldFlush / ShouldStall 阈值 | `include/rocksdb/write_buffer_manager.h:101-142` |
| 写路径触发 flush | `db/db_impl/db_impl_write.cc:1214-1226`（`HandleWriteBufferManagerFlush:1842-1932`） |
| 写路径 stall | `db/db_impl/db_impl_write.cc:1268-1278`（`WriteBufferManagerStallWrites:2049-2068`） |
| StallInterface / WBMStallInterface | `include/rocksdb/write_buffer_manager.h:28-35` / `db/db_impl/db_impl.h:1246-1292` |
| CacheReservationManager | `cache/cache_reservation_manager.h` / `.cc` |
| dummy entry 插入/释放 | `cache/cache_reservation_manager.cc:111-147` |
| LRU cache 淘汰与 MemoryLimit | `cache/lru_cache.cc:322-335`（EvictFromLRU）/ `:383-395`（InsertItem）/ `:394`（MemoryLimit 串） |
| 读路径 block cache Insert | `table/block_based/block_based_table_reader.cc:1328,1392` |
| 读路径 table reader 预留 | `table/block_based/block_based_table_reader.cc:805-817` |
| 文件元数据预留 | `db/version_builder.cc:295-307,795-809` |
| table_cache 容量（max_open_files） | `db/db_impl/db_impl.cc:374-388` / `db/table_cache.cc:218` |
| stall 统计 | `db/write_stall_stats.cc:18-98` / `db/internal_stats.cc:90` |
