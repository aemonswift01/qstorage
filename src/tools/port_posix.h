#pragma once

namespace qstorage::tools {
static inline void AsmVolatilePause() {
#if defined(__i386__) || defined(__x86_64__)
    asm volatile("pause");
#elif defined(__aarch64__)
    asm volatile("isb");
#elif defined(__powerpc64__)
    asm volatile("or 27,27,27");
#elif defined(__loongarch64)
    asm volatile("dbar 0");
#endif
    // it's okay for other platforms to be no-ops
}
}  // namespace qstorage::tools
