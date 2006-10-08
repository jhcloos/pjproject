/* $Id$ */
/* 
 * Copyright (C)2003-2006 Benny Prijono <benny@prijono.org>
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
#include <pj/pool_buf.h>
#include <pj/assert.h>
#include <pj/os.h>

struct pj_pool_factory stack_based_factory;

struct creation_param
{
    void	*stack_buf;
    pj_size_t	 size;
};

static int is_initialized;
static long tls;
static void* stack_alloc(pj_pool_factory *factory, pj_size_t size);

static pj_status_t initialize()
{
    stack_based_factory.policy.block_alloc = &stack_alloc;
    return pj_thread_local_alloc(&tls);
}

static void* stack_alloc(pj_pool_factory *factory, pj_size_t size)
{
    struct creation_param *param;

    PJ_UNUSED_ARG(factory);

    param = pj_thread_local_get(tls);
    PJ_ASSERT_RETURN(param != NULL, NULL);

    pj_thread_local_set(tls, NULL);

    PJ_ASSERT_RETURN(size <= param->size, NULL);

    return param->stack_buf;
}


PJ_DEF(pj_pool_t*) pj_pool_create_on_buf(const char *name,
					 void *buf,
					 pj_size_t size)
{
    struct creation_param param;

    PJ_ASSERT_RETURN(buf && size, NULL);

    if (!is_initialized) {
	if (initialize() != PJ_SUCCESS)
	    return NULL;
    }

    param.stack_buf = buf;
    param.size = size;
    pj_thread_local_set(tls, &param);

    return pj_pool_create_int(&stack_based_factory, name, size, 0, 
			      pj_pool_factory_default_policy.callback);
}

