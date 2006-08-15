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
#pragma data_seg(".SYMBIAN")
__EMULATOR_IMAGE_HEADER2 (0x10000079,0x1000008d,0x1000425b,EPriorityForeground,0x00000000u,0x00000000u,0x00000000,0x70000001,0x00000000,0)
#pragma data_seg()


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

#if defined(PJ_HAS_SEMAPHORE) && PJ_HAS_SEMAPHORE != 0
#  include <semaphore.h>
#endif

#include <unistd.h>	    // getpid()
#include <errno.h>	    // errno


#define PJ_MAX_TLS  32


struct pj_thread_t
{
    char	    obj_name[PJ_MAX_OBJ_NAME];
    RThread	   *thread;
    pj_thread_proc *proc;
    void	   *arg;
    unsigned	    flags;
    void	   *tls_values[PJ_MAX_TLS];
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

  return PJ_SUCCESS;
}


/*
 * thread_main()
 *
 * This is the main entry for all threads.
 */
static TInt thread_main(TAny *param)
{
    pj_thread_t *rec = (pj_thread_t *) param;
    TInt result;

    /* Save thread record to Symbian TLS */
    Dll::SetTls(rec);

    /* Suspend thread if PJ_THREAD_SUSPENDED is specified */
    if (rec->flags & PJ_THREAD_SUSPENDED)
	rec->thread->Suspend();

    PJ_LOG(6,(rec->obj_name, "Thread started"));

    /* Call user's entry! */
    result = (TInt)(*rec->proc)(rec->arg);

    /* Done. */
    PJ_LOG(6,(rec->obj_name, "Thread quitting"));

    return result;
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
    pj_thread_t *rec;
    void *p;
    int rc;


    PJ_ASSERT_RETURN(pool && proc && ptr_thread, PJ_EINVAL);

    /* Create thread record and assign name for the thread */
    rec = (struct pj_thread_t*) pj_pool_zalloc(pool, sizeof(pj_thread_t));
    PJ_ASSERT_RETURN(rec, PJ_ENOMEM);
    
    /* Set name. */
    if (!thread_name) 
	thread_name = "thr%p";
    
    if (strchr(thread_name, '%')) {
	pj_ansi_snprintf(rec->obj_name, PJ_MAX_OBJ_NAME, thread_name, rec);
    } else {
	strncpy(rec->obj_name, thread_name, PJ_MAX_OBJ_NAME);
	rec->obj_name[PJ_MAX_OBJ_NAME-1] = '\0';
    }

    /* Create Symbian RThread object */
    p = pj_pool_alloc(pool, sizeof(RThread));
    rec->thread = new (p) RThread;

    /* Create the thread. */
    rec->proc = proc;
    rec->arg = arg;
    rec->flags = flags;
    _LIT( KThreadName, "Athread");
    rc = rec->thread->Create(KThreadName, &thread_main, stack_size, 
			     KMinHeapSize, KMinHeapSize, rec);
    if (rc != 0) {
	return PJ_RETURN_OS_ERROR(rc);
    }

    *ptr_thread = rec;

    PJ_LOG(6, (rec->obj_name, "Thread created"));
    return PJ_SUCCESS;
}

/*
 * pj_thread-get_name()
 */
PJ_DEF(const char*) pj_thread_get_name(pj_thread_t *p)
{
    return p->obj_name;
}

/*
 * pj_thread_resume()
 */
PJ_DEF(pj_status_t) pj_thread_resume(pj_thread_t *p)
{
    p->thread->Resume();
    return PJ_SUCCESS;
}

/*
 * pj_thread_this()
 */
PJ_DEF(pj_thread_t*) pj_thread_this(void)
{
    return (pj_thread_t*)Dll::Tls();
}

/*
 * pj_thread_join()
 */
PJ_DEF(pj_status_t) pj_thread_join(pj_thread_t *rec)
{
    TRequestStatus result;

    PJ_LOG(6, (rec->obj_name, "Joining thread %s", rec->obj_name));

    rec->thread->Rendezvous(result);

    return PJ_SUCCESS;
}

/*
 * pj_thread_destroy()
 */
PJ_DEF(pj_status_t) pj_thread_destroy(pj_thread_t *rec)
{
    rec->thread->Kill(1);
    return PJ_SUCCESS;
}

/*
 * pj_thread_sleep()
 */
PJ_DEF(pj_status_t) pj_thread_sleep(unsigned msec)
{
    PJ_TODO(MSEC_RESOLUTION_SLEEP);

    if (sleep(msec * 1000) == 0)
	return PJ_SUCCESS;
	
    return PJ_SUCCESS;
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


PJ_DEF(pj_status_t) pj_mutex_create( pj_pool_t *pool, 
                                     const char *name,
				     int type, 
                                     pj_mutex_t **mutex)
{
    PJ_TODO(pj_mutex_create);
    *mutex = (pj_mutex_t*)1;
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
    PJ_TODO(pj_mutex_lock);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_trylock()
 */
PJ_DEF(pj_status_t) pj_mutex_trylock(pj_mutex_t *mutex)
{
    PJ_TODO(pj_mutex_trylock);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_unlock()
 */
PJ_DEF(pj_status_t) pj_mutex_unlock(pj_mutex_t *mutex)
{
    PJ_TODO(pj_mutex_unlock);
    return PJ_SUCCESS;
}

/*
 * pj_mutex_destroy()
 */
PJ_DEF(pj_status_t) pj_mutex_destroy(pj_mutex_t *mutex)
{
    PJ_TODO(pj_mutex_destroy);
    return PJ_SUCCESS;
}

/////////////////////////////////////////////////////////////////////////////

/*
 * Enter critical section.
 */
PJ_DEF(void) pj_enter_critical_section(void)
{
    PJ_TODO(pj_enter_critical_section);
}


/*
 * Leave critical section.
 */
PJ_DEF(void) pj_leave_critical_section(void)
{
    PJ_TODO(pj_leave_critical_section);
}




