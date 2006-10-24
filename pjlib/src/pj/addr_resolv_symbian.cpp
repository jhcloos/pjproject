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
#include <pj/addr_resolv.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/unicode.h>

#include "os_symbian.h"
 

// PJLIB API: resolve hostname
PJ_DEF(pj_status_t) pj_gethostbyname(const pj_str_t *name, pj_hostent *he)
{
    PJ_ASSERT_RETURN(name && he, PJ_EINVAL);

    RHostResolver &resv = PjSymbianOS::Instance()->GetResolver();

    // Convert name to Unicode
    wchar_t name16[PJ_MAX_HOSTNAME];
    pj_ansi_to_unicode(name->ptr, name->slen, name16, PJ_ARRAY_SIZE(name16));
    TPtrC16 data(name16);

    // Resolve!
    TNameEntry nameEntry;
    TInt rc = resv.GetByName(data, nameEntry);

    if (rc != KErrNone)
	return PJ_RETURN_OS_ERROR(rc);

    // Get the resolved TInetAddr
    const TNameRecord &rec = (const TNameRecord&) nameEntry;
    TInetAddr inetAddr(rec.iAddr);

    //
    // This where we keep static variables.
    // These should be kept in TLS probably, to allow multiple threads
    // to call pj_gethostbyname() without interfering each other.
    // But again, we don't support threads in Symbian!
    //
    static char resolved_name[PJ_MAX_HOSTNAME];
    static char *no_aliases[1];
    static pj_in_addr *addr_list[2];
    static pj_sockaddr_in resolved_addr;

    // Convert the official address to ANSI.
    pj_unicode_to_ansi(rec.iName.Ptr(), rec.iName.Length(),
		       resolved_name, sizeof(resolved_name));

    // Convert IP address
    
    PjSymbianOS::Addr2pj(inetAddr, resolved_addr);
    addr_list[0] = &resolved_addr.sin_addr;

    // Return hostent
    he->h_name = resolved_name;
    he->h_aliases = no_aliases;
    he->h_addrtype = PJ_AF_INET;
    he->h_length = 4;
    he->h_addr_list = (char**) addr_list;

    return PJ_SUCCESS;
}

