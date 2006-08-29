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

#include <e32cmn.h>
#include <e32std.h>

#include <pj/os.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/log.h>
#include <pj/rand.h>
#include <pj/string.h>
#include <pj/guid.h>
#include <pj/except.h>
#include <pj/errno.h>


#define PJ_MAX_TLS	    32
#define DUMMY_MUTEX	    ((pj_mutex_t*)101)
#define DUMMY_SEMAPHORE	    ((pj_sem_t*)102)


/*
 * Note:
 *
 * The Symbian implementation does not support threading!
 */ 

struct pj_thread_t
{
    char	    obj_name[PJ_MAX_OBJ_NAME];
    void	   *tls_values[PJ_MAX_TLS];

} main_thread;

struct pj_atomic_t
{
    pj_atomic_value_t	value;
};


/* Flags to indicate which TLS variables have been used */
static int tls_vars[PJ_MAX_TLS];



PJ_DEF(pj_uint32_t) pj_getpid(void)
{
    return 0;
}


/*
 * pj_init(void).
 * Init PJLIB!
 */
PJ_DEF(pj_status_t) pj_init(void)
{
    pj_ansi_strcpy(main_thread.obj_name, "pjthread");
    return PJ_SUCCESS;
}


/*
 * pj_thread_create(...)
 */
PJ_DEF(pj_status_t) pj_thread_create( pj_pool_t *pool, 
				      const char *thread_name,
				      pj_thread_proc *proc, 
				      void *arg,
				      pj_size_t stack_size, 
				      unsigned flags,
				      pj_thread_t **ptr_thread)
{
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(thread_name);
    PJ_UNUSED_ARG(proc);
    PJ_UNUSED_ARG(arg);
    PJ_UNUSED_ARG(stack_size);
    PJ_UNUSED_ARG(flags);
    PJ_UNUSED_ARG(ptr_thread);

    /* Sorry mate, we don't support threading */
    return PJ_ENOTSUP;
}

/*
 * pj_thread-get_name()
 */
PJ_DEF(const char*) pj_thread_get_name(pj_thread_t *p)
{
    pj_assert(p == &main_thread);
    return p->obj_name;
}

/*
 * pj_thread_resume()
 */
PJ_DEF(pj_status_t) pj_thread_resume(pj_thread_t *p)
{
    PJ_UNUSED_ARG(p);
    return PJ_EINVALIDOP;
}

/*
 * pj_thread_this()
 */
PJ_DEF(pj_thread_t*) pj_thread_this(void)
{
    return &main_thread;
}

/*
 * pj_thread_join()
 */
PJ_DEF(pj_status_t) pj_thread_join(pj_thread_t *rec)
{
    PJ_UNUSED_ARG(rec);
    return PJ_EINVALIDOP;
}

/*
 * pj_thread_destroy()
 */
PJ_DEF(pj_status_t) pj_thread_destroy(pj_thread_t *rec)
{
    PJ_UNUSED_ARG(rec);
    return PJ_EINVALIDOP;
}

/*
 * pj_thread_sleep()
 */
PJ_DEF(pj_status_t) pj_thread_sleep(unsigned msec)
{
    PJ_UNUSED_ARG(msec);

    /* Not supported either */
    return PJ_EINVALIDOP;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * pj_thread_local_alloc()
 */

PJ_DEF(pj_status_t) pj_thread_local_alloc(long *index)
{
    unsigned i;

    /* Find unused TLS variable */
    for (i=0; i<PJ_ARRAY_SIZE(tls_vars); ++i) {
	if (tls_vars[i] == 0)
	    break;
    }

    if (i == PJ_ARRAY_SIZE(tls_vars))
	return PJ_ETOOMANY;

    tls_vars[i] = 1;
    *index = i;

    return PJ_SUCCESS;
}

/*
 * pj_thread_local_free()
 */
PJ_DEF(void) pj_thread_local_free(long index)
{
    PJ_ASSERT_ON_FAIL(index >= 0 && index < PJ_ARRAY_SIZE(tls_vars) &&
		     tls_vars[index] != 0, return);

    tls_vars[index] = 0;
}


/*
 * pj_thread_local_set()
 */
PJ_DEF(pj_status_t) pj_thread_local_set(long index, void *value)
{
    pj_thread_t *rec = pj_thread_this();

    PJ_ASSERT_RETURN(index >= 0 && index < PJ_ARRAY_SIZE(tls_vars) &&
		     tls_vars[index] != 0, PJ_EINVAL);

    rec->tls_values[index] = value;
    return PJ_SUCCESS;
}

/*
 * pj_thread_local_get()
 */
PJ_DEF(void*) pj_thread_local_get(long index)
{
    pj_thread_t *rec = pj_thread_this();

    PJ_ASSERT_RETURN(index >= 0 && index < PJ_ARRAY_SIZE(tls_vars) &&
		     tls_vars[index] != 0, NULL);

    return rec->tls_values[index];
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Create atomic variable.
 */
PJ_DEF(pj_status_t) pj_atomic_create( pj_pool_t *pool, 
				      pj_atomic_value_t initial,
				      pj_atomic_t **atomic )
{
    *atomic = (pj_atomic_t*)pj_pool_alloc(pool, sizeof(struct pj_atomic_t));
    (*atomic)->value = initial;
    return PJ_SUCCESS;
}


/*
 * Destroy atomic variable.
 */
PJ_DEF(pj_status_t) pj_atomic_destroy( pj_atomic_t *atomic_var )
{
    PJ_UNUSED_ARG(atomic_var);
    return PJ_SUCCESS;
}


/*
 * Set the value of an atomic type, and return the previous value.
 */
PJ_DEF(void) pj_atomic_set( pj_atomic_t *atomic_var, 
			    pj_atomic_value_t value)
{
    atomic_var->value = value;
}


/*
 * Get the value of an atomic type.
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_get(pj_atomic_t *atomic_var)
{
    return atomic_var->value;
}


/*
 * Increment the value of an atomic type.
 */
PJ_DEF(void) pj_atomic_inc(pj_atomic_t *atomic_var)
{
    ++atomic_var->value;
}


/*
 * Increment the value of an atomic type and get the result.
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_inc_and_get(pj_atomic_t *atomic_var)
{
    return ++atomic_var->value;
}


/*
 * Decrement the value of an atomic type.
 */
PJ_DEF(void) pj_atomic_dec(pj_atomic_t *atomic_var)
{
    --atomic_var->value;
}	


/*
 * Decrement the value of an atomic type and get the result.
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_dec_and_get(pj_atomic_t *atomic_var)
{
    return --atomic_var->value;
}


/*
 * Add a value to an atomic type.
 */
PJ_DEF(void) pj_atomic_add( pj_atomic_t *atomic_var,
			    pj_atomic_value_t value)
{
    atomic_var->value += value;
}


/*
 * Add a value to an atomic type and get the result.
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_add_and_get( pj_atomic_t *atomic_var,
			                         pj_atomic_value_t value)
{
    atomic_var->value += value;
    return atomic_var->value;
}



/////////////////////////////////////////////////////////////////////////////

PJ_DEF(pj_status_t) pj_mutex_create( pj_pool_t *pool, 
                                     const char *name,
				     int type, 
                                     pj_mutex_t **mutex)
{
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(name);
    PJ_UNUSED_ARG(type);

    *mutex = DUMMY_MUTEX;
    return PJ_SUCCESS;
}

/*
 * pj_mutex_create_simple()
 */
PJ_DEF(pj_status_t) pj_mutex_create_simple( pj_pool_t *pool, 
                                            const char *name,
					    pj_mutex_t **mutex )
{
    return pj_mutex_create(pool, name, PJ_MUTEX_SIMPLE, mutex);
}


PJ_DEF(pj_status_t) pj_mutex_create_recursive( pj_pool_t *pool,
					       const char *name,
					       pj_mutex_t **mutex )
{
    return pj_mutex_create(pool, name, PJ_MUTEX_RECURSE, mutex);
}


/*
 * pj_mutex_lock()
 */
PJ_DEF(pj_status_t) pj_mutex_lock(pj_mutex_t *mutex)
{
    pj_assert(mutex == DUMMY_MUTEX);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_trylock()
 */
PJ_DEF(pj_status_t) pj_mutex_trylock(pj_mutex_t *mutex)
{
    pj_assert(mutex == DUMMY_MUTEX);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_unlock()
 */
PJ_DEF(pj_status_t) pj_mutex_unlock(pj_mutex_t *mutex)
{
    pj_assert(mutex == DUMMY_MUTEX);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_destroy()
 */
PJ_DEF(pj_status_t) pj_mutex_destroy(pj_mutex_t *mutex)
{
    pj_assert(mutex == DUMMY_MUTEX);
    return PJ_SUCCESS;
}


/////////////////////////////////////////////////////////////////////////////
/*
 * RW Mutex
 */
#include "os_rwmutex.c"


/////////////////////////////////////////////////////////////////////////////

/*
 * Enter critical section.
 */
PJ_DEF(void) pj_enter_critical_section(void)
{
    /* Nothing to do */
}


/*
 * Leave critical section.
 */
PJ_DEF(void) pj_leave_critical_section(void)
{
    /* Nothing to do */
}


/////////////////////////////////////////////////////////////////////////////

/*
 * Create semaphore.
 */
PJ_DEF(pj_status_t) pj_sem_create( pj_pool_t *pool, 
                                   const char *name,
				   unsigned initial, 
                                   unsigned max,
				   pj_sem_t **sem)
{
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(name);
    PJ_UNUSED_ARG(initial);
    PJ_UNUSED_ARG(max);
    PJ_UNUSED_ARG(sem);

    /* Unsupported */
    return PJ_ENOTSUP;
}


/*
 * Wait for semaphore.
 */
PJ_DEF(pj_status_t) pj_sem_wait(pj_sem_t *sem)
{
    PJ_UNUSED_ARG(sem);
    return PJ_EINVALIDOP;
}


/*
 * Try wait for semaphore.
 */
PJ_DEF(pj_status_t) pj_sem_trywait(pj_sem_t *sem)
{
    PJ_UNUSED_ARG(sem);
    return PJ_EINVALIDOP;
}


/*
 * Release semaphore.
 */
PJ_DEF(pj_status_t) pj_sem_post(pj_sem_t *sem)
{
    PJ_UNUSED_ARG(sem);
    return PJ_EINVALIDOP;
}


/*
 * Destroy semaphore.
 */
PJ_DEF(pj_status_t) pj_sem_destroy(pj_sem_t *sem)
{
    PJ_UNUSED_ARG(sem);
    return PJ_EINVALIDOP;
}


