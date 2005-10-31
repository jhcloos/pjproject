/* $Header: /pjproject-0.3/pjlib/include/pj/lock.h 2     10/14/05 12:26a Bennylp $ */
#ifndef __PJ_LOCK_H__
#define __PJ_LOCK_H__

/**
 * @file lock.h
 * @brief Higher abstraction for locking objects.
 */
#include <pj/types.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJ_LOCK Lock Objects
 * @ingroup PJ_OS
 * @{
 *
 * <b>Lock Objects</b> are higher abstraction for different lock mechanisms.
 * It offers the same API for manipulating different lock types (e.g.
 * @ref PJ_MUTEX "mutex", @ref PJ_SEM "semaphores", or null locks).
 * Because Lock Objects have the same API for different types of lock
 * implementation, it can be passed around in function arguments. As the
 * result, it can be used to control locking policy for  a particular
 * feature.
 */


/**
 * Create simple, non recursive mutex lock object.
 *
 * @param pool	    Memory pool.
 * @param name	    Lock object's name.
 * @param lock	    Pointer to store the returned handle.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_create_simple_mutex( pj_pool_t *pool,
						  const char *name,
						  pj_lock_t **lock );

/**
 * Create recursive mutex lock object.
 *
 * @param pool	    Memory pool.
 * @param name	    Lock object's name.
 * @param lock	    Pointer to store the returned handle.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_create_recursive_mutex( pj_pool_t *pool,
						     const char *name,
						     pj_lock_t **lock );


/**
 * Create NULL mutex. A NULL mutex doesn't actually have any synchronization
 * object attached to it.
 *
 * @param pool	    Memory pool.
 * @param name	    Lock object's name.
 * @param lock	    Pointer to store the returned handle.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_create_null_mutex( pj_pool_t *pool,
						const char *name,
						pj_lock_t **lock );


#if defined(PJ_HAS_SEMAPHORE) && PJ_HAS_SEMAPHORE != 0
/**
 * Create semaphore lock object.
 *
 * @param pool	    Memory pool.
 * @param name	    Lock object's name.
 * @param initial   Initial value of the semaphore.
 * @param max	    Maximum value of the semaphore.
 * @param lock	    Pointer to store the returned handle.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_create_semaphore( pj_pool_t *pool,
					       const char *name,
					       unsigned initial,
					       unsigned max,
					       pj_lock_t **lock );

#endif	/* PJ_HAS_SEMAPHORE */

/**
 * Acquire lock on the specified lock object.
 *
 * @param lock	    The lock object.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_acquire( pj_lock_t *lock );


/**
 * Try to acquire lock on the specified lock object.
 *
 * @param lock	    The lock object.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_tryacquire( pj_lock_t *lock );


/**
 * Release lock on the specified lock object.
 *
 * @param lock	    The lock object.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_release( pj_lock_t *lock );


/**
 * Destroy the lock object.
 *
 * @param lock	    The lock object.
 *
 * @return	    PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_lock_destroy( pj_lock_t *lock );


/** @} */

PJ_END_DECL


#endif	/* __PJ_LOCK_H__ */

