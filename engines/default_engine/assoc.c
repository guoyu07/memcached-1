/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Hash table
 *
 */
#include "config.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <platform/platform.h>

#include "default_engine_internal.h"

#define hashsize(n) ((size_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)

ENGINE_ERROR_CODE assoc_init(struct default_engine *engine) {
    engine->assoc.primary_hashtable = calloc(hashsize(engine->assoc.hashpower),
                                             sizeof(hash_item*));
    return (engine->assoc.primary_hashtable != NULL) ? ENGINE_SUCCESS : ENGINE_ENOMEM;
}

void assoc_destroy(struct default_engine *engine) {
    while (engine->assoc.expanding) {
#ifdef WIN32
        Sleep(1);
#else
        usleep(250);
#endif
    }
    free(engine->assoc.primary_hashtable);
}

hash_item *assoc_find(struct default_engine *engine, uint32_t hash, const char *key, const size_t nkey) {
    hash_item *it;
    unsigned int oldbucket;
    hash_item *ret = NULL;
    int depth = 0;

    if (engine->assoc.expanding &&
        (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        it = engine->assoc.old_hashtable[oldbucket];
    } else {
        it = engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
    }

    while (it) {
        if ((nkey == it->nkey) && (memcmp(key, item_get_key(it), nkey) == 0)) {
            ret = it;
            break;
        }
        it = it->h_next;
        ++depth;
    }
    MEMCACHED_ASSOC_FIND(key, nkey, depth);
    return ret;
}

/* returns the address of the item pointer before the key.  if *item == 0,
   the item wasn't found */

static hash_item** _hashitem_before(struct default_engine *engine,
                                    uint32_t hash,
                                    const char *key,
                                    const size_t nkey) {
    hash_item **pos;
    unsigned int oldbucket;

    if (engine->assoc.expanding &&
        (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        pos = &engine->assoc.old_hashtable[oldbucket];
    } else {
        pos = &engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
    }

    while (*pos && ((nkey != (*pos)->nkey) || memcmp(key, item_get_key(*pos), nkey))) {
        pos = &(*pos)->h_next;
    }
    return pos;
}

static void assoc_maintenance_thread(void *arg);

/* grows the hashtable to the next power of 2. */
static void assoc_expand(struct default_engine *engine) {
    engine->assoc.old_hashtable = engine->assoc.primary_hashtable;

    engine->assoc.primary_hashtable = calloc(hashsize(engine->assoc.hashpower + 1),
                                             sizeof(hash_item *));
    if (engine->assoc.primary_hashtable) {
        int ret = 0;
        cb_thread_t tid;

        engine->assoc.hashpower++;
        engine->assoc.expanding = true;
        engine->assoc.expand_bucket = 0;

        /* start a thread to do the expansion */
        if ((ret = cb_create_named_thread(&tid, assoc_maintenance_thread,
                                          engine, 1, "mc:assoc maint")) != 0)
        {
            EXTENSION_LOGGER_DESCRIPTOR *logger;
            logger = (void*)engine->server.extension->get_extension(EXTENSION_LOGGER);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                        "Can't create thread: %s\n", strerror(ret));
            engine->assoc.hashpower--;
            engine->assoc.expanding = false;
            free(engine->assoc.primary_hashtable);
            engine->assoc.primary_hashtable =engine->assoc.old_hashtable;
        }
    } else {
        engine->assoc.primary_hashtable = engine->assoc.old_hashtable;
        /* Bad news, but we can keep running. */
    }
}

/* Note: this isn't an assoc_update.  The key must not already exist to call this */
int assoc_insert(struct default_engine *engine, uint32_t hash, hash_item *it) {
    unsigned int oldbucket;

    cb_assert(assoc_find(engine, hash, item_get_key(it), it->nkey) == 0);  /* shouldn't have duplicately named things defined */

    if (engine->assoc.expanding &&
        (oldbucket = (hash & hashmask(engine->assoc.hashpower - 1))) >= engine->assoc.expand_bucket)
    {
        it->h_next = engine->assoc.old_hashtable[oldbucket];
        engine->assoc.old_hashtable[oldbucket] = it;
    } else {
        it->h_next = engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)];
        engine->assoc.primary_hashtable[hash & hashmask(engine->assoc.hashpower)] = it;
    }

    engine->assoc.hash_items++;
    if (! engine->assoc.expanding && engine->assoc.hash_items > (hashsize(engine->assoc.hashpower) * 3) / 2) {
        assoc_expand(engine);
    }

    MEMCACHED_ASSOC_INSERT(item_get_key(it), it->nkey, engine->assoc.hash_items);
    return 1;
}

void assoc_delete(struct default_engine *engine, uint32_t hash, const char *key, const size_t nkey) {
    hash_item **before = _hashitem_before(engine, hash, key, nkey);

    if (*before) {
        hash_item *nxt;
        engine->assoc.hash_items--;
        /* The DTrace probe cannot be triggered as the last instruction
         * due to possible tail-optimization by the compiler
         */
        MEMCACHED_ASSOC_DELETE(key, nkey, engine->assoc.hash_items);
        nxt = (*before)->h_next;
        (*before)->h_next = 0;   /* probably pointless, but whatever. */
        *before = nxt;
        return;
    }
    /* Note:  we never actually get here.  the callers don't delete things
       they can't find. */
    cb_assert(*before != 0);
}



#define DEFAULT_HASH_BULK_MOVE 1
int hash_bulk_move = DEFAULT_HASH_BULK_MOVE;

static void assoc_maintenance_thread(void *arg) {
    struct default_engine *engine = arg;
    bool done = false;
    do {
        int ii;
        cb_mutex_enter(&engine->cache_lock);

        for (ii = 0; ii < hash_bulk_move && engine->assoc.expanding; ++ii) {
            hash_item *it, *next;
            int bucket;

            for (it = engine->assoc.old_hashtable[engine->assoc.expand_bucket];
                 NULL != it; it = next) {
                next = it->h_next;

                bucket = engine->server.core->hash(item_get_key(it), it->nkey, 0)
                    & hashmask(engine->assoc.hashpower);
                it->h_next = engine->assoc.primary_hashtable[bucket];
                engine->assoc.primary_hashtable[bucket] = it;
            }

            engine->assoc.old_hashtable[engine->assoc.expand_bucket] = NULL;
            engine->assoc.expand_bucket++;
            if (engine->assoc.expand_bucket == hashsize(engine->assoc.hashpower - 1)) {
                engine->assoc.expanding = false;
                free(engine->assoc.old_hashtable);
                if (engine->config.verbose > 1) {
                    EXTENSION_LOGGER_DESCRIPTOR *logger;
                    logger = (void*)engine->server.extension->get_extension(EXTENSION_LOGGER);
                    logger->log(EXTENSION_LOG_INFO, NULL,
                                "Hash table expansion done\n");
                }
            }
        }
        if (!engine->assoc.expanding) {
            done = true;
        }
        cb_mutex_exit(&engine->cache_lock);
    } while (!done);
}
