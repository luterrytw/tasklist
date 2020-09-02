#define LOG_TAG "test"
#include "log.h"
#include "tasklist.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct TestDataST {
	int id;
	char str[64];
} TestData;

/*
	Function for test tl_add_task()
*/
static void* print_string(TaskListHandler* hdl, void *data)
{
	TestData* testdata = (TestData*) data;
	
	LOGI("print_string, string id=%d", testdata->id);
	snprintf(testdata->str, sizeof(testdata->str), "my task string addr is %p", testdata->str);

	LOGI("allocat_string, id=%d, str=[%s]", testdata->id, testdata->str);

	return NULL;
}

/*
	Function for test tl_iterator_task()
	
	Type: TLIteratorFunc

	data:
		the user defined data pass by tl_add_task()
	return TL_IT_CONTINUE for continue
*/
static int change_data_id(TaskListHandler* hdl, void* data, void* itdata)
{
	TestData* testdata = (TestData*) data;
	testdata->id += 10;
	return TL_IT_CONTINUE;
}

/*
	Function for test tl_iterator_task()
	
	Type: TLIteratorFunc

	data:
		the user defined data pass by tl_add_task()
	return TL_IT_CONTINUE for continue
	return TL_IT_REMOVE for removing
	return TL_IT_REMOVE_BREAK for removing task and break
*/
static int remove_specific_data_id(TaskListHandler* hdl, void* data, void* itdata)
{
	TestData* testdata = (TestData*) data;
	int* id = (int*) itdata;
	if (testdata->id >= *id) {
		return TL_IT_REMOVE_BREAK; // TL_IT_REMOVE;
	}
	return TL_IT_CONTINUE;
}

/*
	Type: TLTaskFunc

	data:
		the user defined data pass by tl_add_task()

	return the string that want to print
	return NULL for print nothing
*/
static char* dump_my_data(void* data, char* strBuf, int strBufLen)
{
	TestData* testdata = (TestData*) data;
	if (!testdata) {
		return NULL;
	}
	snprintf(strBuf, strBufLen, "task id = %d", testdata->id);
	
	return strBuf;
}

/*
	matchdata:
		the matchdata of tl_find_tasks()
	teskdata:
		the testdata of each task in list
*/
static int match_my_data(void* matchdata, void* teskdata)
{
	TestData* data1 = (TestData*) matchdata;
	TestData* data2 = (TestData*) teskdata;
	
	if (data1->id == data2->id) {
		return TL_IT_MATCH;
	}
	return TL_IT_NOT_MATCH;
}

int main()
{
	TestData data[4];
	TestData matchdata;
	TestData* founddata;
	TaskListHandler* hdl = tl_create_handler(19966);
	tl_start_task_loop_thread(hdl);
	
	memset(data, 0, sizeof(data));
	data[0].id = 10;
	data[1].id = 11;
	data[2].id = 12;
	data[3].id = 13;

	tl_add_task(hdl,
			    2000, // msec. time to invoke the callback function
			    print_string, // timeout callback function
			    &data[0]);

	tl_add_task(hdl,
			    2000, // msec. time to invoke the callback function
			    print_string, // timeout callback function
			    &data[1]);
				
	tl_add_task(hdl,
			    2000, // msec. time to invoke the callback function
			    print_string, // timeout callback function
			    &data[2]);

	tl_add_task(hdl,
			    2000, // msec. time to invoke the callback function
			    print_string, // timeout callback function
			    &data[3]);
	
	// dump task
	tl_dump_tasks("dump task", hdl, dump_my_data);
	
	// add id+10 and dump tasks
	tl_iterator_task(hdl, change_data_id, NULL);
	tl_dump_tasks("add id+10 and dump tasks", hdl, dump_my_data);
	
	// remove specified task id
	int removeId = 22;
	LOGI("remove task id>=%d", removeId);
	tl_iterator_task(hdl, remove_specific_data_id, &removeId);
	tl_dump_tasks("remove task id by tl_iterator_task()", hdl, dump_my_data);
	
	// try tl_find_tasks
	LOGI("found id == 22");
	matchdata.id = 22;
	founddata = tl_find_task(hdl, match_my_data, &matchdata);
	LOGI("founddata=%p, data=%p, %p, %p, %p", founddata, &data[0], &data[1], &data[2], &data[3]);
	
	LOGI("found & remove id == 22");
	founddata = tl_remove_task(hdl, match_my_data, &matchdata);
	LOGI("founddata=%p, data=%p, %p, %p, %p", founddata, &data[0], &data[1], &data[2], &data[3]);
	
	tl_dump_tasks("remove task id by tl_find_task_and_remove()", hdl, dump_my_data);
	
	sleep(4);
	tl_release_handler(hdl);
	
	uninit_log();
	
	return 0;
}