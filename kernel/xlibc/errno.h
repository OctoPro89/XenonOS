#pragma once

#define EPERM           1  // Operation not permitted
#define ENOENT          2  // No such file or directory
#define ESRCH           3  // No such process
#define EINTR           4  // Interrupted system call
#define EIO             5  // I/O error
#define	E2BIG		    7  // Argument list too long
#define EBADF           9  // Bad file number
#define ECHILD          10 // No child processes
#define EAGAIN          11 // Try again
#define ENOMEM          12 // Out of memory
#define EACCES          13 // Permission denied
#define EFAULT          14 // Bad address
#define	EEXIST		    17 // File exists
#define	ENOTDIR		    20 // Not a directory
#define	EISDIR		    21 // Is a directory
#define EINVAL          22 // Invalid argument
#define EMFILE          24 // Too many open files
#define ENOSPC          28 // No space left on device
#define ESPIPE          29 // Illegal seek
#define EPIPE           32 // Broken pipe
#define	ERANGE		    34 // Math result not representable
#define ENAMETOOLONG    36 // File name too long
#define ENOSYS          38 // Invalid syscall number

// TODO: thread-local errno
extern int errno;