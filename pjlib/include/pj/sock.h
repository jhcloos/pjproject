/* $Id$
 *
 */

#ifndef __PJ_SOCK_H__
#define __PJ_SOCK_H__

/**
 * @file sock.h
 * @brief Socket Abstraction.
 */

#include <pj/types.h>

PJ_BEGIN_DECL 


/**
 * @defgroup PJ_SOCK Socket Abstraction
 * @ingroup PJ_IO
 * @{
 *
 * The PJLIB socket abstraction layer is a thin and very portable abstraction
 * for socket API. It provides API similar to BSD socket API. The abstraction
 * is needed because BSD socket API is not always available on all platforms,
 * therefore it wouldn't be possible to create a trully portable network
 * programs unless we provide such abstraction.
 *
 * Applications can use this API directly in their application, just
 * as they would when using traditional BSD socket API, provided they
 * call #pj_init() first.
 *
 * \section pj_sock_examples_sec Examples
 *
 * For some examples on how to use the socket API, please see:
 *
 *  - \ref page_pjlib_sock_test
 *  - \ref page_pjlib_select_test
 *  - \ref page_pjlib_sock_perf_test
 */


/**
 * Supported address families. 
 * APPLICATION MUST USE THESE VALUES INSTEAD OF NORMAL AF_*, BECAUSE
 * THE LIBRARY WILL DO TRANSLATION TO THE NATIVE VALUE.
 */
extern const pj_uint16_t PJ_AF_UNIX;    /**< Unix domain socket.	*/
#define PJ_AF_LOCAL	 PJ_AF_UNIX;    /**< POSIX name for AF_UNIX	*/
extern const pj_uint16_t PJ_AF_INET;    /**< Internet IP protocol.	*/
extern const pj_uint16_t PJ_AF_INET6;   /**< IP version 6.		*/
extern const pj_uint16_t PJ_AF_PACKET;  /**< Packet family.		*/
extern const pj_uint16_t PJ_AF_IRDA;    /**< IRDA sockets.		*/


/**
 * Supported types of sockets.
 * APPLICATION MUST USE THESE VALUES INSTEAD OF NORMAL SOCK_*, BECAUSE
 * THE LIBRARY WILL TRANSLATE THE VALUE TO THE NATIVE VALUE.
 */

extern const pj_uint16_t PJ_SOCK_STREAM; /**< Sequenced, reliable, connection-
					      based byte streams.           */
extern const pj_uint16_t PJ_SOCK_DGRAM;  /**< Connectionless, unreliable 
					      datagrams of fixed maximum 
					      lengths.                      */
extern const pj_uint16_t PJ_SOCK_RAW;    /**< Raw protocol interface.       */
extern const pj_uint16_t PJ_SOCK_RDM;    /**< Reliably-delivered messages.  */


/**
 * Socket level specified in #pj_sock_setsockopt().
 * APPLICATION MUST USE THESE VALUES INSTEAD OF NORMAL SOL_*, BECAUSE
 * THE LIBRARY WILL TRANSLATE THE VALUE TO THE NATIVE VALUE.
 */
extern const pj_uint16_t PJ_SOL_SOCKET;	/**< Socket level.  */
extern const pj_uint16_t PJ_SOL_IP;	/**< IP level.	    */
extern const pj_uint16_t PJ_SOL_TCP;	/**< TCP level.	    */
extern const pj_uint16_t PJ_SOL_UDP;	/**< UDP level.	    */
extern const pj_uint16_t PJ_SOL_IPV6;	/**< IP version 6   */

/**
 * Flags to be specified in #pj_sock_recv, #pj_sock_send, etc.
 */
typedef enum pj_sock_msg_flag
{
    PJ_MSG_OOB		= 0x01,	    /**< Out-of-band messages.		 */
    PJ_MSG_PEEK		= 0x02,	    /**< Peek, don't remove from buffer. */
    PJ_MSG_DONTROUTE	= 0x04,	    /**< Don't route.			 */
} pj_sock_msg_flag;


/**
 * Flag to be specified in #pj_sock_shutdown.
 */
typedef enum pj_socket_sd_type
{
    PJ_SD_RECEIVE   = 0,    /**< No more receive.	    */
    PJ_SHUT_RD	    = 0,    /**< Alias for SD_RECEIVE.	    */
    PJ_SD_SEND	    = 1,    /**< No more sending.	    */
    PJ_SHUT_WR	    = 1,    /**< Alias for SD_SEND.	    */
    PJ_SD_BOTH	    = 2,    /**< No more send and receive.  */
    PJ_SHUT_RDWR    = 2,    /**< Alias for SD_BOTH.	    */
} pj_socket_sd_type;



/** Address to accept any incoming messages. */
#define PJ_INADDR_ANY	    ((pj_uint32_t)0)

/** Address indicating an error return */
#define PJ_INADDR_NONE	    ((pj_uint32_t)0xffffffff)

/** Address to send to all hosts. */
#define PJ_INADDR_BROADCAST ((pj_uint32_t)0xffffffff)


/** 
 * Maximum length specifiable by #pj_sock_listen().
 * If the build system doesn't override this value, then the lowest 
 * denominator (five, in Win32 systems) will be used.
 */
#if !defined(PJ_SOMAXCONN)
#  define PJ_SOMAXCONN	5
#endif


/**
 * Constant for invalid socket returned by #pj_sock_socket() and
 * #pj_sock_accept().
 */
#define PJ_INVALID_SOCKET   (-1)

/**
 * Structure describing a generic socket address.
 */
typedef struct pj_sockaddr
{
    pj_uint16_t	sa_family;	/**< Common data: address family.   */
    char	sa_data[14];	/**< Address data.		    */
} pj_sockaddr;


/**
 * This structure describes Internet address.
 */
typedef struct pj_in_addr
{
    pj_uint32_t	s_addr;		/**< The 32bit IP address.	    */
} pj_in_addr;


/**
 * This structure describes Internet socket address.
 */
typedef struct pj_sockaddr_in
{
    pj_uint16_t	sin_family;	/**< Address family.		    */
    pj_uint16_t	sin_port;	/**< Transport layer port number.   */
    pj_in_addr	sin_addr;	/**< IP address.		    */
    char	sin_zero[8];	/**< Padding.			    */
} pj_sockaddr_in;


/**
 * This structure describes IPv6 address.
 */
typedef struct pj_in6_addr
{
    /** Union of address formats. */
    union {
	pj_uint8_t  u6_addr8[16];   /**< u6_addr8   */
	pj_uint16_t u6_addr16[8];   /**< u6_addr16  */
	pj_uint32_t u6_addr32[4];   /**< u6_addr32  */
    } in6_u;
/** Shortcut to access in6_u.u6_addr8. */
#define s6_addr                 in6_u.u6_addr8
/** Shortcut to access in6_u.u6_addr16. */
#define s6_addr16               in6_u.u6_addr16
/** Shortcut to access in6_u.u6_addr32. */
#define s6_addr32               in6_u.u6_addr32
} pj_in6_addr;

/** Initializer value for pj_in6_addr. */
#define PJ_IN6ADDR_ANY_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } }

/** Initializer value for pj_in6_addr. */
#define PJ_IN6ADDR_LOOPBACK_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }

/**
 * This structure describes IPv6 socket address.
 */
typedef struct pj_sockaddr_in6
{
    pj_uint16_t	sin6_family;	    /**< Address family		    */
    pj_uint16_t	sin6_port;	    /**< Transport layer port number. */
    pj_uint32_t	sin6_flowinfo;	    /**< IPv6 flow information	    */
    pj_in6_addr sin6_addr;	    /**< IPv6 address.		    */
    pj_uint32_t sin6_scope_id;	    /**< IPv6 scope-id		    */
} pj_sockaddr_in6;


/*****************************************************************************
 *
 * SOCKET ADDRESS MANIPULATION.
 *
 *****************************************************************************
 */

/**
 * Convert 16-bit value from network byte order to host byte order.
 *
 * @param netshort  16-bit network value.
 * @return	    16-bit host value.
 */
PJ_DECL(pj_uint16_t) pj_ntohs(pj_uint16_t netshort);

/**
 * Convert 16-bit value from host byte order to network byte order.
 *
 * @param hostshort 16-bit host value.
 * @return	    16-bit network value.
 */
PJ_DECL(pj_uint16_t) pj_htons(pj_uint16_t hostshort);

/**
 * Convert 32-bit value from network byte order to host byte order.
 *
 * @param netlong   32-bit network value.
 * @return	    32-bit host value.
 */
PJ_DECL(pj_uint32_t) pj_ntohl(pj_uint32_t netlong);

/**
 * Convert 32-bit value from host byte order to network byte order.
 *
 * @param hostlong  32-bit host value.
 * @return	    32-bit network value.
 */
PJ_DECL(pj_uint32_t) pj_htonl(pj_uint32_t hostlong);

/**
 * Convert an Internet host address given in network byte order
 * to string in standard numbers and dots notation.
 *
 * @param inaddr    The host address.
 * @return	    The string address.
 */
PJ_DECL(char*) pj_inet_ntoa(pj_in_addr inaddr);

/**
 * This function converts the Internet host address cp from the standard
 * numbers-and-dots notation into binary data and stores it in the structure
 * that inp points to. 
 *
 * @param cp	IP address in standard numbers-and-dots notation.
 * @param inp	Structure that holds the output of the conversion.
 *
 * @return	nonzero if the address is valid, zero if not.
 */
PJ_DECL(int) pj_inet_aton(const pj_str_t *cp, struct pj_in_addr *inp);

/**
 * Convert address string with numbers and dots to binary IP address.
 * 
 * @param cp	    The IP address in numbers and dots notation.
 * @return	    If success, the IP address is returned in network
 *		    byte order. If failed, PJ_INADDR_NONE will be
 *		    returned.
 * @remark
 * This is an obsolete interface to #pj_inet_aton(); it is obsolete
 * because -1 is a valid address (255.255.255.255), and #pj_inet_aton()
 * provides a cleaner way to indicate error return.
 */
PJ_DECL(pj_in_addr) pj_inet_addr(const pj_str_t *cp);


/**
 * Get the transport layer port number of an Internet socket address.
 * The port is returned in host byte order.
 *
 * @param addr	    The IP socket address.
 * @return	    Port number, in host byte order.
 */
PJ_INLINE(pj_uint16_t) pj_sockaddr_in_get_port(const pj_sockaddr_in *addr)
{
    return pj_ntohs(addr->sin_port);
}

/**
 * Set the port number of an Internet socket address.
 *
 * @param addr	    The IP socket address.
 * @param hostport  The port number, in host byte order.
 */
PJ_INLINE(void) pj_sockaddr_in_set_port(pj_sockaddr_in *addr, 
					pj_uint16_t hostport)
{
    addr->sin_port = pj_htons(hostport);
}

/**
 * Get the IP address of an Internet socket address.
 * The address is returned as 32bit value in host byte order.
 *
 * @param addr	    The IP socket address.
 * @return	    32bit address, in host byte order.
 */
PJ_INLINE(pj_in_addr) pj_sockaddr_in_get_addr(const pj_sockaddr_in *addr)
{
    pj_in_addr in_addr;
    in_addr.s_addr = pj_ntohl(addr->sin_addr.s_addr);
    return in_addr;
};

/**
 * Set the IP address of an Internet socket address.
 *
 * @param addr	    The IP socket address.
 * @param hostaddr  The host address, in host byte order.
 */
PJ_INLINE(void) pj_sockaddr_in_set_addr(pj_sockaddr_in *addr,
					pj_uint32_t hostaddr)
{
    addr->sin_addr.s_addr = pj_htonl(hostaddr);
}

/**
 * Set the IP address of an IP socket address from string address, 
 * with resolving the host if necessary. The string address may be in a
 * standard numbers and dots notation or may be a hostname. If hostname
 * is specified, then the function will resolve the host into the IP
 * address.
 *
 * @param addr	    The IP socket address to be set.
 * @param cp	    The address string, which can be in a standard 
 *		    dotted numbers or a hostname to be resolved.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sockaddr_in_set_str_addr( pj_sockaddr_in *addr,
					          const pj_str_t *cp);

/**
 * Set the IP address and port of an IP socket address.
 * The string address may be in a standard numbers and dots notation or 
 * may be a hostname. If hostname is specified, then the function will 
 * resolve the host into the IP address.
 *
 * @param addr	    The IP socket address to be set.
 * @param cp	    The address string, which can be in a standard 
 *		    dotted numbers or a hostname to be resolved.
 * @param port	    The port number, in host byte order.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sockaddr_in_init( pj_sockaddr_in *addr,
				          const pj_str_t *cp,
					  pj_uint16_t port);


/*****************************************************************************
 *
 * HOST NAME AND ADDRESS.
 *
 *****************************************************************************
 */

/**
 * Get system's host name.
 *
 * @return	    The hostname, or empty string if the hostname can not
 *		    be identified.
 */
PJ_DECL(const pj_str_t*) pj_gethostname(void);

/**
 * Get host's IP address, which the the first IP address that is resolved
 * from the hostname.
 *
 * @return	    The host's IP address, PJ_INADDR_NONE if the host
 *		    IP address can not be identified.
 */
PJ_DECL(pj_in_addr) pj_gethostaddr(void);


/*****************************************************************************
 *
 * SOCKET API.
 *
 *****************************************************************************
 */

/**
 * Create new socket/endpoint for communication.
 *
 * @param family    Specifies a communication domain; this selects the
 *		    protocol family which will be used for communication.
 * @param type	    The socket has the indicated type, which specifies the 
 *		    communication semantics.
 * @param protocol  Specifies  a  particular  protocol  to  be used with the
 *		    socket.  Normally only a single protocol exists to support 
 *		    a particular socket  type  within  a given protocol family, 
 *		    in which a case protocol can be specified as 0.
 * @param sock	    New socket descriptor, or PJ_INVALID_SOCKET on error.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_socket(int family, 
				    int type, 
				    int protocol,
				    pj_sock_t *sock);

/**
 * Close the socket descriptor.
 *
 * @param sockfd    The socket descriptor.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_close(pj_sock_t sockfd);


/**
 * This function gives the socket sockfd the local address my_addr. my_addr is
 * addrlen bytes long.  Traditionally, this is called assigning a name to
 * a socket. When a socket is created with #pj_sock_socket(), it exists in a
 * name space (address family) but has no name assigned.
 *
 * @param sockfd    The socket desriptor.
 * @param my_addr   The local address to bind the socket to.
 * @param addrlen   The length of the address.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_bind( pj_sock_t sockfd, 
				   const pj_sockaddr_t *my_addr,
				   int addrlen);

/**
 * Bind the IP socket sockfd to the given address and port.
 *
 * @param sockfd    The socket descriptor.
 * @param addr	    Local address to bind the socket to, in host byte order.
 * @param port	    The local port to bind the socket to, in host byte order.
 *
 * @return	    Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_bind_in( pj_sock_t sockfd, 
				      pj_uint32_t addr,
				      pj_uint16_t port);

#if PJ_HAS_TCP
/**
 * Listen for incoming connection. This function only applies to connection
 * oriented sockets (such as PJ_SOCK_STREAM or PJ_SOCK_SEQPACKET), and it
 * indicates the willingness to accept incoming connections.
 *
 * @param sockfd	The socket descriptor.
 * @param backlog	Defines the maximum length the queue of pending
 *			connections may grow to.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_listen( pj_sock_t sockfd, 
				     int backlog );

/**
 * Accept new connection on the specified connection oriented server socket.
 *
 * @param serverfd  The server socket.
 * @param newsock   New socket on success, of PJ_INVALID_SOCKET if failed.
 * @param addr	    A pointer to sockaddr type. If the argument is not NULL,
 *		    it will be filled by the address of connecting entity.
 * @param addrlen   Initially specifies the length of the address, and upon
 *		    return will be filled with the exact address length.
 *
 * @return	    Zero on success, or the error number.
 */
PJ_DECL(pj_status_t) pj_sock_accept( pj_sock_t serverfd,
				     pj_sock_t *newsock,
				     pj_sockaddr_t *addr,
				     int *addrlen);
#endif

/**
 * The file descriptor sockfd must refer to a socket.  If the socket is of
 * type PJ_SOCK_DGRAM  then the serv_addr address is the address to which
 * datagrams are sent by default, and the only address from which datagrams
 * are received. If the socket is of type PJ_SOCK_STREAM or PJ_SOCK_SEQPACKET,
 * this call attempts to make a connection to another socket.  The
 * other socket is specified by serv_addr, which is an address (of length
 * addrlen) in the communications space of the  socket.  Each  communications
 * space interprets the serv_addr parameter in its own way.
 *
 * @param sockfd	The socket descriptor.
 * @param serv_addr	Server address to connect to.
 * @param addrlen	The length of server address.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_connect( pj_sock_t sockfd,
				      const pj_sockaddr_t *serv_addr,
				      int addrlen);

/**
 * Return the address of peer which is connected to socket sockfd.
 *
 * @param sockfd	The socket descriptor.
 * @param addr		Pointer to sockaddr structure to which the address
 *			will be returned.
 * @param namelen	Initially the length of the addr. Upon return the value
 *			will be set to the actual length of the address.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_getpeername(pj_sock_t sockfd,
					  pj_sockaddr_t *addr,
					  int *namelen);

/**
 * Return the current name of the specified socket.
 *
 * @param sockfd	The socket descriptor.
 * @param addr		Pointer to sockaddr structure to which the address
 *			will be returned.
 * @param namelen	Initially the length of the addr. Upon return the value
 *			will be set to the actual length of the address.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_getsockname( pj_sock_t sockfd,
					  pj_sockaddr_t *addr,
					  int *namelen);

/**
 * Get socket option associated with a socket. Options may exist at multiple
 * protocol levels; they are always present at the uppermost socket level.
 *
 * @param sockfd	The socket descriptor.
 * @param level		The level which to get the option from.
 * @param optname	The option name, which will be passed uninterpreted
 *			by the library.
 * @param optval	Identifies the buffer which the value will be
 *			returned.
 * @param optlen	Initially contains the length of the buffer, upon
 *			return will be set to the actual size of the value.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_getsockopt( pj_sock_t sockfd,
					 int level,
					 int optname,
					 void *optval,
					 int *optlen);
/**
 * Manipulate the options associated with a socket. Options may exist at 
 * multiple protocol levels; they are always present at the uppermost socket 
 * level.
 *
 * @param sockfd	The socket descriptor.
 * @param level		The level which to get the option from.
 * @param optname	The option name, which will be passed uninterpreted
 *			by the library.
 * @param optval	Identifies the buffer which contain the value.
 * @param optlen	The length of the value.
 *
 * @return		PJ_SUCCESS or the status code.
 */
PJ_DECL(pj_status_t) pj_sock_setsockopt( pj_sock_t sockfd,
					 int level,
					 int optname,
					 const void *optval,
					 int optlen);


/**
 * Receives data stream or message coming to the specified socket.
 *
 * @param sockfd	The socket descriptor.
 * @param buf		The buffer to receive the data or message.
 * @param len		On input, the length of the buffer. On return,
 *			contains the length of data received.
 * @param flags		Combination of #pj_sock_msg_flag.
 *
 * @return		PJ_SUCCESS or the error code.
 */
PJ_DECL(pj_status_t) pj_sock_recv(pj_sock_t sockfd,
				  void *buf,
				  pj_ssize_t *len,
				  unsigned flags);

/**
 * Receives data stream or message coming to the specified socket.
 *
 * @param sockfd	The socket descriptor.
 * @param buf		The buffer to receive the data or message.
 * @param len		On input, the length of the buffer. On return,
 *			contains the length of data received.
 * @param flags		Bitmask combination of #pj_sock_msg_flag.
 * @param from		If not NULL, it will be filled with the source
 *			address of the connection.
 * @param fromlen	Initially contains the length of from address,
 *			and upon return will be filled with the actual
 *			length of the address.
 *
 * @return		PJ_SUCCESS or the error code.
 */
PJ_DECL(pj_status_t) pj_sock_recvfrom( pj_sock_t sockfd,
				      void *buf,
				      pj_ssize_t *len,
				      unsigned flags,
				      pj_sockaddr_t *from,
				      int *fromlen);

/**
 * Transmit data to the socket.
 *
 * @param sockfd	Socket descriptor.
 * @param buf		Buffer containing data to be sent.
 * @param len		On input, the length of the data in the buffer.
 *			Upon return, it will be filled with the length
 *			of data sent.
 * @param flags		Bitmask combination of #pj_sock_msg_flag.
 *
 * @return		PJ_SUCCESS or the status code.
 */
PJ_DECL(pj_status_t) pj_sock_send(pj_sock_t sockfd,
				  const void *buf,
				  pj_ssize_t *len,
				  unsigned flags);

/**
 * Transmit data to the socket to the specified address.
 *
 * @param sockfd	Socket descriptor.
 * @param buf		Buffer containing data to be sent.
 * @param len		On input, the length of the data in the buffer.
 *			Upon return, it will be filled with the length
 *			of data sent.
 * @param flags		Bitmask combination of #pj_sock_msg_flag.
 * @param to		The address to send.
 * @param tolen		The length of the address in bytes.
 *
 * @return		The length of data successfully sent.
 */
PJ_DECL(pj_status_t) pj_sock_sendto(pj_sock_t sockfd,
				    const void *buf,
				    pj_ssize_t *len,
				    unsigned flags,
				    const pj_sockaddr_t *to,
				    int tolen);

#if PJ_HAS_TCP
/**
 * The shutdown call causes all or part of a full-duplex connection on the
 * socket associated with sockfd to be shut down.
 *
 * @param sockfd	The socket descriptor.
 * @param how		If how is PJ_SHUT_RD, further receptions will be 
 *			disallowed. If how is PJ_SHUT_WR, further transmissions
 *			will be disallowed. If how is PJ_SHUT_RDWR, further 
 *			receptions andtransmissions will be disallowed.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pj_sock_shutdown( pj_sock_t sockfd,
				       int how);
#endif

/**
 * @}
 */


PJ_END_DECL

#endif	/* __PJ_SOCK_H__ */

