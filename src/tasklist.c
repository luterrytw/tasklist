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

#include "tasklist.h"

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
			return task;
		}
		lastTask = task;
		task = task->next;
	}
	return NULL;
}


static void do_task(TaskListHandler* hdl)
{
	int64_t timeoutTime = get_current_ms_time();
	TLTask* task;
	task = remove_timeout_task(hdl, timeoutTime);

	while (task) {
		pthread_mutex_unlock(&hdl->listLock); // unlock, so do_task can call tl_xxx function
		LOGD("do_task %p", task->taskFunc);

		if (task->taskFunc) {
			task->taskFunc(hdl, task->taskdata);
		}
		free(task); // free, since we have done the task
		
		pthread_mutex_lock(&hdl->listLock); // lock again, because 
		task = remove_timeout_task(hdl, timeoutTime);
	}
}

/*
	return minum task abstime time, if not found, return 36000
*/
static void get_next_timeout_time(TaskListHandler* hdl, struct timespec* ts)
{
	int64_t current = get_current_ms_time();
	int64_t abstime;

	ts->tv_sec = 2100000000; // 2036 year
	ts->tv_nsec = 0;
	if (!hdl->minTask) {
		return;
	}
	abstime = hdl->minTask->abstime;
	if (abstime <= current) {
		ts->tv_sec = 0;
		ts->tv_nsec = 0;
	} else {
		ts->tv_sec = abstime / 1000;
		ts->tv_nsec = (abstime % 1000) * 1000000;
	}
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
TaskListHandler* tl_create_handler()
{
	TaskListHandler* hdl = (TaskListHandler*) malloc(sizeof(TaskListHandler));
	if (!hdl)
		return NULL;
	
	memset(hdl, 0, sizeof(TaskListHandler));
	pthread_mutex_init(&hdl->listLock, NULL);
	pthread_cond_init(&hdl->listCond, NULL);

	return hdl;
}

void tl_release_handler(TaskListHandler* hdl)
{
	if (!hdl)
		return;
	tl_stop_task_loop_thread(hdl);
	release_all_task(hdl);
	pthread_mutex_destroy(&hdl->listLock);
	pthread_cond_destroy(&hdl->listCond);
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
	struct timespec ts;
	int ret;
	TaskListHandler* hdl = (TaskListHandler*) param;

	if (!hdl) return NULL;
	
	while (hdl->isRunning) {
		pthread_mutex_lock(&hdl->listLock);
		get_next_timeout_time(hdl, &ts);
		//LOGI("loop waiting, tv_sec=%ld, tv_nsec=%ld..............................", ts.tv_sec, ts.tv_nsec);
		ret = pthread_cond_timedwait(&hdl->listCond, &hdl->listLock, &ts);
		if (ret == ETIMEDOUT) {
			do_task(hdl);
		}
		// else change notify
		pthread_mutex_unlock(&hdl->listLock);
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
	pthread_mutex_lock(&hdl->listLock);
	hdl->isRunning = 0;
	pthread_cond_signal(&hdl->listCond); // trigger interrupt to end
	pthread_mutex_unlock(&hdl->listLock);
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
	// trigger interrupt to re-calculate timeout time
	pthread_cond_signal(&hdl->listCond);
	pthread_mutex_unlock(&hdl->listLock);

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
				pthread_cond_signal(&hdl->listCond); // trigger interrupt to re-calculate timeout time
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
				pthread_cond_signal(&hdl->listCond); // trigger interrupt to re-calculate timeout time
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
	pthread_mutex_lock(&hdl->listLock);
	update_min_task(hdl);
	pthread_cond_signal(&hdl->listCond); // trigger interrupt to re-calculate timeout time
	pthread_mutex_unlock(&hdl->listLock);
}
