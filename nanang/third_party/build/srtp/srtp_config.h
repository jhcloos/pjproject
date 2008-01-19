#include <pj/types.h>

/* Define if building for a CISC machine (e.g. Intel). */
/* #define CPU_CISC 1 */

/* Define if building for a RISC machine (assume slow byte access). */
/* #undef CPU_RISC */

/* PJLIB can't detect CISC vs RISC CPU, so we'll just say RISC here */
#if defined(PJ_WIN32) && PJ_WIN32!=0
#   define CPU_CISC	1
#else
#   define CPU_RISC	1
#endif

/* Path to random device */
/* #define DEV_URANDOM "/dev/urandom" */

/* Define to compile in dynamic debugging system. */
#define ENABLE_DEBUGGING 1

/* Report errors to this file. */
/* #undef ERR_REPORTING_FILE */

/* Define to use logging to stdout. */
//#define ERR_REPORTING_STDOUT 1

/* Define this to use ISMAcryp code. */
/* #undef GENERIC_AESICM */

/* Define to 1 if you have the <arpa/inet.h> header file. */
#ifdef PJ_HAS_ARPA_INET_H 
#   define HAVE_ARPA_INET_H 1
#endif

/* Define to 1 if you have the <byteswap.h> header file. */
/* #undef HAVE_BYTESWAP_H */

/* Define to 1 if you have the `inet_aton' function. */
#ifdef PJ_SOCK_HAS_INET_PTON
#   define HAVE_INET_ATON   1
#endif

/* Define to 1 if the system has the type `int16_t'. */
#define HAVE_INT16_T 1

/* Define to 1 if the system has the type `int32_t'. */
#define HAVE_INT32_T 1

/* Define to 1 if the system has the type `int8_t'. */
#define HAVE_INT8_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
/* #undef HAVE_INTTYPES_H */

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* Define to 1 if you have the <machine/types.h> header file. */
/* #undef HAVE_MACHINE_TYPES_H */

/* Define to 1 if you have the <memory.h> header file. */
//#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#ifdef PJ_HAS_NETINET_IN_H
#   define HAVE_NETINET_IN_H	1
#endif

/* Define to 1 if you have the `socket' function. */
/* #undef HAVE_SOCKET */

/* Define to 1 if you have the <stdint.h> header file. */
/* #undef HAVE_STDINT_H */

/* Define to 1 if you have the <stdlib.h> header file. */
#ifdef PJ_HAS_STDLIB_H
#   define HAVE_STDLIB_H 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
//#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#ifdef PJ_HAS_STRING_H
#   define HAVE_STRING_H 1
#endif

/* Define to 1 if you have the <syslog.h> header file. */
/* #undef HAVE_SYSLOG_H */

/* Define to 1 if you have the <sys/int_types.h> header file. */
/* #undef HAVE_SYS_INT_TYPES_H */

/* Define to 1 if you have the <sys/socket.h> header file. */
#ifdef PJ_HAS_SYS_SOCKET_H
#   define HAVE_SYS_SOCKET_H	1
#endif

/* Define to 1 if you have the <sys/stat.h> header file. */
//#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#ifdef PJ_HAS_SYS_TYPES_H
#   define HAVE_SYS_TYPES_H 1
#endif

/* Define to 1 if you have the <sys/uio.h> header file. */
/* #undef HAVE_SYS_UIO_H */

/* Define to 1 if the system has the type `uint8_t'. */
#define HAVE_UINT8_T 1

/* Define to 1 if the system has the type `uint16_t'. */
#define HAVE_UINT16_T 1

/* Define to 1 if the system has the type `uint32_t'. */
#define HAVE_UINT32_T 1

/* Define to 1 if the system has the type `uint64_t'. */
#define HAVE_UINT64_T 1

/* Define to 1 if you have the <unistd.h> header file. */
/* Define to 1 if you have the `usleep' function. */
#ifdef PJ_HAS_UNISTD_H
#   define HAVE_UNISTD_H    1
#   define HAVE_USLEEP	    1
#endif


/* Define to 1 if you have the <windows.h> header file. */
#if defined(PJ_WIN32) && PJ_WIN32!=0
#   define HAVE_WINDOWS_H 1
#endif

/* Define to 1 if you have the <winsock2.h> header file. */
#ifdef PJ_HAS_WINSOCK2_H 
#   define HAVE_WINSOCK2_H 1
#endif

/* Define to use X86 inlined assembly code */
/* #undef HAVE_X86 */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* The size of a `unsigned long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG (sizeof(unsigned long))

/* The size of a `unsigned long long', as computed by sizeof. */
#define SIZEOF_UNSIGNED_LONG_LONG 8

/* Define to use GDOI. */
/* #undef SRTP_GDOI */

/* Define to compile for kernel contexts. */
/* #undef SRTP_KERNEL */

/* Define to compile for Linux kernel context. */
/* #undef SRTP_KERNEL_LINUX */

/* Define to 1 if you have the ANSI C header files. */
//#define STDC_HEADERS 1

/* Write errors to this file */
/* #undef USE_ERR_REPORTING_FILE */

/* Define to use syslog logging. */
/* #undef USE_SYSLOG */

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* MSVC does't have "inline" */
#if defined(_MSC_VER)
#   define inline _inline
#endif

/* Define to `unsigned' if <sys/types.h> does not define. */
/* #undef size_t */

#if (_MSC_VER >= 1400) // VC8+
#   ifndef _CRT_SECURE_NO_DEPRECATE
#	define _CRT_SECURE_NO_DEPRECATE
#   endif
#   ifndef _CRT_NONSTDC_NO_DEPRECATE
#	define _CRT_NONSTDC_NO_DEPRECATE
#   endif
#endif // VC8+

#ifndef int_types_defined
    typedef pj_uint8_t	uint8_t;
    typedef pj_uint16_t	uint16_t;
    typedef pj_uint32_t uint32_t;
    typedef pj_uint64_t uint64_t;
    typedef pj_int8_t	int8_t;
    typedef pj_int16_t	int16_t;
    typedef pj_int32_t	int32_t;
    typedef pj_int64_t	int64_t;
#   define int_types_defined
#endif

#ifdef _MSC_VER
    #pragma warning(disable:4311)
    #pragma warning(disable:4761) // integral mismatch
    #pragma warning(disable:4018) // signed/unsigned mismatch
    #pragma warning(disable:4244) // conversion from int64 to int
#endif

