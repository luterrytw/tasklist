#ifndef __TASK_LIST_H__
#define __TASK_LIST_H__

#include "tcp-socket.h"

#include <stdint.h>
#include <pthread.h>

struct TLTaskST;

typedef struct {
	int isRunning;
	int localPipePort; // port of pipeRecv socket
	SOCKET pipeRecv;
	SOCKET pipeSend;
	pthread_t loopThread;
	pthread_mutex_t listLock;
	struct TLTaskST* lastMinTask;
	struct TLTaskST* tasklist;
} TaskListHandler;

/*
	data:
		the user defined data pass by tl_add_task()
	function definition for task callback
*/
typedef void* (*TLTaskFunc)(TaskListHandler *hdl, void* taskdata);

typedef struct TLTaskST {
	TLTaskFunc taskFunc;
	void *taskdata;
	int64_t abstime; // time from 1970
	struct TLTaskST* next;
} TLTask;

#define TL_IT_MATCH			1
#define TL_IT_NOT_MATCH		0
#define TL_IT_CONTINUE		0
#define TL_IT_BREAK			-1
#define TL_IT_REMOVE		1
#define TL_IT_REMOVE_BREAK	2
/*
	function definition for iterator each task in list
	taskdata:
		the user defined data pass by tl_add_task()
	itdata:
		the data pass to tl_iterator_task()
	return 0(TL_IT_CONTINUE) for success & continue
	return -1(TL_IT_BREAK) for fail & break
	return 1(TL_IT_REMOVE) for removing this task
	return 2(TL_IT_REMOVE_BREAK) for removing the first found task
	
	return TL_IT_MATCH/TL_IT_NOT_MATCH for tl_find_task()
*/
typedef int (*TLIteratorFunc)(TaskListHandler *hdl, void* taskdata, void* itdata);
typedef char* (*TLDumpFunc)(void* taskdata, char* strBuf, int strBufLen);
typedef int (*TLMatchFunc)(void* matchdata, void* taskdata);

#ifdef __cplusplus
extern "C" {
#endif

TaskListHandler* tl_create_handler(int port);
void tl_release_handler(TaskListHandler* hdl);

/*
	create thread that will call tl_task_loop() without block current thread 
*/
int tl_start_task_loop_thread(TaskListHandler* hdl);

/*
	stop thread create by tl_start_task_loop_thread()
*/
int tl_stop_task_loop_thread(TaskListHandler* hdl);

/*
	Add a new task to task list
*/
int tl_add_task(TaskListHandler* hdl,
			    int64_t timeout, // msec. time to invoke the callback function
			    TLTaskFunc taskFunc, // timeout callback function
			    void* taskdata); // data for func

int tl_add_task_abstime(TaskListHandler* hdl,
			    int64_t abstime, // msec. time to invoke the callback function
			    TLTaskFunc taskFunc, // callback function
			    void* taskdata); // data for func
/*
	do function for each task in taslist
*/
int tl_iterator_task(TaskListHandler* hdl, TLIteratorFunc func, void* itdata);

/*
	dump all tasks in tasklist
*/
int tl_dump_tasks(TaskListHandler* hdl, TLDumpFunc func, char* title);

/*
	matchdata:
		the user define data in task
	if found, return the taskdata of task
	return NULL while not found
	
*/
void* tl_find_task(TaskListHandler* hdl, TLMatchFunc matchFunc, void* matchdata);

/*
	matchdata:
		the user define data in task
	if found, return the taskdata of task and remove the task
	return NULL while not found
	
*/
void* tl_find_task_and_remove(TaskListHandler* hdl, TLMatchFunc matchFunc, void* matchdata);

#ifdef __cplusplus
}
#endif

#endif