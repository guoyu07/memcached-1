#ifndef MEMCACHED_TYPES_H
#define MEMCACHED_TYPES_H 1

#include <sys/types.h>
#include <stdint.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef WIN32
/*
** From https://msdn.microsoft.com/en-us/library/z8y1yy88.aspx:
**
** The inline keyword is available only in C++. The __inline and
** __forceinline keywords are available in both C and C++.
*/
#define CB_INLINE __inline
#include <platform/platform.h>
#else
#define CB_INLINE inline
#include <sys/uio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Time relative to server start. Smaller than time_t on 64-bit systems.
     */
    typedef uint32_t rel_time_t;

    /**
     * Response codes for engine operations.
     */
    typedef enum {
        ENGINE_SUCCESS = 0x00, /**< The command executed successfully */
        ENGINE_KEY_ENOENT = 0x01, /**< The key does not exists */
        ENGINE_KEY_EEXISTS = 0x02, /**< The key already exists */
        ENGINE_ENOMEM = 0x03, /**< Could not allocate memory */
        ENGINE_NOT_STORED = 0x04, /**< The item was not stored */
        ENGINE_EINVAL = 0x05, /**< Invalid arguments */
        ENGINE_ENOTSUP = 0x06, /**< The engine does not support this */
        ENGINE_EWOULDBLOCK = 0x07, /**< This would cause the engine to block */
        ENGINE_E2BIG = 0x08, /**< The data is too big for the engine */
        ENGINE_WANT_MORE = 0x09, /**< The engine want more data if the frontend
                                  * have more data available. */
        ENGINE_DISCONNECT = 0x0a, /**< Tell the server to disconnect this
                                   * client */
        ENGINE_EACCESS = 0x0b, /**< Access control violations */
        ENGINE_NOT_MY_VBUCKET = 0x0c, /** < This vbucket doesn't belong to me */
        ENGINE_TMPFAIL = 0x0d, /**< Temporary failure, please try again later */
        ENGINE_ERANGE = 0x0e, /**< Value outside legal range */
        ENGINE_ROLLBACK = 0x0f, /**< Roll back to a previous version */
        ENGINE_NO_BUCKET = 0x10, /**< The connection isn't bound to an engine */
        ENGINE_EBUSY = 0x11, /**< Can't serve the request.. busy */
        ENGINE_AUTH_STALE = 0x12, /**< Auth data stale */
        ENGINE_FAILED = 0xff  /**< Generic failue. */
    } ENGINE_ERROR_CODE;

    /**
     * Engine storage operations.
     */
    typedef enum {
        OPERATION_ADD = 1, /**< Store with add semantics */
        OPERATION_SET, /**< Store with set semantics */
        OPERATION_REPLACE, /**< Store with replace semantics */
        OPERATION_APPEND, /**< Store with append semantics */
        OPERATION_PREPEND, /**< Store with prepend semantics */
        OPERATION_CAS /**< Store with set semantics. */
    } ENGINE_STORE_OPERATION;

    typedef enum {
        CONN_PRIORITY_HIGH,
        CONN_PRIORITY_MED,
        CONN_PRIORITY_LOW
    } CONN_PRIORITY;

    /**
     * Data common to any item stored in memcached.
     */
    typedef void item;

    typedef struct {
        uint64_t cas;
        uint64_t vbucket_uuid;
        uint64_t seqno;
        rel_time_t exptime; /**< When the item will expire (relative to process
                             * startup) */
        uint32_t nbytes; /**< The total size of the data (in bytes) */
        uint32_t flags; /**< Flags associated with the item (in network byte order)*/
        uint8_t datatype;
        uint8_t clsid; /** class id for the object */
        uint16_t nkey; /**< The total length of the key (in bytes) */
        uint16_t nvalue; /** < IN: The number of elements available in value
                          * OUT: the number of elements used in value */
        const void *key;
        struct iovec value[1];
    } item_info;

    typedef struct {
        const char *username;
    } auth_data_t;

    /* Forward declaration of the server handle -- to be filled in later */
    typedef struct server_handle_v1_t SERVER_HANDLE_V1;

    /* Information to uniquely identify (and order) a mutation. */
    typedef struct {
        uint64_t vbucket_uuid; /** vBucket UUID for this mutation. */
        uint64_t seqno; /** sequence number of the mutation. */
    } mutation_descr_t;

    /* Value used to distinguish one bucket from another */
    typedef uint32_t bucket_id_t;

#ifdef __cplusplus
}
#endif

#endif /* MEMCACHED_TYPES_H */
