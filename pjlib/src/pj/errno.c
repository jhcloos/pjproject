/* $Id$ */
/* 
 * Copyright (C)2003-2007 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pj/errno.h>
#include <pj/string.h>
#include <pj/assert.h>

/* Prototype for platform specific error message, which will be defined 
 * in separate file.
 */
extern int platform_strerror( pj_os_err_type code, 
                              char *buf, pj_size_t bufsize );

#define PJLIB_MAX_ERR_MSG_HANDLER   8

/* Error message handler. */
static unsigned err_msg_hnd_cnt;
static struct err_msg_hnd
{
    pj_status_t	    begin;
    pj_status_t	    end;
    pj_str_t	  (*strerror)(pj_status_t, char*, pj_size_t);

} err_msg_hnd[PJLIB_MAX_ERR_MSG_HANDLER];

/* PJLIB's own error codes/messages */
#if defined(PJ_HAS_ERROR_STRING) && PJ_HAS_ERROR_STRING!=0

static const struct 
{
    int code;
    const char *msg;
} err_str[] = 
{
    PJ_BUILD_ERR(PJ_EUNKNOWN,      "Unknown Error" ),
    PJ_BUILD_ERR(PJ_EPENDING,      "Pending operation" ),
    PJ_BUILD_ERR(PJ_ETOOMANYCONN,  "Too many connecting sockets" ),
    PJ_BUILD_ERR(PJ_EINVAL,        "Invalid value or argument" ),
    PJ_BUILD_ERR(PJ_ENAMETOOLONG,  "Name too long" ),
    PJ_BUILD_ERR(PJ_ENOTFOUND,     "Not found" ),
    PJ_BUILD_ERR(PJ_ENOMEM,        "Not enough memory" ),
    PJ_BUILD_ERR(PJ_EBUG,          "BUG DETECTED!" ),
    PJ_BUILD_ERR(PJ_ETIMEDOUT,     "Operation timed out" ),
    PJ_BUILD_ERR(PJ_ETOOMANY,      "Too many objects of the specified type"),
    PJ_BUILD_ERR(PJ_EBUSY,         "Object is busy"),
    PJ_BUILD_ERR(PJ_ENOTSUP,	   "Option/operation is not supported"),
    PJ_BUILD_ERR(PJ_EINVALIDOP,	   "Invalid operation"),
    PJ_BUILD_ERR(PJ_ECANCELLED,    "Operation cancelled"),
    PJ_BUILD_ERR(PJ_EEXISTS,       "Object already exists" ),
    PJ_BUILD_ERR(PJ_EEOF,	   "End of file" ),
    PJ_BUILD_ERR(PJ_ETOOBIG,	   "Size is too big"),
    PJ_BUILD_ERR(PJ_ERESOLVE,	   "gethostbyname() has returned error"),
    PJ_BUILD_ERR(PJ_ETOOSMALL,	   "Size is too short"),
};
#endif	/* PJ_HAS_ERROR_STRING */


/*
 * pjlib_error()
 *
 * Retrieve message string for PJLIB's own error code.
 */
static int pjlib_error(pj_status_t code, char *buf, pj_size_t size)
{
#if defined(PJ_HAS_ERROR_STRING) && PJ_HAS_ERROR_STRING!=0
    unsigned i;

    for (i=0; i<sizeof(err_str)/sizeof(err_str[0]); ++i) {
        if (err_str[i].code == code) {
            pj_size_t len = strlen(err_str[i].msg);
            if (len >= size) len = size-1;
            pj_memcpy(buf, err_str[i].msg, len);
            buf[len] = '\0';
            return len;
        }
    }
#endif

    return pj_ansi_snprintf( buf, size, "Unknown pjlib error %d", code);
}

#define IN_RANGE(val,start,end)	    ((val)>=(start) && (val)<(end))

/* Register strerror handle. */
PJ_DECL(pj_status_t) pj_register_strerror(pj_status_t start,
					  pj_status_t space,
					  pj_str_t (*f)(pj_status_t,char*,
							pj_size_t))
{
    unsigned i;

    /* Check arguments. */
    PJ_ASSERT_RETURN(start && space && f, PJ_EINVAL);

    /* Check if there aren't too many handlers registered. */
    PJ_ASSERT_RETURN(err_msg_hnd_cnt < PJ_ARRAY_SIZE(err_msg_hnd),
		     PJ_ETOOMANY);

    /* Start error must be greater than PJ_ERRNO_START_USER */
    PJ_ASSERT_RETURN(start >= PJ_ERRNO_START_USER, PJ_EEXISTS);

    /* Check that no existing handler has covered the specified range. */
    for (i=0; i<err_msg_hnd_cnt; ++i) {
	if (IN_RANGE(start, err_msg_hnd[i].begin, err_msg_hnd[i].end) ||
	    IN_RANGE(start+space-1, err_msg_hnd[i].begin, err_msg_hnd[i].end))
	{
	    return PJ_EEXISTS;
	}
    }

    /* Register the handler. */
    err_msg_hnd[err_msg_hnd_cnt].begin = start;
    err_msg_hnd[err_msg_hnd_cnt].end = start + space;
    err_msg_hnd[err_msg_hnd_cnt].strerror = f;

    ++err_msg_hnd_cnt;

    return PJ_SUCCESS;
}

/* Internal PJLIB function called by pj_shutdown() to clear error handlers */
void pj_errno_clear_handlers(void)
{
    err_msg_hnd_cnt = 0;
    pj_bzero(err_msg_hnd, sizeof(err_msg_hnd));
}


/*
 * pj_strerror()
 */
PJ_DEF(pj_str_t) pj_strerror( pj_status_t statcode, 
			      char *buf, pj_size_t bufsize )
{
    int len = -1;
    pj_str_t errstr;

    pj_assert(buf && bufsize);

    if (statcode == PJ_SUCCESS) {
	len = pj_ansi_snprintf( buf, bufsize, "Success");

    } else if (statcode < PJ_ERRNO_START + PJ_ERRNO_SPACE_SIZE) {
        len = pj_ansi_snprintf( buf, bufsize, "Unknown error %d", statcode);

    } else if (statcode < PJ_ERRNO_START_STATUS + PJ_ERRNO_SPACE_SIZE) {
        len = pjlib_error(statcode, buf, bufsize);

    } else if (statcode < PJ_ERRNO_START_SYS + PJ_ERRNO_SPACE_SIZE) {
        len = platform_strerror(PJ_STATUS_TO_OS(statcode), buf, bufsize);

    } else {
	unsigned i;

	/* Find user handler to get the error message. */
	for (i=0; i<err_msg_hnd_cnt; ++i) {
	    if (IN_RANGE(statcode, err_msg_hnd[i].begin, err_msg_hnd[i].end)) {
		return (*err_msg_hnd[i].strerror)(statcode, buf, bufsize);
	    }
	}

	/* Handler not found! */
	len = pj_ansi_snprintf( buf, bufsize, "Unknown error %d", statcode);
    }

    if (len < 1) {
        *buf = '\0';
        len = 0;
    }

    errstr.ptr = buf;
    errstr.slen = len;

    return errstr;
}

