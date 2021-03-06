/*
 * Copyright (C) 2011 Bernhard Froehlich <decke@bluelife.at>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Author's name may not be used endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* atomic_fetchadd */
#include <sys/types.h>
#include <machine/atomic.h>

#include <leveldb/c.h>

#include "log.h"
#include "util.h"
#include "client.h"
#include "stomp.h"

#define CheckNoError(err) \
    if ((err) != NULL) { \
        logerror("%s:%d: %s\n", __FILE__, __LINE__, (err)); \
        abort(); \
    }


leveldb_t* db;
leveldb_cache_t* cache;
leveldb_env_t* env;
leveldb_options_t* options;
leveldb_readoptions_t* roptions;
leveldb_writeoptions_t* woptions;


int leveldb_init(void)
{
    char *error = NULL;

    /* Initialize LevelDB */
    env = leveldb_create_default_env();
    cache = leveldb_cache_create_lru(100000);
        
    options = leveldb_options_create();
    leveldb_options_set_cache(options, cache);
    leveldb_options_set_env(options, env);
    leveldb_options_set_create_if_missing(options, 1);
    leveldb_options_set_error_if_exists(options, 0);

    roptions = leveldb_readoptions_create();
    leveldb_readoptions_set_verify_checksums(roptions, 1);
    leveldb_readoptions_set_fill_cache(roptions, 0);

    woptions = leveldb_writeoptions_create();
    leveldb_writeoptions_set_sync(woptions, 1);

    db = leveldb_open(options, configget("dbFile"), &error);
    if(error != NULL){
       logerror("LevelDB Error: %s", error);
       return 1;
    }

    return 0;
}


int leveldb_free(void)
{
    leveldb_close(db);
    leveldb_options_destroy(options);
    leveldb_readoptions_destroy(roptions);
    leveldb_writeoptions_destroy(woptions);
    leveldb_cache_destroy(cache);
    leveldb_env_destroy(env);

    return 0;
}


int leveldb_add_message(struct queue *queue, char *message)
{
    leveldb_writebatch_t *wb;
    char key[MAXQUEUELEN+10];
    char value[16];
    char *error = NULL;
    int seq;

    if(strlen(queue->queuename) >= MAXQUEUELEN){
        logerror("LevelDB add_message failed: Queuename too long");
        return 1;
    }

    seq = atomic_fetchadd_int(&queue->write, 1);
    wb = leveldb_writebatch_create();

    snprintf(key, sizeof(key)-1, "%s.%d", queue->queuename, seq);
    key[sizeof(key)-1] = '\0';
    leveldb_writebatch_put(wb, key, strlen(key), message, strlen(message));

    snprintf(key, sizeof(key)-1, "%s.write", queue->queuename);
    key[sizeof(key)-1] = '\0';
    
    sprintf(value, "%d", seq);
    leveldb_writebatch_put(wb, key, strlen(key), value, strlen(value));

    leveldb_write(db, woptions, wb, &error);
    leveldb_writebatch_destroy(wb);

    if(error != NULL){
        logerror("LevelDB add_message failed: %s", error);
        return 1;
    }

    loginfo("Added message %d to %s: %.20s", queue->write, queue->queuename, message);

    return 0;
}

int leveldb_load_queue(struct queue *queue)
{
    char key[MAXQUEUELEN+10];
    char *value;
    char *error = NULL;
    size_t value_len;

    if(strlen(queue->queuename) >= MAXQUEUELEN){
        logerror("LevelDB add_message failed: Queuename too long");
        return 1;
    }

    /* queuename.read */
    snprintf(key, sizeof(key)-1, "%s.read", queue->queuename);
    key[sizeof(key)-1] = '\0';

    value = leveldb_get(db, roptions, key, strlen(key), &value_len, &error);
    if(error != NULL){
       logerror("LevelDB load_queue failed: %s", error);
       return 1;
    }

    if(value != NULL){
       queue->read = atoi(value)+1;
       free(value);
       value = NULL;
    }
    else
       queue->read = 1;


    /* queuename.write */
    snprintf(key, sizeof(key)-1, "%s.write", queue->queuename);
    key[sizeof(key)-1] = '\0';

    value = leveldb_get(db, roptions, key, strlen(key), &value_len, &error);
    if(error != NULL){
       logerror("LevelDB load_queue failed: %s", error);
       return 1;
    }

    if(value != NULL){
       queue->write = atoi(value)+1;
       free(value);
       value = NULL;
    }
    else
       queue->write = 1;

    return 0;
}

char* leveldb_get_message(struct queue *queue)
{
    return 0;
}

int leveldb_ack_message(struct queue *queue)
{
    return 0;
}

