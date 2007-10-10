/* $Id$ */
/* 
 * Copyright (C) 2003-2007 Benny Prijono <benny@prijono.org>
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
#include <pjlib-util/base64.h>
#include <pj/assert.h>
#include <pj/errno.h>

#define INV	    -1
#define PADDING	    '='

const char base64_char[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '+', '/' 
};

static int base256_char(char c)
{
    if (c >= 'A' && c <= 'Z')
	return (c - 'A');
    else if (c >= 'a' && c <= 'z')
	return (c - 'a' + 26);
    else if (c >= '0' && c <= '9')
	return (c - '0' + 52);
    else if (c == '+')
	return (62);
    else if (c == '/')
	return (63);
    else {
	pj_assert(!"Should not happen as '=' should have been filtered");
	return INV;
    }
}


static void base256to64(pj_uint8_t c1, pj_uint8_t c2, pj_uint8_t c3, 
			int padding, char *output)
{
    *output++ = base64_char[c1>>2];
    *output++ = base64_char[((c1 & 0x3)<< 4) | ((c2 & 0xF0) >> 4)];
    switch (padding) {
    case 0:
	*output++ = base64_char[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
	*output = base64_char[c3 & 0x3F];
	break;
    case 1:
	*output++ = base64_char[((c2 & 0xF) << 2) | ((c3 & 0xC0) >>6)];
	*output = PADDING;
	break;
    case 2:
    default:
	*output++ = PADDING;
	*output = PADDING;
	break;
    }
}


PJ_DEF(pj_status_t) pj_base64_encode(const pj_uint8_t *input, int in_len,
				     char *output, int *out_len)
{
    const pj_uint8_t *pi = input;
    pj_uint8_t c1, c2, c3;
    int i = 0;
    char *po = output;

    PJ_ASSERT_RETURN(input && output && out_len, PJ_EINVAL);
    PJ_ASSERT_RETURN(*out_len >= PJ_BASE256_TO_BASE64_LEN(in_len), 
		     PJ_ETOOSMALL);

    while (i < in_len) {
	c1 = *pi++;
	++i;

	if (i == in_len) {
	    base256to64(c1, 0, 0, 2, po);
	    po += 4;
	    break;
	} else {
	    c2 = *pi++;
	    ++i;

	    if (i == in_len) {
		base256to64(c1, c2, 0, 1, po);
		po += 4;
		break;
	    } else {
		c3 = *pi++;
		++i;
		base256to64(c1, c2, c3, 0, po);
	    }
	}

	po += 4;
    }

    *out_len = po - output;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_base64_decode(const pj_str_t *input, 
				     pj_uint8_t *out, int *out_len)
{
    const char *buf = input->ptr;
    int len = input->slen;
    int i, j;
    int c1, c2, c3, c4;

    PJ_ASSERT_RETURN(input && out && out_len, PJ_EINVAL);

    while (buf[len-1] == '=' && len)
	--len;

    PJ_ASSERT_RETURN(*out_len >= PJ_BASE64_TO_BASE256_LEN(len), 
		     PJ_ETOOSMALL);

    for (i=0, j=0; i+3 < len; i+=4) {
	c1 = base256_char(buf[i]);
	c2 = base256_char(buf[i+1]);
	c3 = base256_char(buf[i+2]);
	c4 = base256_char(buf[i+3]);

	out[j++] = (pj_uint8_t)((c1<<2) | ((c2 & 0x30)>>4));
	out[j++] = (pj_uint8_t)(((c2 & 0x0F)<<4) | ((c3 & 0x3C)>>2));
	out[j++] = (pj_uint8_t)(((c3 & 0x03)<<6) | (c4 & 0x3F));
    }

    if (i < len) {
	c1 = base256_char(buf[i]);

	if (i+1 < len)
	    c2 = base256_char(buf[i+1]);
	else 
	    c2 = (INV);

	if (i+2 < len)
	    c3 = base256_char(buf[i+2]);
	else
	    c3 = (INV);

	c4 = (INV);

	if (c2 != INV) {
	    out[j++] = (pj_uint8_t)((c1<<2) | ((c2 & 0x30)>>4));
	    if (c3 != INV) {
		out[j++] = (pj_uint8_t)(((c2 & 0x0F)<<4) | ((c3 & 0x3C)>>2));
		if (c4 != INV) {
		    out[j++] = (pj_uint8_t)(((c3 & 0x03)<<6) | (c4 & 0x3F));
		}
	    }
	}
	
    }

    pj_assert(j < *out_len);
    *out_len = j;

    return PJ_SUCCESS;
}


