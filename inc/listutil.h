#ifndef __LIST_UTIL_H__
#define __LIST_UTIL_H__

#include <stdint.h>
#include <pthread.h>

struct LUEntryST;

typedef struct {
	pthread_mutex_t listLock;
	struct LUEntryST* list;
} LUHandler;

typedef struct LUEntryST {
	void *data;
	struct LUEntryST* next;
} LUEntry;

#define LU_IT_MATCH			1
#define LU_IT_NOT_MATCH		0
#define LU_IT_CONTINUE		0
#define LU_IT_BREAK			-1
#define LU_IT_REMOVE		1
#define LU_IT_REMOVE_BREAK	2

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

LUHandler* lu_create_list();

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
	Add a new data of entry to list
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

#ifdef __cplusplus
}
#endif

#endif