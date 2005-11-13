/* $Id$
 *
 */
/* 
 * PJLIB - PJ Foundation Library
 * (C)2003-2005 Benny Prijono <bennylp@bulukucing.org>
 *
 * Author:
 *  Benny Prijono <bennylp@bulukucing.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* $Log: /pjproject-0.3/pjlib/include/pj/compat/setjmp.h $
 * 
 * 4     10/14/05 12:26a Bennylp
 * Finished error code framework, some fixes in ioqueue, etc. Pretty
 * major.
 * 
 * 3     9/22/05 10:31a Bennylp
 * Moving all *.h files to include/.
 * 
 * 2     9/21/05 1:39p Bennylp
 * Periodic checkin for backup.
 * 
 * 1     9/17/05 10:36a Bennylp
 * Created.
 * 
 */
#ifndef __PJ_COMPAT_SETJMP_H__
#define __PJ_COMPAT_SETJMP_H__

/**
 * @file setjmp.h
 * @brief Provides setjmp.h functionality.
 */

#if defined(PJ_HAS_SETJMP_H) && PJ_HAS_SETJMP_H != 0
#  include <setjmp.h>
   typedef jmp_buf pj_jmp_buf;
#  define pj_setjmp(buf)	setjmp(buf)
#  define pj_longjmp(buf,d)	longjmp(buf,d)

#elif defined(PJ_LINUX_KERNEL) && PJ_LINUX_KERNEL != 0 && \
      defined(PJ_M_I386) && PJ_M_I386 != 0

    /*
     * These are taken from uClibc.
     * Copyright (C) 2000-2003 Erik Andersen <andersen@uclibc.org>
     */
#   if defined __USE_MISC || defined _ASM
#	define JB_BX	0
#	define JB_SI	1
#	define JB_DI	2
#	define JB_BP	3
#	define JB_SP	4
#	define JB_PC	5
#	define JB_SIZE 24
#   endif

# ifndef _ASM
	typedef int __jmp_buf[6];

    /* A `sigset_t' has a bit for each signal.  */
#   define _SIGSET_NWORDS	(1024 / (8 * sizeof (unsigned long int)))
    typedef struct __sigset_t_tag
    {
	unsigned long int __val[_SIGSET_NWORDS];
    } __sigset_t;

    /* Calling environment, plus possibly a saved signal mask.  */
    typedef struct __jmp_buf_tag    /* C++ doesn't like tagless structs.  */
    {
	/* NOTE: The machine-dependent definitions of `__sigsetjmp'
	   assume that a `jmp_buf' begins with a `__jmp_buf' and that
	   `__mask_was_saved' follows it.  Do not move these members
	   or add others before it.  */
	__jmp_buf __jmpbuf;		/* Calling environment.  */
	int __mask_was_saved;		/* Saved the signal mask?  */
	// we never saved the mask.
	__sigset_t __saved_mask;	/* Saved signal mask.  */
    } jmp_buf[1];

    typedef jmp_buf sigjmp_buf;
    typedef jmp_buf pj_jmp_buf;

    PJ_DECL(int) pj_setjmp(pj_jmp_buf env);
    PJ_DECL(void) pj_longjmp(pj_jmp_buf env, int val) __attribute__((noreturn));

# endif   /* _ASM */

#else
#  warning "setjmp()/longjmp() is not implemented"
   typedef int pj_jmp_buf[1];
#  define pj_setjmp(buf)	0
#  define pj_longjmp(buf,d)	0
#endif


#endif	/* __PJ_COMPAT_SETJMP_H__ */

