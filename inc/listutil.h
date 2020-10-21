#ifndef __LIST_UTIL_H__
#define __LIST_UTIL_H__

#include <stdint.h>
#include <pthread.h>

struct LUEntryST;

typedef struct {
	int type; // LU_TYPE_xxx
    pthread_mutex_t listLock;
	// waiting condition for lu_pop() in LU_TYPE_NONBLOCK_STACK
	// waiting condition for lu_dequeue() in LU_TYPE_BLOCK_QUEUE
	// waiting condition for lu_wait_notify() in LU_TYPE_LIST
	pthread_cond_t listCond;
	int leaveFlag;
    struct LUEntryST* head;
    struct LUEntryST* tail;
} LUHandler;

typedef struct LUEntryST {
    void *data;
    struct LUEntryST* next;
	struct LUEntryST* prev;
} LUEntry;

#define LU_IT_MATCH         1
#define LU_IT_NOT_MATCH     0
#define LU_IT_CONTINUE      0
#define LU_IT_BREAK         -1
#define LU_IT_REMOVE        1
#define LU_IT_REMOVE_BREAK  2

#define LU_TYPE_NONBLOCK		0
#define LU_TYPE_BLOCK			1
#define LU_TYPE_LIST			(1<<1) | LU_TYPE_NONBLOCK
#define LU_TYPE_NONBLOCK_QUEUE	(2<<1) | LU_TYPE_NONBLOCK
#define LU_TYPE_BLOCK_QUEUE		(2<<1) | LU_TYPE_BLOCK
#define LU_TYPE_NONBLOCK_STACK	(3<<1) | LU_TYPE_NONBLOCK
#define LU_TYPE_BLOCK_STACK		(3<<1) | LU_TYPE_BLOCK

/*
    function definition for iterator each entry in list
    entrydata:
        the user defined data pass by lu_add()
    itdata:
        the data pass to lu_iterator()
    return 0(LU_IT_CONTINUE) for success & continue
    return -1(LU_IT_BREAK) for fail & break
    return 1(LU_IT_REMOVE) for removing this entry
    return 2(LU_IT_REMOVE_BREAK) for removing the first found entry
    
    return LU_IT_MATCH/LU_IT_NOT_MATCH for lu_find()
*/
typedef int (*LUIteratorFunc)(LUHandler *hdl, void* entrydata, void* itdata);

/*
    strBuf:
        output, the internal buffer for dump entrydata
    strBufLen:
        the length of strBuf
*/
typedef char* (*LUDumpFunc)(void* entrydata, char* strBuf, int strBufLen);

/*
    match function for each entry in list
    matchdata:
        the input data for match
    entrydata:
        the data of entry in list
*/
typedef int (*LUMatchFunc)(void* entrydata, void* matchdata);

#ifdef __cplusplus
extern "C" {
#endif

LUHandler* lu_create_list(int type);

/*
    NOTE: you must free all entrydata before lu_release_list()
    We don't free entrydata while release_all_entry() because we have no default free callback.
    We don't define default free callback because you can call lu_remove() to get removed entrydata without freed.
    You can free all entrydata by lu_iterator(free_callback) and return LU_IT_REMOVE in free_callback()
*/
void lu_release_list(LUHandler* hdl);

/*
    Return 1 for empty, else 0
*/
int lu_is_empty(LUHandler* hdl);

/*
    Add data to list->tail
*/
int lu_add(LUHandler* hdl, void* entrydata); // data for func

/*
    do function for each entry in taslist
    itdata:
        the input data pass to LUIteratorFunc(hdl, entrydata, itdata)
*/
int lu_iterator(LUHandler* hdl, LUIteratorFunc func, void* itdata);

/*
    dump all entries in list
*/
int lu_dump_list(char* title, LUHandler* hdl, LUDumpFunc func);

/*
    matchdata:
        the input data for match
    Return
        the entrydata of entry if found
        NULL while not found
*/
void* lu_find(LUHandler* hdl, LUMatchFunc matchFunc, void* matchdata);

/*
    matchdata:
        the user define data in entry
    if found, return the entrydata of entry and remove the entry
    return NULL while not found
    
*/
void* lu_remove(LUHandler* hdl, LUMatchFunc matchFunc, void* matchdata);

/*
   push data to list->head for FILO
*/
int lu_push(LUHandler* hdl, void* entrydata);

/*
   Pop data from list->head for FILO
*/
void* lu_pop(LUHandler* hdl);

/*
    Add data to list->tail for FIFO
*/
#define lu_enqueue lu_add

/*
   Remove data from list->head for FIFO
*/
#define lu_dequeue lu_pop

/*
   clear list without free content
*/
void lu_clear(LUHandler* hdl);

/*
	notify about list change
*/
void lu_notify(LUHandler* hdl);

/*
	block and wait notify event
*/
void lu_wait_notify(LUHandler* hdl);

#ifdef __cplusplus
}
#endif

#endif