#include <unistd.h>

namespace qstorage::io {
// kernel_completion = 内核做完 IO 后，用来通知结果的最顶层抽象接口 **
class KernelCompletion {
   public:
    // 5. 纯虚函数：内核调用它来通知 IO 完成
    virtual void CompleteWith(ssize_t res) = 0;

   protected:
    KernelCompletion() = default;
    KernelCompletion(KernelCompletion&&) = default;
    KernelCompletion& operator=(KernelCompletion&&) = default;
    ~KernelCompletion() = default;
};
}  // namespace qstorage::io