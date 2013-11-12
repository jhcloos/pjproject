/* $Id$ */
/* 
 * Copyright (C) 2008-2013 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#include <pj/list.h>
#include <pj/string.h>
#include <pj/pool.h>

/** Buffer used to discover size of inflated data */
#define INFLATE_TEST_BUFSIZE 512

/** zlib requires a free() function */
PJ_DEF(void) pj_pool_faux_free( pj_pool_t *pool, const void *addr)
{
    return;
}


PJ_DEF(pj_size_t) pj_deflate_size( pj_pool_t *pool, const char *src, const pj_size_t src_len)
{
    return compressBound(src_len);
}

PJ_DEF(pj_status_t) pj_deflate( pj_pool_t *pool, char *dest, pj_size_t *dest_len, const char *src, const pj_size_t src_len)
{
    z_stream z;
    int ret;

    dest_len = (pj_size_t *) pj_pool_zalloc(pool, sizeof(pj_size_t));
    *dest_len = pj_deflate_size(pool, src, src_len);

    dest = (char *) pj_pool_zalloc(pool, *dest_len);

    ret = compress(dest, dest_len, src, src_len);

    switch (ret) {
    case Z_OK:
	return PJ_SUCCESS;
    case Z_MEM_ERROR:
	return PJ_ENOMEM;
    case Z_BUF_ERROR:
	return PJ_ETOOBIG;
    case Z_DATA_ERROR:
	return PJ_EINVAL;
    default:
	return PJ_EUNKNOWN;
    }
}

PJ_DEF(pj_size_t) pj_inflate_size( pj_pool_t *pool, const char *src, const pj_size_t src_len)
{
    z_stream z;
    int ret, flush;
    char *buf;
    pj_size_t total = 0;

    buf = (char*) pj_pool_alloc(pool, INFLATE_TEST_BUFSIZE);

    z.next_in = src;
    z.avail_in = src_len;

    z.opaque = pool;
    z.alloc_func = pj_pool_calloc;
    z.free_func = pj_pool_faux_free;

    ret = inflateInit(&z);

    for(;;) {
	z.next_out = buf;
	z.inflate_out = INFLATE_TEST_BUFSIZE;

	ret = inflate(z, Z_SYNC_FLUSH);

	if (ret != Z_OK) {
	    total = z.total_out;
	    inflateEnd(&z);
	    return total;
	}
    }
}

PJ_DEF(pj_status_t) pj_inflate( pj_pool_t *pool, char *dest, pj_size_t *dest_len, const char *src, const pj_size_t src_len)
{
    z_stream z;
    int ret;

    dest_len = (pj_size_t *) pj_pool_zalloc(pool, sizeof(pj_size_t));
    *dest_len = pj_inflate_size(pool, src, src_len);

    dest = (char *) pj_pool_zalloc(pool, *dest_len);

    ret = uncompress(dest, dest_len, src, src_len);

    switch (ret) {
    case Z_OK:
	return PJ_SUCCESS;
    case Z_MEM_ERROR:
	return PJ_ENOMEM;
    case Z_BUF_ERROR:
	return PJ_ETOOBIG;
    case Z_DATA_ERROR:
	return PJ_EINVAL;
    default:
	return PJ_EUNKNOWN;
    }
}

