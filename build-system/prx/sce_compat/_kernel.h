#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define SCE_KERNEL_O_RDONLY 0x0000  /* [F] */
#define SCE_KERNEL_O_WRONLY 0x0001
#define SCE_KERNEL_O_APPEND 0x0008
#define SCE_KERNEL_O_CREAT  0x0200
#ifdef __cplusplus
extern "C" {
#endif
int sceKernelOpen(const char* path, int flags, int mode);
int sceKernelClose(int fd);
ssize_t sceKernelRead(int fd, void* buf, size_t n);
ssize_t sceKernelWrite(int fd, const void* buf, size_t n);
int sceKernelDebugOutText(int channel, const char* text);
#ifdef __cplusplus
}
#endif
