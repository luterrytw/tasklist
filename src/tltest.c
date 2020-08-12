#define LOG_TAG "test"
#include "log.h"
#include "tasklist.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct TestDataST {
	int id;
	char *str;
} TestData;

/*
	Function for test tl_add_task()
*/
static void* allocate_string(void *data)
{
	TestData* testdata = (TestData*) data;
	
	LOGI("allocate_string, string id=%d", testdata->id);
	testdata->str = malloc(64);
	snprintf(testdata->str, 64, "my task string addr is %p", testdata->str);

	LOGI("allocat_string, id=%d, str=[%s]", testdata->id, testdata->str);
	

	return NULL;
}

/*
	Function for test tl_add_task()
*/
static void* free_string(void *data)
{
	TestData* testdata = (TestData*) data;

	LOGI("free_string, string id=%d", testdata->id);
	if (testdata->str) {
		free(testdata->str);
	}

	return NULL;
}

/*
	Function for test tl_iterator_task()
	
	Type: TLIteratorFunc

	data:
		the user defined data pass by tl_add_task()
	return TL_IT_CONTINUE for continue
*/
static int change_data_id(void* data, void* itdata)
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
static int remove_specific_data_id(void* data, void* itdata)
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
static void* dump_my_data(void* data)
{
	static char strBuf[1024];

	TestData* testdata = (TestData*) data;
	if (!testdata) {
		return NULL;
	}
	snprintf(strBuf, sizeof(strBuf), "task id = %d", testdata->id);
	
	return strBuf;
}

int main()
{
	TestData data[3];
	TaskListHandler* hdl = tl_create_handler(19966);
	tl_start_task_loop_thread(hdl);
	
	memset(data, 0, sizeof(data));
	data[0].id = 10;
	data[1].id = 11;
	data[2].id = 12;

	tl_add_task(hdl,
			    1000, // msec. time to invoke the callback function
			    allocate_string, // timeout callback function
				free_string, // function to free data in this task
			    &data[0]);

	tl_add_task(hdl,
			    2000, // msec. time to invoke the callback function
			    allocate_string, // timeout callback function
				free_string, // function to free data in this task
			    &data[1]);
				
	tl_add_task(hdl,
			    3000, // msec. time to invoke the callback function
			    allocate_string, // timeout callback function
				free_string, // function to free data in this task
			    &data[2]);
	
	// dump task
	tl_dump_tasks(hdl, dump_my_data, "dump task");
	
	// add id+10 and dump tasks
	tl_iterator_task(hdl, change_data_id, NULL);
	tl_dump_tasks(hdl, dump_my_data, "add id+10 and dump tasks");
	
	// remove task id
	int removeId = 21;
	LOGI("remove task id>=%d", removeId);
	tl_iterator_task(hdl, remove_specific_data_id, &removeId);
	tl_dump_tasks(hdl, dump_my_data, "remove task id");
	
	sleep(5);

	tl_release_handler(hdl);
	
	uninit_log();
	
	return 0;
}