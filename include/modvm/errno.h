/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef MODVM_ERRNO_H
#define MODVM_ERRNO_H

/* Complete Linux asm-generic userspace errno set, with Linux libc aliases.
 * Values follow include/uapi/asm-generic/errno{,-base}.h, including the gaps
 * at 41 and 58.
 * APIs return the negatives of these values, independent of host errno.
 */
enum vm_error {
	VM_EPERM = 1, /* Operation not permitted */
	VM_ENOENT = 2, /* No such file or directory */
	VM_ESRCH = 3, /* No such process */
	VM_EINTR = 4, /* Interrupted system call */
	VM_EIO = 5, /* I/O error */
	VM_ENXIO = 6, /* No such device or address */
	VM_E2BIG = 7, /* Argument list too long */
	VM_ENOEXEC = 8, /* Exec format error */
	VM_EBADF = 9, /* Bad file number */
	VM_ECHILD = 10, /* No child processes */
	VM_EAGAIN = 11, /* Try again */
	VM_ENOMEM = 12, /* Out of memory */
	VM_EACCES = 13, /* Permission denied */
	VM_EFAULT = 14, /* Bad address */
	VM_ENOTBLK = 15, /* Block device required */
	VM_EBUSY = 16, /* Device or resource busy */
	VM_EEXIST = 17, /* File exists */
	VM_EXDEV = 18, /* Cross-device link */
	VM_ENODEV = 19, /* No such device */
	VM_ENOTDIR = 20, /* Not a directory */
	VM_EISDIR = 21, /* Is a directory */
	VM_EINVAL = 22, /* Invalid argument */
	VM_ENFILE = 23, /* File table overflow */
	VM_EMFILE = 24, /* Too many open files */
	VM_ENOTTY = 25, /* Not a typewriter */
	VM_ETXTBSY = 26, /* Text file busy */
	VM_EFBIG = 27, /* File too large */
	VM_ENOSPC = 28, /* No space left on device */
	VM_ESPIPE = 29, /* Illegal seek */
	VM_EROFS = 30, /* Read-only file system */
	VM_EMLINK = 31, /* Too many links */
	VM_EPIPE = 32, /* Broken pipe */
	VM_EDOM = 33, /* Math argument out of domain of func */
	VM_ERANGE = 34, /* Math result not representable */
	VM_EDEADLK = 35, /* Resource deadlock would occur */
	VM_ENAMETOOLONG = 36, /* File name too long */
	VM_ENOLCK = 37, /* No record locks available */
	VM_ENOSYS = 38, /* Invalid system call number */
	VM_ENOTEMPTY = 39, /* Directory not empty */
	VM_ELOOP = 40, /* Too many symbolic links encountered */
	VM_ENOMSG = 42, /* No message of desired type */
	VM_EIDRM = 43, /* Identifier removed */
	VM_ECHRNG = 44, /* Channel number out of range */
	VM_EL2NSYNC = 45, /* Level 2 not synchronized */
	VM_EL3HLT = 46, /* Level 3 halted */
	VM_EL3RST = 47, /* Level 3 reset */
	VM_ELNRNG = 48, /* Link number out of range */
	VM_EUNATCH = 49, /* Protocol driver not attached */
	VM_ENOCSI = 50, /* No CSI structure available */
	VM_EL2HLT = 51, /* Level 2 halted */
	VM_EBADE = 52, /* Invalid exchange */
	VM_EBADR = 53, /* Invalid request descriptor */
	VM_EXFULL = 54, /* Exchange full */
	VM_ENOANO = 55, /* No anode */
	VM_EBADRQC = 56, /* Invalid request code */
	VM_EBADSLT = 57, /* Invalid slot */
	VM_EBFONT = 59, /* Bad font file format */
	VM_ENOSTR = 60, /* Device not a stream */
	VM_ENODATA = 61, /* No data available */
	VM_ETIME = 62, /* Timer expired */
	VM_ENOSR = 63, /* Out of streams resources */
	VM_ENONET = 64, /* Machine is not on the network */
	VM_ENOPKG = 65, /* Package not installed */
	VM_EREMOTE = 66, /* Object is remote */
	VM_ENOLINK = 67, /* Link has been severed */
	VM_EADV = 68, /* Advertise error */
	VM_ESRMNT = 69, /* Srmount error */
	VM_ECOMM = 70, /* Communication error on send */
	VM_EPROTO = 71, /* Protocol error */
	VM_EMULTIHOP = 72, /* Multihop attempted */
	VM_EDOTDOT = 73, /* RFS specific error */
	VM_EBADMSG = 74, /* Not a data message */
	VM_EOVERFLOW = 75, /* Value too large for defined data type */
	VM_ENOTUNIQ = 76, /* Name not unique on network */
	VM_EBADFD = 77, /* File descriptor in bad state */
	VM_EREMCHG = 78, /* Remote address changed */
	VM_ELIBACC = 79, /* Can not access a needed shared library */
	VM_ELIBBAD = 80, /* Accessing a corrupted shared library */
	VM_ELIBSCN = 81, /* .lib section in a.out corrupted */
	VM_ELIBMAX = 82, /* Attempting to link in too many shared libraries */
	VM_ELIBEXEC = 83, /* Cannot exec a shared library directly */
	VM_EILSEQ = 84, /* Illegal byte sequence */
	VM_ERESTART = 85, /* Interrupted system call should be restarted */
	VM_ESTRPIPE = 86, /* Streams pipe error */
	VM_EUSERS = 87, /* Too many users */
	VM_ENOTSOCK = 88, /* Socket operation on non-socket */
	VM_EDESTADDRREQ = 89, /* Destination address required */
	VM_EMSGSIZE = 90, /* Message too long */
	VM_EPROTOTYPE = 91, /* Protocol wrong type for socket */
	VM_ENOPROTOOPT = 92, /* Protocol not available */
	VM_EPROTONOSUPPORT = 93, /* Protocol not supported */
	VM_ESOCKTNOSUPPORT = 94, /* Socket type not supported */
	VM_EOPNOTSUPP = 95, /* Operation not supported on transport endpoint */
	VM_EPFNOSUPPORT = 96, /* Protocol family not supported */
	VM_EAFNOSUPPORT = 97, /* Address family not supported by protocol */
	VM_EADDRINUSE = 98, /* Address already in use */
	VM_EADDRNOTAVAIL = 99, /* Cannot assign requested address */
	VM_ENETDOWN = 100, /* Network is down */
	VM_ENETUNREACH = 101, /* Network is unreachable */
	VM_ENETRESET = 102, /* Network dropped connection because of reset */
	VM_ECONNABORTED = 103, /* Software caused connection abort */
	VM_ECONNRESET = 104, /* Connection reset by peer */
	VM_ENOBUFS = 105, /* No buffer space available */
	VM_EISCONN = 106, /* Transport endpoint is already connected */
	VM_ENOTCONN = 107, /* Transport endpoint is not connected */
	VM_ESHUTDOWN = 108, /* Cannot send after transport endpoint shutdown */
	VM_ETOOMANYREFS = 109, /* Too many references: cannot splice */
	VM_ETIMEDOUT = 110, /* Connection timed out */
	VM_ECONNREFUSED = 111, /* Connection refused */
	VM_EHOSTDOWN = 112, /* Host is down */
	VM_EHOSTUNREACH = 113, /* No route to host */
	VM_EALREADY = 114, /* Operation already in progress */
	VM_EINPROGRESS = 115, /* Operation now in progress */
	VM_ESTALE = 116, /* Stale file handle */
	VM_EUCLEAN = 117, /* Structure needs cleaning */
	VM_ENOTNAM = 118, /* Not a XENIX named type file */
	VM_ENAVAIL = 119, /* No XENIX semaphores available */
	VM_EISNAM = 120, /* Is a named type file */
	VM_EREMOTEIO = 121, /* Remote I/O error */
	VM_EDQUOT = 122, /* Quota exceeded */
	VM_ENOMEDIUM = 123, /* No medium found */
	VM_EMEDIUMTYPE = 124, /* Wrong medium type */
	VM_ECANCELED = 125, /* Operation Canceled */
	VM_ENOKEY = 126, /* Required key not available */
	VM_EKEYEXPIRED = 127, /* Key has expired */
	VM_EKEYREVOKED = 128, /* Key has been revoked */
	VM_EKEYREJECTED = 129, /* Key was rejected by service */
	VM_EOWNERDEAD = 130, /* Owner died */
	VM_ENOTRECOVERABLE = 131, /* State not recoverable */
	VM_ERFKILL = 132, /* Operation not possible due to RF-kill */
	VM_EHWPOISON = 133, /* Memory page has hardware error */

	/* Linux aliases share their canonical values. */
	VM_EWOULDBLOCK = VM_EAGAIN,
	VM_EDEADLOCK = VM_EDEADLK,
	VM_ENOTSUP = VM_EOPNOTSUPP,
};

#endif /* MODVM_ERRNO_H */
