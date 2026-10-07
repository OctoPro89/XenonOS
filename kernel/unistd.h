#pragma once

#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <kernel.h>

// Standard file descriptors
#define	STDIN_FILENO	0	// Standard input
#define	STDOUT_FILENO	1	// Standard output
#define	STDERR_FILENO	2	// Standard error output

extern ASMCALL ssize_t read(int fd, void* buf, size_t count);
extern ASMCALL ssize_t write(int fd, const void* buf, size_t count);

// TODO: move to real header
typedef u64 pid_t;
extern ASMCALL pid_t spawn(const char* path);
extern ASMCALL int wait(pid_t pid);