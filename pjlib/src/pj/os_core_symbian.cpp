/* $Id: os_core_unix.c 433 2006-05-10 19:24:40Z bennylp $ */
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


struct pj_thread_t
{
    char	obj_name[PJ_MAX_OBJ_NAME];
    RThread    thread;
    pj_thread_proc *proc;
    void	   *arg;
	
};

/*
    TODO: implement these stub methods!
*/

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
TInt thread_main(TAny *param)
{
    pj_thread_t *rec = (pj_thread_t *) param;
    TInt result;
    /* pj_status_t rc; */

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


    /* Create the thread. */
    rec->proc = proc;
    rec->arg = arg;
    _LIT( KThreadName, "Athread");
    rc = rec->thread.Create(KThreadName, thread_main, 4096, KMinHeapSize, 256*16, rec->arg, EOwnerThread);
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
    pj_thread_t *rec = (pj_thread_t*)p;

    return rec->obj_name;
}

/*
 * pj_thread_resume()
 */
PJ_DEF(pj_status_t) pj_thread_resume(pj_thread_t *p)
{
    pj_status_t rc;

    pj_thread_t *rec = (pj_thread_t*)p;

    rec->thread.Resume();

    rc = PJ_SUCCESS;

    return rc;
}

/*
 * pj_thread_this()
 */
PJ_DEF(pj_thread_t*) pj_thread_this(void)
{
    // TODO
    return NULL;
}

/*
 * pj_thread_join()
 */
PJ_DEF(pj_status_t) pj_thread_join(pj_thread_t *p)
{
    pj_thread_t *rec = (pj_thread_t *)p;
    TRequestStatus result;

    //PJ_LOG(6, (pj_thread_this()->obj_name, "Joining thread %s", p->obj_name));

    rec->thread.Rendezvous(result);

    return PJ_SUCCESS;
}

/*
 * pj_thread_destroy()
 */
PJ_DEF(pj_status_t) pj_thread_destroy(pj_thread_t *p)
{
    pj_thread_t *rec = (pj_thread_t *)p;
    rec->thread.Kill(1);
	

    return PJ_SUCCESS;
}

/*
 * pj_thread_sleep()
 */
PJ_DEF(pj_status_t) pj_thread_sleep(unsigned msec)
{
    if (sleep(msec * 1000) == 0)
	return PJ_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * pj_thread_local_alloc()
 */

PJ_DEF(pj_status_t) pj_thread_local_alloc(long *index)
{
    return PJ_SUCCESS;
}

/*
 * pj_thread_local_free()
 */
PJ_DEF(void) pj_thread_local_free(long index)
{
}

class foodata
{
};

/*
 * pj_thread_local_set()
 */
PJ_DEF(pj_status_t) pj_thread_local_set(long index, void *value)
{
    return PJ_SUCCESS;
}

/*
 * pj_thread_local_get()
 */
PJ_DEF(void*) pj_thread_local_get(long index)
{
    return NULL; //Dll::Tls();
}


/*
 * pj_mutex_create_simple()
 */
PJ_DEF(pj_status_t) pj_mutex_create_simple( pj_pool_t *pool, 
                                            const char *name,
					    pj_mutex_t **mutex )
{
    (*mutex) = (pj_mutex_t *)1;
    return PJ_SUCCESS;
}

/*
 * pj_mutex_lock()
 */
PJ_DEF(pj_status_t) pj_mutex_lock(pj_mutex_t *mutex)
{
    return PJ_SUCCESS;
}

/*
 * pj_mutex_trylock()
 */
PJ_DEF(pj_status_t) pj_mutex_trylock(pj_mutex_t *mutex)
{
    return PJ_SUCCESS;
}

/*
 * pj_mutex_unlock()
 */
PJ_DEF(pj_status_t) pj_mutex_unlock(pj_mutex_t *mutex)
{
    return PJ_SUCCESS;
}

/*
 * pj_mutex_destroy()
 */
PJ_DEF(pj_status_t) pj_mutex_destroy(pj_mutex_t *mutex)
{
    return PJ_SUCCESS;
}