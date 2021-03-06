#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "listutil.h"

#define LOG_TAG "lu"
#include "log.h"

typedef int (*LUIteratorEntryFunc)(LUEntry* task, void* itdata);

#define MAX_DUMP_STR_BUF_LEN    1024
typedef struct {
    char strBuf[MAX_DUMP_STR_BUF_LEN];
    int count;
    LUDumpFunc dumpFunc;
} LuEntryDumpST;

////////////////////////////////////////////////////////////////////////////////
// List Utility
////////////////////////////////////////////////////////////////////////////////
static int dump_entry(LUEntry* entry, void* dumpdata)
{
    LuEntryDumpST* dumpst = (LuEntryDumpST*) dumpdata;
    char* str = NULL;
    dumpst->count++;

    if (dumpst->dumpFunc) {
        dumpst->strBuf[sizeof(dumpst->strBuf)-1] = '\0'; // null end of strBuf
        str = dumpst->dumpFunc(entry->data, dumpst->strBuf, sizeof(dumpst->strBuf)-1); // -1 to avoid null end be overwrite
    }

    if (str) {
        LOGI("ENTRY %d(%p): %s", dumpst->count, entry, str);
    } else {
        LOGI("ENTRY %d(%p): ", dumpst->count, entry);
    }
    return 0;
}

static void release_all_entry(LUHandler* hdl)
{
    LUEntry *entry2free, *entry;

    pthread_mutex_lock(&hdl->listLock);
    entry = hdl->head;
    while (entry) {
        entry2free = entry;
        entry = entry->next;
        free(entry2free);
    }
    hdl->head = NULL;
    hdl->tail = NULL;
    pthread_mutex_unlock(&hdl->listLock);
}

/*
    do function for each task in taslist
*/
static int iterator_entry(LUHandler* hdl, LUIteratorEntryFunc itfunc, void* itdata)
{
    int ret = 0;
    LUEntry *entry;

    if (!itfunc) {
        return -1;
    }

    pthread_mutex_lock(&hdl->listLock);
    entry = hdl->head;
    while (entry) {
        ret = itfunc(entry, itdata);
        if (ret == LU_IT_BREAK) {
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&hdl->listLock);
    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// Entry List Export Function
////////////////////////////////////////////////////////////////////////////////
/*
    return NULL for fail
*/
LUHandler* lu_create_list(int type)
{
    LUHandler* hdl = (LUHandler*) malloc(sizeof(LUHandler));
    if (!hdl)
        return NULL;
    memset(hdl, 0, sizeof(LUHandler));
	hdl->type = type;
    pthread_mutex_init(&hdl->listLock, NULL);
	pthread_cond_init(&hdl->listCond, NULL);
    return hdl;
}

/*
    NOTE: you must free all entrydata before lu_release_handler()
    We don't free entrydata while release_all_entry() because we have no default free callback.
    We don't define default free callback because you can call lu_remove() to get removed entrydata without freed.
    You can free all entrydata by lu_iterator(free_callback) and return LU_IT_REMOVE in free_callback()
*/
void lu_release_list(LUHandler* hdl)
{
    if (!hdl)
        return;
    release_all_entry(hdl);
	
	pthread_mutex_lock(&hdl->listLock);
	if (hdl->type & LU_TYPE_BLOCK) {
		hdl->leaveFlag = 1;
		pthread_cond_signal(&hdl->listCond);
	}
	pthread_mutex_unlock(&hdl->listLock);
	
	usleep(100000); // waiting thread(lu_pop) end
	
	// lock again to confirm all waiting thread(lu_pop) is end
	pthread_mutex_lock(&hdl->listLock);
	pthread_mutex_unlock(&hdl->listLock);
	// destory mutex & cond
    pthread_mutex_destroy(&hdl->listLock);
	pthread_cond_destroy(&hdl->listCond);
    free(hdl);
}

/*
    Return 1 for empty, else 0
*/
int lu_is_empty(LUHandler* hdl)
{
    return (hdl->head == NULL)? 1: 0;
}

/*
    Add data to list->tail for FIFO
*/
int lu_add(LUHandler* hdl, void* entrydata)
{
    LUEntry *entry = (LUEntry*) calloc(1, sizeof(LUEntry));
    if (!entry) {
        LOGE("lu_add: entry == NULL");
        return -1;
    }

    // init entry
    entry->data = entrydata;

    // add to list
    pthread_mutex_lock(&hdl->listLock);
    if (hdl->tail) {
        hdl->tail->next = entry;
		entry->prev = hdl->tail;
		entry->next = NULL;
		hdl->tail = entry;
    } else { // no tail imply no head, so fill it
		hdl->tail = entry;
        hdl->head = entry;
		entry->prev = NULL;
		entry->next = NULL;
    }
	if (hdl->type & LU_TYPE_BLOCK)
		pthread_cond_signal(&hdl->listCond);
    pthread_mutex_unlock(&hdl->listLock);

    return 0;
}

/*
    Do function, itfunc, for each entry in list
    
    Depend on itfunc() return value, it will do following action after entry
        if itfunc() return LU_IT_CONTINUE, continue entry in list
        if itfunc() return LU_IT_BREAK, break iterator entry
        if itfunc() return LU_IT_REMOVE, remove this entry in list
        if itfunc() return LU_IT_REMOVE_BREAK, remove this entry and break
*/
int lu_iterator(LUHandler* hdl, LUIteratorFunc itfunc, void* itdata)
{
    int ret = 0;
    LUEntry *entry = NULL;
    LUEntry *entry2free = NULL;

    if (!itfunc) {
        return -1;
    }

    pthread_mutex_lock(&hdl->listLock);
    entry = hdl->head;
    while (entry) {
        ret = itfunc(hdl, entry->data, itdata);
        if (ret == LU_IT_BREAK) {
            break;
        } else if (ret == LU_IT_REMOVE || ret == LU_IT_REMOVE_BREAK) {
            // remove entry
			if (hdl->head != entry && hdl->tail != entry) {
				entry->prev->next = entry->next;
				entry->next->prev = entry->prev;
			} else {
				if (hdl->head == entry) { // first entry
					hdl->head = entry->next;
					if (hdl->head) {
						hdl->head->prev = NULL;
					}
				}
				if (hdl->tail == entry) { // tail entry
					hdl->tail = entry->prev;
					if (hdl->tail) {
						hdl->tail->next = NULL;
					}
				}
			}
            entry2free = entry;
            entry = entry->next;
            free(entry2free);
            
            // break or not
            if (ret == LU_IT_REMOVE_BREAK) {
                break;
            } else {
                continue;
            }
        } else {
            // get next entry
            entry = entry->next;
        }
    }
    pthread_mutex_unlock(&hdl->listLock);
    return ret;
}

/*
    dump all entries in list
*/
int lu_dump_list(char* title, LUHandler* hdl, LUDumpFunc dumpFunc)
{
    LuEntryDumpST dumpst;

    if (title) LOGI("------ %s LIST START ------", title);
    else LOGI("------ DUMP LIST START ------");

    dumpst.dumpFunc = dumpFunc;
    dumpst.count = 0;
    iterator_entry(hdl, dump_entry, &dumpst);
    
    if (title) LOGI("------ %s LIST END ------", title);
    else LOGI("------ DUMP LIST END ------");

    return 0;
}

/*
    matchdata:
        the user define data in entry
    if found, return the entrydata of entry
    return NULL while not found
    
*/
void* lu_find(LUHandler* hdl, LUMatchFunc matchFunc, void* matchdata)
{
    int ret = 0;
    LUEntry *entry;

    if (!matchFunc) {
        return NULL;
    }
    
    pthread_mutex_lock(&hdl->listLock);
    entry = hdl->head;
    while (entry) {
        ret = matchFunc(entry->data, matchdata);
        if (ret == LU_IT_MATCH) {
            pthread_mutex_unlock(&hdl->listLock);
            return entry->data;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&hdl->listLock);
    return NULL;
}

/*
    matchdata:
        the user define data in entry
    if found, return the entrydata of entry and remove the entry
    return NULL while not found
*/
void* lu_remove(LUHandler* hdl, LUMatchFunc matchFunc, void* matchdata)
{
    int ret = 0;
    LUEntry *entry;
    void *retdata = NULL;

    if (!matchFunc) {
        return NULL;
    }
    
    pthread_mutex_lock(&hdl->listLock);
    entry = hdl->head;
    while (entry) {
        ret = matchFunc(entry->data, matchdata);
        if (ret == LU_IT_MATCH) {
			if (hdl->head != entry && hdl->tail != entry) {
				entry->prev->next = entry->next;
				entry->next->prev = entry->prev;
			} else {
				if (hdl->head == entry) { // first entry
					hdl->head = entry->next;
					if (hdl->head) {
						hdl->head->prev = NULL;
					}
				}
				if (hdl->tail == entry) { // tail entry
					hdl->tail = entry->prev;
					if (hdl->tail) {
						hdl->tail->next = NULL;
					}
				}
			}
            retdata = entry->data;
            free(entry); // free entry item
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&hdl->listLock);
    return retdata;
}

/*
   push data to list->head for FILO
*/
int lu_push(LUHandler* hdl, void* entrydata)
{
    LUEntry *entry = (LUEntry*) calloc(1, sizeof(LUEntry));
    if (!entry) {
        LOGE("lu_push: entry == NULL");
        return -1;
    }

    // init entry
    entry->data = entrydata;

    // add to list
    pthread_mutex_lock(&hdl->listLock);
    if (hdl->head) {
		hdl->head->prev = entry;
		entry->next = hdl->head;
		hdl->head = entry;
		entry->prev = NULL;
    } else { // no head imply no tail, so fill it
		hdl->head = entry;
		hdl->tail = entry;
		entry->prev = NULL;
		entry->next = NULL;
    }
	if (hdl->type & LU_TYPE_BLOCK)
		pthread_cond_signal(&hdl->listCond);
    pthread_mutex_unlock(&hdl->listLock);

    return 0;
}

void* lu_pop(LUHandler* hdl)
{
    LUEntry *entry = NULL;
    void *retdata = NULL;

	// add to list
    pthread_mutex_lock(&hdl->listLock);
	do {
		if (hdl->head == NULL) {
			if (hdl->type & LU_TYPE_BLOCK) { // wait for push or queue
				pthread_cond_wait(&hdl->listCond, &hdl->listLock);
			} else { // return immediately
				break;
			}
		}
		if (hdl->leaveFlag == 1 || hdl->head == NULL) {
			break;
		}

		entry = hdl->head;
		retdata = entry->data;
		// modify head
		hdl->head = hdl->head->next;
		if (hdl->head) {
			hdl->head->prev = NULL;
		}
		// modify tail
		if (hdl->tail == entry) { // tail entry
			hdl->tail = entry->prev;
			if (hdl->tail) {
				hdl->tail->next = NULL;
			}
		}
	}
	while (0);
	pthread_mutex_unlock(&hdl->listLock);

	if (entry) {
		free(entry);
	}
    return retdata;
}

void lu_clear(LUHandler* hdl)
{
	LUEntry *entry = NULL;
	LUEntry *entry2free = NULL;
	
	pthread_mutex_lock(&hdl->listLock);
	entry = hdl->head;
	while (entry) {
		entry2free = entry;
		entry = entry->next;
		free(entry2free);
	}
	pthread_mutex_unlock(&hdl->listLock);
}

void lu_notify(LUHandler* hdl)
{
	pthread_mutex_lock(&hdl->listLock);
	pthread_cond_signal(&hdl->listCond);
	pthread_mutex_unlock(&hdl->listLock);
}

void lu_wait_notify(LUHandler* hdl)
{
	pthread_mutex_lock(&hdl->listLock);
	pthread_cond_wait(&hdl->listCond, &hdl->listLock);
	pthread_mutex_unlock(&hdl->listLock);	
}
