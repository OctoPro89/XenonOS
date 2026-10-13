#pragma once

#include <xlibc/xstddef.h>
#include <xlibc/xstdint.h>
#include <kernel.h>

// Standard file descriptors
#define	STDIN_FILENO	0	// Standard input
#define	STDOUT_FILENO	1	// Standard output
#define	STDERR_FILENO	2	// Standard error output

ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
ssize_t close(int fd);


int chdir(const char* path);
char* getcwd(char* buf, size_t size);

// NOT POSIX STANDARDS!
void* sbrk(ssize_t increment);

// TODO: move to real header
typedef u64 pid_t;
pid_t spawn(const char* path);
int wait(pid_t pid);