#pragma once

#define EPERM       1 // Operation not permitted
#define ENOENT      2 // No such file or directory
#define ESRCH       3 // No such process
#define EINTR       4 // Interrupted system call
#define EIO         5 // I/O error
#define EBADF       9 // Bad file number
#define ECHILD     10 // No child processes
#define EAGAIN     11 // Try again
#define ENOMEM     12 // Out of memory
#define EACCES     13 // Permission denied
#define EFAULT     14 // Bad address
#define EINVAL     22 // Invalid argument
#define EMFILE     24 // Too many open files
#define ENOSPC     28 // No space left on device
#define ESPIPE     29 // Illegal seek
#define EPIPE      32 // Broken pipe
#define ENOSYS     38 // 