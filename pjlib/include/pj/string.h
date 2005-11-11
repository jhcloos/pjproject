/* $Id$
 *
 */

#ifndef __PJ_STRING_H__
#define __PJ_STRING_H__

/**
 * @file string.h
 * @brief PJLIB String Operations.
 */

#include <pj/types.h>
#include <pj/compat/string.h>
#include <pj/compat/sprintf.h>
#include <pj/compat/vsprintf.h>


PJ_BEGIN_DECL

/**
 * @defgroup PJ_PSTR String Operations
 * @ingroup PJ_DS
 * @{
 * This module provides string manipulation API.
 *
 * \section pj_pstr_not_null_sec PJLIB String is NOT Null Terminated!
 *
 * That is the first information that developers need to know. Instead
 * of using normal C string, strings in PJLIB are represented as
 * pj_str_t structure below:
 *
 * <pre>
 *   typedef struct pj_str_t
 *   {
 *       char      *ptr;
 *       pj_size_t  slen;
 *   } pj_str_t;
 * </pre>
 *
 * There are some advantages of using this approach:
 *  - the string can point to arbitrary location in memory even
 *    if the string in that location is not null terminated. This is
 *    most usefull for text parsing, where the parsed text can just
 *    point to the original text in the input. If we use C string,
 *    then we will have to copy the text portion from the input
 *    to a string variable.
 *  - because the length of the string is known, string copy operation
 *    can be made more efficient.
 *
 * Most of APIs in PJLIB that expect or return string will represent
 * the string as pj_str_t instead of normal C string.
 *
 * \section pj_pstr_examples_sec Examples
 *
 * For some examples, please see:
 *  - @ref page_pjlib_string_test
 */

/**
 * Create string initializer from a normal C string.
 *
 * @param str	Null terminated string to be stored.
 *
 * @return	pj_str_t.
 */
PJ_IDECL(pj_str_t) pj_str(char *str);

/**
 * Create constant string from normal C string.
 *
 * @param str	The string to be initialized.
 * @param s	Null terminated string.
 *
 * @return	pj_str_t.
 */
PJ_INLINE(const pj_str_t*) pj_cstr(pj_str_t *str, const char *s)
{
    str->ptr = (char*)s;
    str->slen = s ? strlen(s) : 0;
    return str;
}

/**
 * Set the pointer and length to the specified value.
 *
 * @param str	    the string.
 * @param ptr	    pointer to set.
 * @param length    length to set.
 *
 * @return the string.
 */
PJ_INLINE(pj_str_t*) pj_strset( pj_str_t *str, char *ptr, pj_size_t length)
{
    str->ptr = ptr;
    str->slen = length;
    return str;
}

/**
 * Set the pointer and length of the string to the source string, which
 * must be NULL terminated.
 *
 * @param str	    the string.
 * @param src	    pointer to set.
 *
 * @return the string.
 */
PJ_INLINE(pj_str_t*) pj_strset2( pj_str_t *str, char *src)
{
    str->ptr = src;
    str->slen = src ? strlen(src) : 0;
    return str;
}

/**
 * Set the pointer and the length of the string.
 *
 * @param str	    The target string.
 * @param begin	    The start of the string.
 * @param end	    The end of the string.
 *
 * @return the target string.
 */
PJ_INLINE(pj_str_t*) pj_strset3( pj_str_t *str, char *begin, char *end )
{
    str->ptr = begin;
    str->slen = end-begin;
    return str;
}

/**
 * Assign string.
 *
 * @param dst	    The target string.
 * @param src	    The source string.
 *
 * @return the target string.
 */
PJ_IDECL(pj_str_t*) pj_strassign( pj_str_t *dst, pj_str_t *src );

/**
 * Copy string contents.
 *
 * @param dst	    The target string.
 * @param src	    The source string.
 *
 * @return the target string.
 */
PJ_IDECL(pj_str_t*) pj_strcpy(pj_str_t *dst, const pj_str_t *src);

/**
 * Copy string contents.
 *
 * @param dst	    The target string.
 * @param src	    The source string.
 *
 * @return the target string.
 */
PJ_IDECL(pj_str_t*) pj_strcpy2(pj_str_t *dst, const char *src);

/**
 * Duplicate string.
 *
 * @param pool	    The pool.
 * @param dst	    The string result.
 * @param src	    The string to duplicate.
 *
 * @return the string result.
 */
PJ_IDECL(pj_str_t*) pj_strdup(pj_pool_t *pool,
			      pj_str_t *dst,
			      const pj_str_t *src);

/**
 * Duplicate string and NULL terminate the destination string.
 *
 * @param pool
 * @param dst
 * @param src
 */
PJ_IDECL(pj_str_t*) pj_strdup_with_null(pj_pool_t *pool,
					pj_str_t *dst,
					const pj_str_t *src);

/**
 * Duplicate string.
 *
 * @param pool	    The pool.
 * @param dst	    The string result.
 * @param src	    The string to duplicate.
 *
 * @return the string result.
 */
PJ_IDECL(pj_str_t*) pj_strdup2(pj_pool_t *pool,
			       pj_str_t *dst,
			       const char *src);

/**
 * Duplicate string.
 *
 * @param pool	    The pool.
 * @param src	    The string to duplicate.
 *
 * @return the string result.
 */
PJ_IDECL(pj_str_t) pj_strdup3(pj_pool_t *pool, const char *src);

/**
 * Return the length of the string.
 *
 * @param str	    The string.
 *
 * @return the length of the string.
 */
PJ_INLINE(pj_size_t) pj_strlen( const pj_str_t *str )
{
    return str->slen;
}

/**
 * Return the pointer to the string data.
 *
 * @param str	    The string.
 *
 * @return the pointer to the string buffer.
 */
PJ_INLINE(const char*) pj_strbuf( const pj_str_t *str )
{
    return str->ptr;
}

/**
 * Compare strings. 
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strcmp( const pj_str_t *str1, const pj_str_t *str2);

/**
 * Compare strings.
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strcmp2( const pj_str_t *str1, const char *str2 );

/**
 * Compare strings. 
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 * @param len	    The maximum number of characters to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strncmp( const pj_str_t *str1, const pj_str_t *str2, 
			  pj_size_t len);

/**
 * Compare strings. 
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 * @param len	    The maximum number of characters to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strncmp2( const pj_str_t *str1, const char *str2, 
			   pj_size_t len);

/**
 * Perform lowercase comparison to the strings.
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_stricmp( const pj_str_t *str1, const pj_str_t *str2);

/**
 * Perform lowercase comparison to the strings.
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_stricmp2( const pj_str_t *str1, const char *str2);

/**
 * Perform lowercase comparison to the strings.
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 * @param len	    The maximum number of characters to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strnicmp( const pj_str_t *str1, const pj_str_t *str2, 
			   pj_size_t len);

/**
 * Perform lowercase comparison to the strings.
 *
 * @param str1	    The string to compare.
 * @param str2	    The string to compare.
 * @param len	    The maximum number of characters to compare.
 *
 * @return 
 *	- < 0 if str1 is less than str2
 *      - 0   if str1 is identical to str2
 *      - > 0 if str1 is greater than str2
 */
PJ_IDECL(int) pj_strnicmp2( const pj_str_t *str1, const char *str2, 
			    pj_size_t len);

/**
 * Concatenate strings.
 *
 * @param dst	    The destination string.
 * @param src	    The source string.
 */
PJ_IDECL(void) pj_strcat(pj_str_t *dst, const pj_str_t *src);

/**
 * Finds a character in a string.
 *
 * @param str	    The string.
 * @param chr	    The character to find.
 *
 * @return the pointer to first character found, or NULL.
 */
PJ_INLINE(char*) pj_strchr( pj_str_t *str, int chr)
{
    return (char*) memchr(str->ptr, chr, str->slen);
}

/**
 * Remove (trim) leading whitespaces from the string.
 *
 * @param str	    The string.
 *
 * @return the string.
 */
PJ_DECL(pj_str_t*) pj_strltrim( pj_str_t *str );

/**
 * Remove (trim) the trailing whitespaces from the string.
 *
 * @param str	    The string.
 *
 * @return the string.
 */
PJ_DECL(pj_str_t*) pj_strrtrim( pj_str_t *str );

/**
 * Remove (trim) leading and trailing whitespaces from the string.
 *
 * @param str	    The string.
 *
 * @return the string.
 */
PJ_IDECL(pj_str_t*) pj_strtrim( pj_str_t *str );

/**
 * Initialize the buffer with some random string.
 *
 * @param str	    the string to store the result.
 * @param length    the length of the random string to generate.
 *
 * @return the string.
 */
PJ_DECL(char*) pj_create_random_string(char *str, pj_size_t length);

/**
 * Convert string to unsigned integer.
 *
 * @param str	the string.
 *
 * @return the unsigned integer.
 */
PJ_DECL(unsigned long) pj_strtoul(const pj_str_t *str);

/**
 * Utility to convert unsigned integer to string. Note that the
 * string will be NULL terminated.
 *
 * @param val	    the unsigned integer value.
 * @param buf	    the buffer
 *
 * @return the number of characters written
 */
PJ_DECL(int) pj_utoa(unsigned long val, char *buf);

/**
 * Convert unsigned integer to string with minimum digits. Note that the
 * string will be NULL terminated.
 *
 * @param val	    The unsigned integer value.
 * @param buf	    The buffer.
 * @param min_dig   Minimum digits to be printed, or zero to specify no
 *		    minimum digit.
 * @param pad	    The padding character to be put in front of the string
 *		    when the digits is less than minimum.
 *
 * @return the number of characters written.
 */
PJ_DECL(int) pj_utoa_pad( unsigned long val, char *buf, int min_dig, int pad);

/**
 * Fill the memory location with value.
 *
 * @param dst	    The destination buffer.
 * @param c	    Character to set.
 * @param size	    The number of characters.
 *
 * @return the value of dst.
 */
PJ_INLINE(void*) pj_memset(void *dst, int c, pj_size_t size)
{
    return memset(dst, c, size);
}

/**
 * Copy buffer.
 *
 * @param dst	    The destination buffer.
 * @param src	    The source buffer.
 * @param size	    The size to copy.
 *
 * @return the destination buffer.
 */
PJ_INLINE(void*) pj_memcpy(void *dst, const void *src, pj_size_t size)
{
    return memcpy(dst, src, size);
}

/**
 * Move memory.
 *
 * @param dst	    The destination buffer.
 * @param src	    The source buffer.
 * @param size	    The size to copy.
 *
 * @return the destination buffer.
 */
PJ_INLINE(void*) pj_memmove(void *dst, const void *src, pj_size_t size)
{
    return memmove(dst, src, size);
}

/**
 * Compare buffers.
 *
 * @param buf1	    The first buffer.
 * @param buf2	    The second buffer.
 * @param size	    The size to compare.
 *
 * @return negative, zero, or positive value.
 */
PJ_INLINE(int) pj_memcmp(const void *buf1, const void *buf2, pj_size_t size)
{
    return memcmp(buf1, buf2, size);
}

/**
 * Find character in the buffer.
 *
 * @param buf	    The buffer.
 * @param c	    The character to find.
 * @param size	    The size to check.
 *
 * @return the pointer to location where the character is found, or NULL if
 *         not found.
 */
PJ_INLINE(void*) pj_memchr(const void *buf, int c, pj_size_t size)
{
    return memchr(buf, c, size);
}

/**
 * @}
 */

#if PJ_FUNCTIONS_ARE_INLINED
#  include <pj/string_i.h>
#endif

PJ_END_DECL

#endif	/* __PJ_STRING_H__ */

