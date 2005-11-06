/* $Id */

/* ioqueue_common_abs.h
 *
 * This file contains private declarations for abstracting various 
 * event polling/dispatching mechanisms (e.g. select, poll, epoll) 
 * to the ioqueue. 
 */

#include <pj/list.h>

/*
 * The select ioqueue relies on socket functions (pj_sock_xxx()) to return
 * the correct error code.
 */
#if PJ_RETURN_OS_ERROR(100) != PJ_STATUS_FROM_OS(100)
#   error "Proper error reporting must be enabled for ioqueue to work!"
#endif


struct generic_operation
{
    PJ_DECL_LIST_MEMBER(struct generic_operation);
    pj_ioqueue_operation_e  op;
};

struct read_operation
{
    PJ_DECL_LIST_MEMBER(struct read_operation);
    pj_ioqueue_operation_e  op;

    void		   *buf;
    pj_size_t		    size;
    unsigned                flags;
    pj_sockaddr_t	   *rmt_addr;
    int			   *rmt_addrlen;
};

struct write_operation
{
    PJ_DECL_LIST_MEMBER(struct write_operation);
    pj_ioqueue_operation_e  op;

    char		   *buf;
    pj_size_t		    size;
    pj_ssize_t              written;
    unsigned                flags;
    pj_sockaddr_in	    rmt_addr;
    int			    rmt_addrlen;
};

#if PJ_HAS_TCP
struct accept_operation
{
    PJ_DECL_LIST_MEMBER(struct accept_operation);
    pj_ioqueue_operation_e  op;

    pj_sock_t              *accept_fd;
    pj_sockaddr_t	   *local_addr;
    pj_sockaddr_t	   *rmt_addr;
    int			   *addrlen;
};
#endif

union operation_key
{
    struct generic_operation generic;
    struct read_operation    read;
    struct write_operation   write;
#if PJ_HAS_TCP
    struct accept_operation  accept;
#endif
};

#define DECLARE_COMMON_KEY                          \
    PJ_DECL_LIST_MEMBER(struct pj_ioqueue_key_t);   \
    pj_ioqueue_t           *ioqueue;                \
    pj_mutex_t             *mutex;                  \
    pj_sock_t		    fd;                     \
    int                     fd_type;                \
    void		   *user_data;              \
    pj_ioqueue_callback	    cb;                     \
    int                     connecting;             \
    struct read_operation   read_list;              \
    struct write_operation  write_list;             \
    struct accept_operation accept_list;


#define DECLARE_COMMON_IOQUEUE                      \
    pj_lock_t          *lock;                       \
    pj_bool_t           auto_delete_lock;


enum ioqueue_event_type
{
    NO_EVENT,
    READABLE_EVENT,
    WRITEABLE_EVENT,
    EXCEPTION_EVENT,
};

static void ioqueue_add_to_set( pj_ioqueue_t *ioqueue,
                                pj_sock_t fd,
                                enum ioqueue_event_type event_type );
static void ioqueue_remove_from_set( pj_ioqueue_t *ioqueue,
                                     pj_sock_t fd, 
                                     enum ioqueue_event_type event_type);

