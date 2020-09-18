// must before <unistd.h> that will include windows.h
#include "tasklist.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <pthread.h>

#ifndef WIN32
#include <sys/ioctl.h>
#endif

#define LOG_TAG "tl"
#include "log.h"

typedef int (*TLIteratorTaskFunc)(TLTask* task, void* itdata);

#define MAX_DUMP_STR_BUF_LEN	1024
struct TASK_DUMP_ST {
	char strBuf[MAX_DUMP_STR_BUF_LEN];
	int count;
	TLDumpFunc dumpFunc;
};

////////////////////////////////////////////////////////////////////////////////
// Utility function
////////////////////////////////////////////////////////////////////////////////
static int64_t get_current_ms_time(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	return ((int64_t) tv.tv_sec * 1000) + ((int64_t) tv.tv_usec / 1000);
}


////////////////////////////////////////////////////////////////////////////////
// Task List Utility
////////////////////////////////////////////////////////////////////////////////
static int init_socket(TaskListHandler* hdl)
{
	char port[12];
	SOCKET pipeAccept = INVALID_SOCKET;
	int result;
	
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
	
	// set non-blocking
#ifdef WIN32
	u_long non_blocking = 1;
	result = ioctlsocket(hdl->pipeRecv, FIONBIO, &non_blocking);
#else
	int non_blocking = 1;
	result = ioctl(hdl->pipeRecv, FIONBIO, &non_blocking);
#endif
	SOCKET_NONZERO_GOTO_ERROR(result, "FIONBIO setsockopt() failed");

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
			LOGE("send_dummy_to_localpipe fail, invalid socket");
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
	// ignore non-blocking no data read error
#ifdef WIN32
	if (bytes == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
		bytes = 0;
	}
#else
	if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		bytes = 0;
	}
#endif
	SOCKET_RESULT_GOTO_ERROR(bytes, "recv(dummy) failed");
	if (bytes != 1 && bytes != 0) {
		LOGE("dummy read fail, errno=%d", errno);
	}

	return bytes;

error:
	return -1;
}

/*
	find and upate minTask
*/
static void update_min_task(TaskListHandler* hdl)
{
	int64_t current = get_current_ms_time();
	int64_t abstime = 3600000 + current; // 3600000 ms, 1 hours.
	TLTask* task = hdl->tasklist;

	hdl->minTask = hdl->tasklist;
	while (task) {
		if (abstime > task->abstime) {
			abstime = task->abstime;
			hdl->minTask = task;
		}
		task = task->next;
	}
}

static TLTask* remove_timeout_task(TaskListHandler* hdl, int64_t timeoutTime)
{
	TLTask* task;
	TLTask* lastTask = NULL;

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		if (task->abstime <= timeoutTime) { // timeout
			if (lastTask) {
				lastTask->next = task->next;
			} else {
				hdl->tasklist = task->next;
			}
			task->next = NULL;
			// check minTask
			if (hdl->minTask == task) {
				update_min_task(hdl);
				// doesn't need to notify minTask change, because timeout will re-caculate after do_task()
			}
			pthread_mutex_unlock(&hdl->listLock);
			return task;
		}
		lastTask = task;
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
			task->taskFunc(hdl, task->taskdata);
		}
		free(task); // free, since we have done the task
		task = remove_timeout_task(hdl, timeoutTime);
	}
}

static int do_socket(TaskListHandler* hdl, fd_set* rfds)
{
	int length = 1;

	if (hdl->pipeRecv != INVALID_SOCKET && FD_ISSET(hdl->pipeRecv, rfds)) { // interrupt, to stop
		while (length > 0) {
			length = read_dummy_from_localpipe(hdl);
		}
		return 0; // receive a interrupt event
	} else { // unknown fd input
		return 0;
	}

	if (length < 0) { // decrypt fail
		return -1;
	}

	return 0;
}

/*
	return minum task abstime time, if not found, return 36000
*/
static void get_next_timeout_time(TaskListHandler* hdl, struct timeval* tv)
{
	int64_t current = get_current_ms_time();
	int64_t abstime = 3600000 + current; // 3600000 ms, 1 hours.
	int64_t difftime;

	pthread_mutex_lock(&hdl->listLock);
	if (hdl->minTask) {
		abstime = hdl->minTask->abstime;
	}

	difftime = abstime - current;
	if (difftime <= 0) {
		tv->tv_sec = 0;
		tv->tv_usec = 0;
	} else {
		tv->tv_sec = difftime / 1000;
		tv->tv_usec = (difftime % 1000) * 1000;
	}
	pthread_mutex_unlock(&hdl->listLock);
}

static int dump_task(TLTask* task, void* dumpdata)
{
	struct TASK_DUMP_ST* dumpst = (struct TASK_DUMP_ST*) dumpdata;
	char* str = NULL;
	struct tm timeinfo;
	time_t rawtime = (time_t) task->abstime / 1000;

	localtime_r(&rawtime, &timeinfo);

	dumpst->count++;
	LOGI("TASK %d(%p): abstime=%" PRId64 "(%04d-%02d-%02d %02d:%02d:%02d %d), taskFunc=%p",
			dumpst->count, task, task->abstime,
			timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
			timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
			timeinfo.tm_wday,
			task->taskFunc);

	if (dumpst->dumpFunc) {
		dumpst->strBuf[sizeof(dumpst->strBuf)-1] = '\0'; // null end of strBuf
		str = dumpst->dumpFunc(task->taskdata, dumpst->strBuf, sizeof(dumpst->strBuf)-1); // -1 to avoid null end be overwrite
		if (str) {
			LOGI("\tData in Task: %s", str);
		}
	}

	return 0;
}

static void release_all_task(TaskListHandler* hdl)
{
	TLTask *task2free, *task;

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		task2free = task;
		task = task->next;
		free(task2free);
	}
	hdl->tasklist = NULL;
	pthread_mutex_unlock(&hdl->listLock);
}

/*
	do function for each task in taslist
*/
static int iterator_task(TaskListHandler* hdl, TLIteratorTaskFunc itfunc, void* itdata)
{
	int ret = 0;
	TLTask *task;

	if (!itfunc) {
		return -1;
	}
	
	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		ret = itfunc(task, itdata);
		if (ret == TL_IT_BREAK) {
			break;
		}
		task = task->next;
	}
	pthread_mutex_unlock(&hdl->listLock);
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
	if (!hdl)
		return;
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
	free(hdl);
}

/*
	Return 1 for empty, else 0
*/
int tl_is_empty(TaskListHandler* hdl)
{
	return (hdl->tasklist == NULL)? 1: 0;
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
		LOGI("loop waiting, tv_sec=%ld, tv_usec=%ld..............................", tv.tv_sec, tv.tv_usec);
		ret = select(fdmax+1, &rfds, NULL, NULL, &tv);
		if (ret == -1) {
			LOGE("select failed: errno=%d", errno);
			break;
		} else if (ret == 0) { // timeout, check task list
			do_task(hdl);
		} else { // socket event
			do_socket(hdl, &rfds);
		}

		get_next_timeout_time(hdl, &tv);
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
int tl_add_task_abstime(TaskListHandler* hdl,
			    int64_t abstime, // msec. time to invoke the callback function
			    TLTaskFunc taskFunc, // callback function
			    void* taskdata) // data for func
{
	TLTask *task = (TLTask*) calloc(1, sizeof(TLTask));
	if (!task) {
		LOGE("tl_add_task: task == NULL");
		return -1;
	}

	// init task
	task->abstime = abstime;
	task->taskFunc = taskFunc;
	task->taskdata = taskdata;

	// add to list
	pthread_mutex_lock(&hdl->listLock);
	if (hdl->tasklist) {
		task->next = hdl->tasklist;
	} else {
		task->next = NULL;
	}
	hdl->tasklist = task;
	
	// update minTask
	if (hdl->minTask) {
		if (task->abstime < hdl->minTask->abstime) {
			hdl->minTask = task;
		}
	} else {
		hdl->minTask = task;
	}
	pthread_mutex_unlock(&hdl->listLock);

	// trigger interrupt to re-calculate timeout time
	send_dummy_to_localpipe(hdl);

	return 0;
}

/*
	Add a new task to task list
*/
int tl_add_task(TaskListHandler* hdl,
			    int64_t timeout, // msec. relative time to invoke the callback function
			    TLTaskFunc taskFunc, // timeout callback function
			    void* taskdata) // data for func
{
	return tl_add_task_abstime(hdl,
							   timeout + (time(NULL) * 1000),
							   taskFunc,
							   taskdata);
}

/*
	Do function, itfunc, for each task in taslist
	
	Depend on itfunc() return value, it will do following action after task
		if itfunc() return 0(TL_IT_CONTINUE), continue task in list
		if itfunc() return -1(TL_IT_BREAK), break iterator task
		if itfunc() return 1(TL_IT_REMOVE), remove this task in list
		if itfunc() return 2(TL_IT_REMOVE_BREAK), remove this task and break
*/
int tl_iterator_task(TaskListHandler* hdl, TLIteratorFunc itfunc, void* itdata)
{
	int ret = 0;
	TLTask *task = NULL;
	TLTask *lastTask = NULL;
	TLTask *task2free = NULL;

	if (!itfunc) {
		return -1;
	}

	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		ret = itfunc(hdl, task->taskdata, itdata);
		if (ret == TL_IT_BREAK) {
			break;
		} else if (ret == TL_IT_REMOVE || ret == TL_IT_REMOVE_BREAK) {
			// remove task from list
			if (hdl->tasklist == task) { // first task
				hdl->tasklist = task->next;
			}
			if (lastTask) {
				lastTask->next = task->next;
			}
			task2free = task;
			task = task->next;
			// check minTask
			if (hdl->minTask == task2free) {
				update_min_task(hdl);
				send_dummy_to_localpipe(hdl); // send dummy to notify timeout change
			}
			// free task
			free(task2free);
			
			// break or not
			if (ret == TL_IT_REMOVE_BREAK) {
				break;
			} else {
				continue;
			}
		} else {
			// get next task
			lastTask = task;
			task = task->next;
		}
	}
	pthread_mutex_unlock(&hdl->listLock);
	return ret;
}

/*
	dump all tasks in tasklist
*/
int tl_dump_tasks(char* title, TaskListHandler* hdl, TLDumpFunc dumpFunc)
{
	struct TASK_DUMP_ST dumpst;

	if (title) LOGI("------ %s LIST START ------", title);
	else LOGI("------ DUMP TASK LIST START ------");

	dumpst.dumpFunc = dumpFunc;
	dumpst.count = 0;
	iterator_task(hdl, dump_task, &dumpst);
	
	if (title) LOGI("------ %s LIST END ------", title);
	else LOGI("------ DUMP TASK LIST END ------");

	return 0;
}

/*
	matchdata:
		the user define data in task
	if found, return the taskdata of task
	return NULL while not found
	
*/
void* tl_find_task(TaskListHandler* hdl, TLMatchFunc matchFunc, void* matchdata)
{
	int ret = 0;
	TLTask *task;

	if (!matchFunc) {
		return NULL;
	}
	
	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		ret = matchFunc(task->taskdata, matchdata);
		if (ret == TL_IT_MATCH) {
			pthread_mutex_unlock(&hdl->listLock);
			return task->taskdata;
		}
		task = task->next;
	}
	pthread_mutex_unlock(&hdl->listLock);
	return NULL;
}

/*
	matchdata:
		the user define data in task
	if found, return the taskdata of task and remove the task
	return NULL while not found
*/
void* tl_remove_task(TaskListHandler* hdl, TLMatchFunc matchFunc, void* matchdata)
{
	int ret = 0;
	TLTask *task, *lastTask = NULL;
	void *retdata = NULL;

	if (!matchFunc) {
		return NULL;
	}
	
	pthread_mutex_lock(&hdl->listLock);
	task = hdl->tasklist;
	while (task) {
		ret = matchFunc(task->taskdata, matchdata);
		if (ret == TL_IT_MATCH) {
			if (hdl->tasklist == task) { // first task
				hdl->tasklist = task->next;
			}
			if (lastTask) {
				lastTask->next = task->next;
			}
			retdata = task->taskdata;
			// check minTask
			if (hdl->minTask == task) {
				update_min_task(hdl);
				send_dummy_to_localpipe(hdl); // send dummy to notify timeout change
			}
			free(task); // free task item
			pthread_mutex_unlock(&hdl->listLock);
			return retdata;
		}
		lastTask = task;
		task = task->next;
	}
	pthread_mutex_unlock(&hdl->listLock);
	return NULL;
}

/*
	Refresh min task and recaculate the waiting timeout time
*/
void tl_refresh_loop(TaskListHandler* hdl)
{
	update_min_task(hdl);
	send_dummy_to_localpipe(hdl);
}
