#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// must before <unistd.h> that will include windows.h
#include "tasklist.h"

#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <pthread.h>

#define LOG_TAG "tl"
#include "log.h"


typedef int (*TLIteratorTaskFunc)(TLTask*, void*);

struct TASK_DUMP_ST {
	int count;
	TLTaskFunc dumpFunc;
};

////////////////////////////////////////////////////////////////////////////////
// Utility function
////////////////////////////////////////////////////////////////////////////////
static int64_t get_current_ms_time(void)
{
    int64_t         ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = (int64_t) round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
    if (ms > 999) {
        s++;
        ms = 0;
    }
	return ((int64_t) s) * 1000 + ms;
}


////////////////////////////////////////////////////////////////////////////////
// Task List Utility
////////////////////////////////////////////////////////////////////////////////
static int init_socket(TaskListHandler* hdl)
{
	char port[12];
	SOCKET pipeAccept = INVALID_SOCKET;
	
	// init a local socket to replace pipe() function to compatible with windows
	snprintf(port, sizeof(port), "%d", hdl->localPipePort);
	// init server accept socket
	pipeAccept = init_server_tcp_socket("127.0.0.1", port);
	SOCKET_NOTVALID_GOTO_ERROR(pipeAccept, "init_socket: init_server_tcp_socket(pipeAccept) fail");

	// init client send socket
	if (hdl->pipeSend != INVALID_SOCKET) {
		close(hdl->pipeSend);
	}
	hdl->pipeSend = init_client_tcp_socket("127.0.0.1", port);
	SOCKET_NOTVALID_GOTO_ERROR(hdl->pipeSend, "init_socket: init_client_tcp_socket(pipeSend) fail");
	
	// init server receive socket
	if (hdl->pipeRecv != INVALID_SOCKET) {
		close(hdl->pipeRecv);
	}
	hdl->pipeRecv = accept(pipeAccept, NULL, 0);
	SOCKET_NOTVALID_GOTO_ERROR(hdl->pipeRecv, "init_socket: init_client_tcp_socket(pipeRecv) fail");

	if (pipeAccept != INVALID_SOCKET) close(pipeAccept);

	return 0;

error:
	if (pipeAccept != INVALID_SOCKET) close(pipeAccept);
	if (hdl->pipeSend != INVALID_SOCKET) close(hdl->pipeSend);
	if (hdl->pipeRecv != INVALID_SOCKET) close(hdl->pipeRecv);
	return -1;
}

/*
	send dummy data to local pipe socket to trigger socket event
*/
static int send_dummy_to_localpipe(TaskListHandler* hdl)
{
	int ret;
	unsigned char dummy = 0;

	if (hdl->pipeSend == INVALID_SOCKET) {
		init_socket(hdl);
		if (hdl->pipeSend == INVALID_SOCKET) {
			LOGE("send_dummy_message fail, invalid socket");
			return -1;
		}
	}
	ret = send(hdl->pipeSend, &dummy, 1, 0);

	return (ret == 1)? 0: -1;
}

/*
	read dummy data from local pipe socket to consume the socket event
*/
static int read_dummy_from_localpipe(TaskListHandler* hdl)
{
	unsigned char dummy;
	int bytes = -1;

	if (hdl->pipeRecv == INVALID_SOCKET) {
		init_socket(hdl);
		if (hdl->pipeRecv == INVALID_SOCKET)
			LOGE("read_dummy_message fail, invalid socket");
			return -1;
	}
	
	bytes = recv(hdl->pipeRecv, &dummy, 1, 0);
	SOCKET_RESULT_GOTO_ERROR(bytes, "recv(dummy) failed");
	if(bytes != 1) {
		LOGE("dummy read fail, errno=%d", errno);
	}

	return bytes;

error:
	return -1;
}

static TLTask* remove_timeout_task(TaskListHandler* hdl, int64_t timeoutTime)
{
	TLTask* task;
	TLTask* prev = NULL;

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		if (task->timeout <= timeoutTime) { // timeout
			if (prev) {
				prev->next = task->next;
			} else {
				hdl->tasklist = task->next;
			}
			task->next = NULL;
			pthread_mutex_unlock(&hdl->listLock);
			return task;
		}
		prev = task;
		task = task->next;
	}
	pthread_mutex_unlock(&hdl->listLock);

	return NULL;
}


static void do_task(TaskListHandler* hdl)
{
	int64_t timeoutTime = get_current_ms_time();
	TLTask* task;
	task = remove_timeout_task(hdl, timeoutTime);

	while (task) {
		LOGD("do_task %p", task->taskFunc);

		if (task->taskFunc) {
			task->taskFunc(task->data);
		}
		if (task->freeFunc) {
			task->freeFunc(task->data);
		}
		free(task); // free, since we have done the task
		task = remove_timeout_task(hdl, timeoutTime);
	}
}

static int do_socket(TaskListHandler* hdl, fd_set* rfds)
{
	int length = 0;

	if (hdl->pipeRecv != INVALID_SOCKET && FD_ISSET(hdl->pipeRecv, rfds)) { // interrupt, to stop
		length = read_dummy_from_localpipe(hdl);
		return 0; // receive a interrupt event
	} else { // unknown fd input
		return 0;
	}

	if (length <= 0) { // decrypt fail
		return -1;
	}

	return 0;
}

/*
	return minum task timeout time, if not found, return 36000
*/
static void min_task_timeout_time(TaskListHandler* hdl, struct timeval* tv)
{
	int64_t current = get_current_ms_time();
	int64_t timeout = 36000 + current;
	TLTask* task;

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	pthread_mutex_unlock(&hdl->listLock);
	if (!task) {
		tv->tv_sec = 36000;
		tv->tv_usec = 0;	
		return;
	}

	pthread_mutex_lock(&hdl->listLock);
	while (task) {
		if (timeout > task->timeout) {
			timeout = task->timeout;
		}
		task = task->next;
	}
	pthread_mutex_unlock(&hdl->listLock);

	tv->tv_sec = 0;
	if ((timeout - current) <= 0) {
		tv->tv_usec = 0;
	} else {
		tv->tv_usec = (long) (timeout - current) * 1000;
	}	
}

static int dump_task(TLTask* task, void* data)
{
	struct TASK_DUMP_ST* dumpst = (struct TASK_DUMP_ST*) data;
	char* str = NULL;

	dumpst->count++;
	LOGI("TASK %d: \n\
\ttimeout: %ld\n\
\ttaskFunc: %p\n\
\tfreeFunc: %p\
",
		 dumpst->count,
		 task->timeout,
		 task->taskFunc,
		 task->freeFunc);

	if (dumpst->dumpFunc) {
		str = dumpst->dumpFunc(task->data);
		if (str) {
			LOGI("Data in Task: \n\t%s\n", str);
		}
	}

	return 0;
}

static void release_all_task(TaskListHandler* hdl)
{
	TLTask *task2free, *task;

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	pthread_mutex_unlock(&hdl->listLock);

	while (task) {
		pthread_mutex_lock(&hdl->listLock);
		task2free = task;
		task = task->next;
		pthread_mutex_unlock(&hdl->listLock);

		if (task2free->freeFunc) {
			task2free->freeFunc(task2free->data);
		}
		free(task2free);
	}
	
	pthread_mutex_lock(&hdl->listLock);
	hdl->tasklist = NULL;
	pthread_mutex_unlock(&hdl->listLock);
}

/*
	do function for each task in taslist
*/
static int iterator_task(TaskListHandler* hdl, TLIteratorTaskFunc func, void* data)
{
	int ret = 0;
	TLTask *task;

	if (!func) {
		return -1;
	}
	
	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	pthread_mutex_unlock(&hdl->listLock);
	while (task) {
		ret = func(task, data);
		if (ret == TL_IT_BREAK) {
			break;
		}
		pthread_mutex_lock(&hdl->listLock);
		task = task->next;
		pthread_mutex_unlock(&hdl->listLock);
	}
	return ret;
}

////////////////////////////////////////////////////////////////////////////////
// Task List Export Function
////////////////////////////////////////////////////////////////////////////////
/*
	return NULL for fail
*/
TaskListHandler* tl_create_handler(int port)
{
	TaskListHandler* hdl = (TaskListHandler*) malloc(sizeof(TaskListHandler));
	if (!hdl)
		return NULL;
	
	memset(hdl, 0, sizeof(TaskListHandler));
	hdl->localPipePort = port;
	hdl->pipeRecv = INVALID_SOCKET;
	hdl->pipeSend = INVALID_SOCKET;
	pthread_mutex_init(&hdl->listLock, NULL);

	if (init_socket(hdl))
		return NULL;
	
	return hdl;
}

void tl_release_handler(TaskListHandler* hdl)
{
	tl_stop_task_loop_thread(hdl);
	release_all_task(hdl);

	if (hdl->pipeRecv != INVALID_SOCKET) {
		close(hdl->pipeRecv);
		hdl->pipeRecv = INVALID_SOCKET;
	}
	if (hdl->pipeSend != INVALID_SOCKET) {
		close(hdl->pipeSend);
		hdl->pipeSend = INVALID_SOCKET;
	}
	pthread_mutex_destroy(&hdl->listLock);

	if (hdl)
		free(hdl);
}

/*
	the main thread that will handle task in list
*/
void* tl_task_loop(void *param)
{
	fd_set rfds, rfdsCopy;
	struct timeval tv;
	int ret, fdmax = 0;
	TaskListHandler* hdl = (TaskListHandler*) param;

	if (!hdl) return NULL;
	
	// init select
	FD_ZERO(&rfds);
	FD_SET(hdl->pipeRecv, &rfds);
	if ((int)hdl->pipeRecv > fdmax) {
		fdmax = hdl->pipeRecv;
	}

	rfdsCopy = rfds;
	tv.tv_sec = 3600;
	tv.tv_usec = 0;

	while (hdl->isRunning) {
		//LOGD("loop waiting, tv_sec=%ld, tv_usec=%ld..............................", tv.tv_sec, tv.tv_usec);

		ret = select(fdmax+1, &rfds, NULL, NULL, &tv);
		if (ret == -1) {
			LOGE("select failed: errno=%d", errno);
			break;
		} else if (ret == 0) { // timeout, check task list
			do_task(hdl);
		} else { // socket event
			do_socket(hdl, &rfds);
		}

		min_task_timeout_time(hdl, &tv);
		rfds = rfdsCopy;
	}

	return NULL;
}

/*
	create thread that will call tl_task_loop() without block current thread 
*/
int tl_start_task_loop_thread(TaskListHandler* hdl)
{
	LOGD("Start task loop thread");
	hdl->isRunning = 1;
	pthread_create(&hdl->loopThread, NULL, tl_task_loop, hdl);
	return 0;
}

/*
	stop thread create by tl_start_task_loop_thread()
*/
int tl_stop_task_loop_thread(TaskListHandler* hdl)
{
	if (hdl->isRunning == 0 || hdl->loopThread == 0)
		return 0;

	LOGD("Stop task loop thread...");
	hdl->isRunning = 0;
	send_dummy_to_localpipe(hdl);
	pthread_join(hdl->loopThread, NULL);
	LOGD("Stop task loop thread...ok");
	hdl->loopThread = 0;
	return 0;
}

/*
	Add a new task to task list
*/
int tl_add_task(TaskListHandler* hdl,
			    int64_t timeout, // msec. time to invoke the callback function
			    TLTaskFunc taskFunc, // timeout callback function
				TLTaskFunc freeFunc, // function to free data in this task
			    void* data) // data for func
{
	TLTask *task = (TLTask*) calloc(1, sizeof(TLTask));
	if (!task) {
		LOGE("tl_add_task: task == NULL");
		return -1;
	}

	// init task
	task->timeout = timeout + get_current_ms_time();
	task->taskFunc = taskFunc;
	task->freeFunc = freeFunc;
	task->data = data;

	// add to list
	pthread_mutex_lock(&hdl->listLock);
	if (hdl->tasklist) {
		task->next = hdl->tasklist;
	}
	hdl->tasklist = task;
	pthread_mutex_unlock(&hdl->listLock);

	// trigger interrupt to re-calculate timeout time
	send_dummy_to_localpipe(hdl);

	return 0;
}

/*
	Do function, func, for each task in taslist
	
	Depend on func() return value, it will do following action after task
		if func() return 0(TL_IT_CONTINUE), continue task in list
		if func() return -1(TL_IT_BREAK), break iterator task
		if func() return 1(TL_IT_REMOVE), remove this task in list
		if func() return 2(TL_IT_REMOVE_BREAK), remove this task and break
*/
int tl_iterator_task(TaskListHandler* hdl, TLIteratorFunc func, void* itdata)
{
	int ret = 0;
	TLTask *task = hdl->tasklist;
	TLTask *lastTask = NULL;
	TLTask *task2free = NULL;

	if (!func) {
		return -1;
	}

	while (task) {
		ret = func(task->data, itdata);

		if (ret == TL_IT_BREAK) {
			break;
		} else if (ret == TL_IT_REMOVE || ret == TL_IT_REMOVE_BREAK) {
			// remove task from list
			pthread_mutex_lock(&hdl->listLock);
			if (hdl->tasklist == task) { // first task
				hdl->tasklist = task->next;
			}
			if (lastTask) {
				lastTask->next = task->next;
			}
			task2free = task;
			task = task->next;
			pthread_mutex_unlock(&hdl->listLock);

			// free task
			if (task2free->freeFunc) {
				task2free->freeFunc(task2free->data);
			}
			free(task2free);
			
			// break or not
			if (ret == TL_IT_REMOVE_BREAK) {
				break;
			} else {
				continue;
			}
		} else {
			// get next task
			pthread_mutex_lock(&hdl->listLock);
			lastTask = task;
			task = task->next;
			pthread_mutex_unlock(&hdl->listLock);
		}
	}
	return ret;
}

/*
	dump all tasks in tasklist
*/
int tl_dump_tasks(TaskListHandler* hdl, TLTaskFunc func, char* title)
{
	struct TASK_DUMP_ST dumpst;

	if (title) LOGI("------ %s LIST START ------", title);
	else LOGI("------ DUMP TASK LIST START ------");

	dumpst.dumpFunc = func;
	dumpst.count = 0;
	iterator_task(hdl, dump_task, &dumpst);
	
	if (title) LOGI("------ %s LIST END ------", title);
	else LOGI("------ DUMP TASK LIST END ------");

	return 0;
}
