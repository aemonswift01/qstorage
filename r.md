# gstore 持久化哈希索引落盘设计分析

## 一、设计总览

gstore 的持久化哈希索引采用 **"桶目录（Bucket Directory） + 追加日志（Append-only Log） + 拉链法（Separate Chaining）"** 的两文件分离式落盘结构，每个分段（segment）每个索引拥有两个磁盘文件：

| 文件后缀 | 角色 | 内容 |
|---|---|---|
| `gstore_hash_index_<id>_i.dbf` | 桶目录文件（Array 文件） | 扁平数组，每槽 5 字节，存放指向 Key 文件中冲突链首记录的 40 位偏移 |
| `gstore_hash_index_<id>_ii.dbf` | 键记录文件（Key 文件） | 追加写入的 hrec 记录流，每条记录包含键值与 `prev_offset` 形成拉链 |

关键源码：
- 结构定义：`src/include/storage/gdm_hash_index.h`
- 主实现：`src/storage/gdm_hash_index.c`（4332 行）
- 记录格式：`src/include/fsm/gdm_hrec.h`
- 哈希算法：`src/utils/gdm_murmurhash.c`（MurmurHash3）

---

## 二、核心数据结构

### 2.1 文件头 `t_hash_index_meta`（`gdm_hash_index.h:64-75`）
```
预留 64 KB 文件头（兼容 Win/Linux 的 mmap 偏移对齐），实际刷盘 4 KB
偏移布局: [lsn(8)][xid(8)][last_vid(4)][last_eid(5)][last_rid(4)][offset(5)][mem_flag(1)]
```
其中 `mem_flag` 单字节标识上一运行是否为干净的 mmap 落盘态——崩溃恢复的关键判据。

### 2.2 桶目录槽位 `t_hash_index_table`（`gdm_hash_index.h:49-62`）
- **每个槽位固定 5 字节**（`BYTES5_SIZE`，40 位偏移），可寻址 ~1 TB
- 通过 `edn_read_5bytes / edn_write_5bytes` 读写
- 目录大小在创建时按统计值估算：`估算条目数 × 1.2 (头冗余) × 5 字节`，向下对齐到 5 MB（`HASH_INDEX_TABLE_MAP_SIZE`），即每 mmap 块恰好容纳 1M 个槽
- **运行期永不分裂、永不 rehash**——所有冲突都进入拉链

### 2.3 哈希记录 hrec（`gdm_hrec.h:13-24`）
```
[len(2)][prev_offset(5)][arrayid(5)][null_flag(1)][hashcode(2)]  ← 15B 头
[ key data ... ]
[checksum(1)][delimiter(1)=0xFF]                                  ← 2B 尾
```
- `prev_offset`：构成冲突链（拉链）的前驱指针
- `delimiter == 0xFF`（`HREC_MAGIC_NUM`）+ `checksum`：**逐记录崩溃恢复校验**，加载时遇到任一非法即截断（`_hash_index_load_page`, `gdm_hash_index.c:876/1030`）
- 记录**绝不跨页**，剩余空间不足则整页跳过

### 2.4 内存索引对象 `t_hash_index`（`gdm_hash_index.h:80-122`）
亮点设计：
- **缓存行填充避免伪共享**：`head`/`tail`/`block_count`/`max_map_count` 等热字段均用 `pad[CACHE_LINE_SIZE - n]` 隔离（注释："避免伪共享"）
- **双 meta 快照**：`cur_meta`（活跃写入）↔ `frozen_meta`（冻结刷盘）通过指针原子交换实现无阻塞 checkpoint
- **32 条分段行锁**（`ROW_LOCK_SIZE = 32`，按 `slot % 32` 加锁）兼顾并发与低争用

---

## 三、借鉴的核心思想

1. **拉链法（Separate Chaining）处理冲突** —— 经典哈希表思想，但把链表节点直接放进**追加日志文件**，而非独立的溢出页。
2. **Log-Structured Append-Only 存储** —— Key 文件只追加不修改，类似 LSM / 日志结构文件系统的写优化思想；随机写转化为顺序写。
3. **Directory + Bucket 分离** —— 借鉴 Linear Hashing 的"扁平桶目录 + 槽指向数据"思路，但 gstore 选择**固定桶数、不做动态分裂**，靠链长吸收冲突。
4. **混合内存/mmap 模式** —— 批量导入用纯内存块 + 头插法（O(1) 插入，崩溃即整体重建），稳态运行用 mmap 块 + 尾插法（链路增量完整，崩溃只丢末尾半条记录）。两种模式可运行期互转（`hash_index_table_mmap_low` / `hash_index_table_mem_low`）。
5. **双插法策略**（`gdm_hash_index.c:1981-2034` 原始中文注释）：
   - 内存版用**头插法**：只需知数组内的文件偏移，O(1)；但有断链风险，须保证最终一次性落盘——导入/重建场景可接受。
   - 映射版用**尾插法**：必须查询冲突链才能拿到上条偏移，但无断链风险，增量落盘安全。
6. **MurmurHash3 非加密哈希** —— 全栈哈希基座（逐列哈希、列间 combine、12-bit 段路由哈希），追求分布均匀与速度而非安全性。
7. **WAL + Checkpoint 联动** —— flush 前校验 `frozen_meta->lsn <= vlog_buf->tail`，落盘后通过 `ver_hash_add` 写版本差分，与全局 WAL/检查点协议一致。
8. **小端紧凑编码（5 字节偏移）** —— 空间工程化思想，用 40 位偏移同时满足 ~1 TB 寻址与 5 字节槽宽对齐。

---

## 四、与 PostgreSQL 哈希索引的区别

> 说明：gstore 代码库中确实**逐字拷贝**了 PostgreSQL 的 `src/include/lib/simplehash.h`（见 `src/include/storage/gdm_vec_simple_hash.h:90-93` 版权声明），但该 Robin Hood 开放寻址实现**仅用于向量索引（HNSW/IVFFLAT）**，与本项目落盘的图哈希索引无关。下面的对比针对 gstore 的持久化哈希索引 vs PostgreSQL 的 page-based 哈希索引。

| 维度 | gstore 持久化哈希索引 | PostgreSQL 哈希索引 |
|---|---|---|
| **整体结构** | 扁平 5 字节/槽的桶目录 + 追加日志型 Key 文件（两文件分离） | 桶页（bucket page）+ 溢出页（overflow page）同构，全部为定长 8 KB 数据页 |
| **冲突处理** | 拉链法，链节点是日志中的下一条 hrec（按 `prev_offset` 串） | 拉链法，链节点是溢出页（整页链） |
| **桶动态调整** | **无**——桶目录固定大小，永不分裂/rehash；冲突全靠链长吸收 | **有**——Linear Hashing 式按需分裂桶，`hash_expandable` 标志位 + `spares` 位图按桶号位数渐进扩容 |
| **槽/桶寻址** | `fold XOR 双掩码 → 64 位 → mod slot_count`（`_hash_index_table_calc_hash`） | 桶号 = `hash & (bucket_count - 1)`，并按 `spares` 做分裂期双映射 |
| **桶目录表示** | 扁平字节数组（5 字节/槽），直接存偏移 | 桶页本身就是 8 KB 页，页内是 `(hashcode, TID)` 二元组；桶目录是页级映射表 |
| **记录格式** | 变长 hrec：`len + prev_off + arrayid + null_flag + hashcode + key + checksum + 0xFF` | 定长 `(hashcode4B, ItemPointerData 6B)` 元组 |
| **崩溃恢复** | 逐 hrec 校验 `delimiter(0xFF)+checksum` + 头部 `mem_flag` 字节判断模式 | 依赖 WAL 重做（redo）/ `HASH_METAPAGE` + `_hash_addovflpage` 的 WAL 记录 |
| **写入路径** | 追加日志（顺序写）+ 槽位前/后插链 | 页内插入，页满则申请溢出页并链入 |
| **哈希函数** | MurmurHash3_32（逐列 + 列间 mix） | PostgreSQL 内置 `hash_any`（基于 MurmurHash 变种的融合 hash） |
| **并发控制** | 32 条分段行锁（`slot % 32`）+ 双 meta 原子快照 + 后台 flush 线程 | 桶级磁盘锁（`_hash_get_old_bucket_buf` 的 LWLock）+ 页级 share/exclusive 锁；需进程间对桶加内容锁（context lock）防止 split 中途被读 |
| **空间效率** | 5 字节/槽目录极紧凑；变长记录节省定长开销 | 8 KB 页对齐，小记录有页内碎片 |
| **是否支持 mvcc** | 通过 `xid`/`lsn` 在 meta 中维护，配合 WAL；记录级带 null_flag/gflag | 完全依赖堆元组的 MVCC，索引项只存 TID |
| **WAL 集成** | flush 时把 `lsn/xid` 等写入 4 KB 文件头并 `fsync` | 完整的 WAL record 类型（`XLOG_HASH_*`）支持物理到页级 redo |
| **适用场景** | 图数据库图属性查询、键到 RowID 的二级索引（写密集 + 大数据量） | 通用 OLTP 等值查询 |

### 核心差异总结
1. **gstore 摒弃了 PG 的"定长数据页 + 溢出页"模型，改用"扁平 5 字节桶目录 + 追加日志"**，把随机写彻底变成顺序写，更贴合 SSD 友好的日志结构范式。
2. **gstore 桶目录是静态的**（创建时定尺寸，运行期不分裂），靠链长吸收冲突，换来了写路径的极简与无锁扩容复杂性；而 PG 用 Linear Hashing 动态扩容，读放大更可控但 split 期间的并发协议复杂。
3. **gstore 创新点：双插法（头插/尾插）+ 双模态（内存/mmap）切换**，针对"批量导入 vs 稳态运行"两种负载分别优化，这在 PG 的设计中不存在。
4. **崩溃恢复策略不同**：gstore 依赖逐记录魔法字节 + checksum 自描述截断；PG 依赖 WAL redo 重放。
5. **PG 的 simplehash（Robin Hood 开放寻址）在 gstore 中只用于向量索引**，并未用于持久化图哈希索引——这是常见的混淆点。

---

## 五、参考代码位置速查

| 主题 | 文件 | 行号 |
|---|---|---|
| 文件头偏移常量 | `src/include/storage/gdm_hash_index.h` | 36-44 |
| `t_hash_index_meta` 结构 | `src/include/storage/gdm_hash_index.h` | 64-75 |
| `t_hash_index` 结构（含 pad） | `src/include/storage/gdm_hash_index.h` | 80-122 |
| hrec 记录格式 | `src/include/fsm/gdm_hrec.h` | 13-24 |
| 文件命名（_i/_ii） | `src/storage/gdm_hash_index.c` | 511-536 |
| 桶目录初始化与大小估算 | `src/storage/gdm_hash_index.c` | 556-620 |
| 文件一致性检查 | `src/storage/gdm_hash_index.c` | 539 |
| 崩溃恢复（校验 0xFF/checksum） | `src/storage/gdm_hash_index.c` | 876, 1000-1051 |
| 哈希槽计算 | `src/storage/gdm_hash_index.c` | 985-997 |
| 头插/尾插设计注释 | `src/storage/gdm_hash_index.c` | 1667-1707, 1981-2039 |
| 追加写入 Key 文件 | `src/storage/gdm_hash_index.c` | 2439 |
| 后台 flush 线程 | `src/storage/gdm_hash_index.c` | 4178 |
| flush 主流程 | `src/storage/gdm_hash_index.c` | 3986-4175 |
| mmap/mem 模式互转 | `src/storage/gdm_hash_index.c` | 3431, 3556 |
| MurmurHash3 实现 | `src/utils/gdm_murmurhash.c` | 96-148 |
| 逐列哈希函数 | `src/dta/gdm_dop.c` | 84-173 |
| 多列哈希 combine | `src/dta/gdm_tuple.c` | 788-851 |
| PG simplehash（仅向量索引） | `src/include/storage/gdm_vec_simple_hash.h` | 1-94 |
