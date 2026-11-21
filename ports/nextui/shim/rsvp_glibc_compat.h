#pragma once
// Older glibc (the TrimUI rootfs ships 2.23) lacks the getrandom() wrapper; the kernel has the call.
#include <features.h>
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 25)
#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
static inline ssize_t getrandom(void* buffer, size_t length, unsigned int flags) {
    return (ssize_t) syscall(SYS_getrandom, buffer, length, flags);
}
#endif
