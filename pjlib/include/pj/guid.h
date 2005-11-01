/* $header: $ */

#ifndef __PJ_GUID_H__
#define __PJ_GUID_H__


/**
 * @file guid.h
 * @brief GUID Globally Unique Identifier.
 */
#include <pj/types.h>


PJ_BEGIN_DECL


/**
 * @defgroup PJ_DS Data Structure.
 * @ingroup PJ
 */
/**
 * @defgroup PJ_GUID Globally Unique Identifier
 * @ingroup PJ_DS
 * @{
 *
 * This module provides API to create string that is globally unique.
 * If application doesn't require that strong requirement, it can just
 * use #pj_create_random_string() instead.
 */


/**
 * PJ_GUID_STRING_LENGTH specifies length of GUID string. The value is
 * dependent on the algorithm used internally to generate the GUID string.
 * If real GUID generator is used, then the length will be 128bit or 
 * 32 bytes. If shadow GUID generator is used, then the length
 * will be 20 bytes. Application should not assume which algorithm will
 * be used by GUID generator.
 */
extern const unsigned PJ_GUID_STRING_LENGTH;

/**
 * PJ_GUID_MAX_LENGTH specifies the maximum length of GUID string,
 * regardless of which algorithm to use.
 */
#define PJ_GUID_MAX_LENGTH  32

/**
 * Create a globally unique string, which length is PJ_GUID_STRING_LENGTH
 * characters. Caller is responsible for preallocating the storage used
 * in the string.
 *
 * @param str       The string to store the result.
 *
 * @return          The string.
 */
PJ_DECL(pj_str_t*) pj_generate_unique_string(pj_str_t *str);

/**
 * Generate a unique string.
 *
 * @param pool	    Pool to allocate memory from.
 * @param str	    The string.
 */
PJ_DECL(void) pj_create_unique_string(pj_pool_t *pool, pj_str_t *str);


/**
 * @}
 */

PJ_END_DECL

#endif/* __PJ_GUID_H__ */

