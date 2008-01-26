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
#include <pj/pool.h>
#include <pj/except.h>
#include <pj/os.h>
#include <pj/compat/malloc.h>

#if !PJ_HAS_POOL_ALT_API

/*
 * This file contains pool default policy definition and implementation.
 */


static void *default_block_alloc(pj_pool_factory *factory, pj_size_t size)
{
    void *p;

    PJ_CHECK_STACK();

    if (factory->on_block_alloc) {
	int rc;
	rc = factory->on_block_alloc(factory, size);
	if (!rc)
	    return NULL;
    }

    p = malloc(size);

    if (p == NULL) {
	if (factory->on_block_free) 
	    factory->on_block_free(factory, size);
    }

    return p;
}

static void default_block_free(pj_pool_factory *factory, void *mem, 
			       pj_size_t size)
{
    PJ_CHECK_STACK();

    if (factory->on_block_free) 
        factory->on_block_free(factory, size);

    free(mem);
}

static void default_pool_callback(pj_pool_t *pool, pj_size_t size)
{
    PJ_CHECK_STACK();
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(size);

    PJ_THROW(PJ_NO_MEMORY_EXCEPTION);
}

pj_pool_factory_policy pj_pool_factory_default_policy = 
{
    &default_block_alloc,
    &default_block_free,
    &default_pool_callback,
    0
};

#endif	/* PJ_HAS_POOL_ALT_API */
