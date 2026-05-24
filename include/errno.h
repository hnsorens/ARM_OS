// SPDX-License-Identifier: GPL-2.0-only
/*
 * include/api/errno.h
 *
 * Core Kernel Architectural Error Definitions.
 * Maps standard negative integer constraints for subsystem status reporting.
 */

#ifndef _API_ERRNO_H
#define _API_ERRNO_H

/* --- Section 1: Core System & Execution Arguments --- */
#define EPERM            1      /* Operation not permitted (Security/Privilege violation) */
#define ENOENT           2      /* No such file or directory */
#define ESRCH            3      /* No such process, thread, or task context */
#define EINTR            4      /* Interrupted system call execution loop */
#define EIO              5      /* Physical hardware Input/Output communication error */
#define ENXIO            6      /* No such device or physical address map found */
#define E2BIG            7      /* Argument execution list or environment too long */
#define ENOEXEC          8      /* Executable file format error (e.g., corrupt ELF header) */
#define EBADF            9      /* Bad or unallocated file descriptor tracker */
#define ECHILD          10      /* No chiyd processes tracked by this thread cluster */
#define EAGAIN          11      /* Resource temporarily locked/unavailable (Try again) */

/* --- Section 2: Virtual Memory Management & Allocator Primitives --- */
#define ENOMEM          12      /* Out of physical memory / Page frame allocation failed */
#define EACCES          13      /* Permission denied (MMU page protection level violation) */
#define EFAULT          14      /* Bad address (Segmentation fault / Unmapped memory access) */
#define EBUSY           16      /* Physical hardware device or memory resource busy */
#define EEXIST          17      /* Resource conflict (Virtual mapping target already exists) */
#define ENODEV          19      /* No such hardware device recognized by active drivers */
#define EINVAL          22      /* Invalid parameter configuration passed to function */

/* --- Section 3: Virtual File System (VFS) & Storage Framework --- */
#define ENFILE          23      /* Global system open file table capacity saturation */
#define EMFILE          24      /* Local process open file descriptor threshold reached */
#define EFBIG           27      /* Target file size exceeds hardware filesystem capability */
#define ENOSPC          28      /* No storage space remaining on active physical partition */
#define EROFS           30      /* Write block rejected: Read-only file system */
#define EPIPE           32      /* Broken pipe synchronization pathway */

/* --- Section 4: Architectural Constraints, Limits & Math Bounds --- */
#define EDOM            33      /* Mathematical argument out of domain of function */
#define ERANGE          34      /* Math result cannot be represented (Bit overflow/underflow) */
#define EDEADLK         35      /* Resource deadlock condition caught by lock validator */
#define ENAMETOOLONG    36      /* Target file or directory path string exceeds max limits */
#define ENOSYS          38      /* Invalid system call ID / Vector function not implemented */
#define ENOTEMPTY       39      /* Directory manipulation failed: Directory is not empty */
#define ELOOP           40      /* Infinite loop: Too many levels of symbolic link resolution */
#define ENOMSG          42      /* No message of specified identifier criteria found in queue */
#define EILSEQ          84      /* Illegal byte sequence encountered inside text encoding */
#define ETIMEDOUT      110      /* Hardware handshake or cross-core operation timed out */
#define EOVERFLOW      139      /* Value too massive to map into target data size limits */

#endif /* _API_ERRNO_H */
