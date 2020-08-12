#ifndef __TASK_LIST_H__
#define __TASK_LIST_H__

#include "tcp-socket.h"

#include <stdint.h>
#include <pthread.h>

/*
	data:
		the user defined data pass by tl_add_task()
	function definition for task callback
*/
typedef void* (*TLTaskFunc)(void* data);

#define TL_IT_CONTINUE		0
#define TL_IT_BREAK			-1
#define TL_IT_REMOVE		1
#define TL_IT_REMOVE_BREAK	2
/*
	function definition for iterator each task in list
	data:
		the user defined data pass by tl_add_task()
	itdata:
		the data pass to tl_iterator_task()
	return 0(TL_IT_CONTINUE) for success & continue
	return -1(TL_IT_BREAK) for fail & break
	return 1(TL_IT_REMOVE) for removing this task
	return 2(TL_IT_REMOVE_BREAK) for removing the first found task
*/
typedef int (*TLIteratorFunc)(void* data, void* itdata);

typedef struct TLTaskST {
	TLTaskFunc taskFunc;
	TLTaskFunc freeFunc;
	void *data;
	int64_t timeout; // timeout time from 1970
	struct TLTaskST* next;
} TLTask;

typedef struct TaskListHandlerST {
	int isRunning;
	int localPipePort; // port of pipeRecv socket
	SOCKET pipeRecv;
	SOCKET pipeSend;
	pthread_t loopThread;
	pthread_mutex_t listLock;
	TLTask* tasklist;
} TaskListHandler;


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
				TLTaskFunc freeFunc, // NULL or function to free data in this task
			    void* data); // data for func


/*
	do function for each task in taslist
*/
int tl_iterator_task(TaskListHandler* hdl, TLIteratorFunc func, void* itdata);

/*
	dump all tasks in tasklist
*/
int tl_dump_tasks(TaskListHandler* hdl, TLTaskFunc func, char* title);

#ifdef __cplusplus
}
#endif

#endif