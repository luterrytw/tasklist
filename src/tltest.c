#define LOG_TAG "test"
#include "log.h"
#include "tasklist.h"
#include "listutil.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct TestDataST {
    int id;
    char str[64];
} TestData;

TestData testdata[4];

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

static int lucb_change_data_id(LUHandler* hdl, void* data, void* itdata)
{
    TestData* testdata = (TestData*) data;
    testdata->id += 10;
    return LU_IT_CONTINUE;
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
    if (testdata->id == *id) {
        return TL_IT_REMOVE_BREAK; // TL_IT_REMOVE;
    }
    return TL_IT_CONTINUE;
}

static int lucb_remove_specific_data_id(LUHandler* hdl, void* data, void* itdata)
{
    TestData* testdata = (TestData*) data;
    int* id = (int*) itdata;
    if (testdata->id == *id) {
        return LU_IT_REMOVE_BREAK; // TL_IT_REMOVE;
    }
    return LU_IT_CONTINUE;
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

static int lucb_match_my_data(void* matchdata, void* teskdata)
{
    TestData* data1 = (TestData*) matchdata;
    TestData* data2 = (TestData*) teskdata;

    if (data1->id == data2->id) {
        return LU_IT_MATCH;
    }
    return LU_IT_NOT_MATCH;
}

static int lucb_remove_all(LUHandler* hdl, void* data, void* itdata)
{
    return LU_IT_REMOVE;
}

void* thread_unlock_pop_block(void* args)
{
	LUHandler* stack = (LUHandler*) args;
	
	LOGI("thread_unlock_pop_block enter, wait for 3sec...");
	sleep(3);
	LOGI("thread_unlock_pop_block, push data id=22");
	lu_push(stack, &testdata[2]);
	
	return NULL;
}

void* thread_unlock_dequeue_block(void* args)
{
	LUHandler* queue = (LUHandler*) args;
	
	LOGI("thread_unlock_dequeue_block enter, wait for 3sec...");
	sleep(3);
	LOGI("thread_unlock_dequeue_block, enqueue data id=20");
	lu_enqueue(queue, &testdata[0]);
	
	return NULL;
}

/*
int main()
{
    TestData matchdata;
    TestData* founddata;
	pthread_t thread_hdl;
    LUHandler* list = lu_create_list(LU_TYPE_BLOCK);

    memset(testdata, 0, sizeof(testdata));
    testdata[0].id = 10;
    testdata[1].id = 11;
    testdata[2].id = 12;
    testdata[3].id = 13;

    //////////////////////////////////////////////////////////////
    // Try List
    //////////////////////////////////////////////////////////////
    LOGI("------------- Try List -----------");
    lu_add(list, &testdata[0]);
    lu_add(list, &testdata[1]);
    lu_add(list, &testdata[2]);
    lu_add(list, &testdata[3]);

    lu_dump_list("init list", list, dump_my_data);

    // add id+10 and dump tasks
    lu_iterator(list, lucb_change_data_id, NULL);
    lu_dump_list("add id+10 and dump tasks", list, dump_my_data);

    // remove specified task id
    int removeId = 21;
    lu_iterator(list, lucb_remove_specific_data_id, &removeId);
    lu_dump_list("remove task id by lu_iterator(id=21)", list, dump_my_data);

    // try lu_find
    LOGI("found id == 22");
    matchdata.id = 22;
    founddata = lu_find(list, lucb_match_my_data, &matchdata);
    LOGI("founddata=%p, data=%p, %p, %p, %p", founddata, &testdata[0], &testdata[1], &testdata[2], &testdata[3]);

    // try lu_remove
    matchdata.id = 20;
    founddata = lu_remove(list, lucb_match_my_data, &matchdata);
    LOGI("lu_remove(), founddata.id=%d", founddata->id);
    lu_dump_list("remove task id by lu_remove(id=20)", list, dump_my_data);

    //////////////////////////////////////////////////////////////
    // Try Queue
    //////////////////////////////////////////////////////////////
    LOGI("------------- Try Queue -----------");
    // try lu_enqueue
    LOGI("run lu_enqueue(id=20)");
    lu_enqueue(list, &testdata[0]);
    lu_dump_list("after lu_enqueue()", list, dump_my_data);

    // try lu_dequeue
    LOGI("run lu_dequeue()");
    founddata = lu_dequeue(list);
    if (founddata) {
        LOGI("lu_dequeue(), founddata.id=%d", founddata->id);
        lu_dump_list("after lu_dequeue()", list, dump_my_data);
    }

    //////////////////////////////////////////////////////////////
    // Try Queue Block
    //////////////////////////////////////////////////////////////
	LOGI("------------- Try Queue Block -----------");
	lu_dequeue(list);
	lu_dequeue(list);
	pthread_create(&thread_hdl, NULL, thread_unlock_dequeue_block, list);
	founddata = lu_dequeue(list);
    if (founddata) {
        LOGI("lu_dequeue(), founddata.id=%d", founddata->id);
        lu_dump_list("try queue block", list, dump_my_data);
    }

    //////////////////////////////////////////////////////////////
    // Try Stack
    //////////////////////////////////////////////////////////////
    LOGI("------------- Try Stack -----------");
    lu_push(list, &testdata[1]);
    lu_dump_list("after lu_push(21)", list, dump_my_data);

    LOGI("run lu_pop()");
    founddata = lu_pop(list);
    if (founddata) {
        LOGI("lu_pop(), founddata.id=%d", founddata->id);
        lu_dump_list("after lu_pop()", list, dump_my_data);
    }

    //////////////////////////////////////////////////////////////
    // Try Stack Block
    //////////////////////////////////////////////////////////////
	LOGI("------------- Try Stack Block -----------");
	pthread_create(&thread_hdl, NULL, thread_unlock_pop_block, list);
	founddata = lu_pop(list);
    if (founddata) {
        LOGI("lu_pop(), founddata.id=%d", founddata->id);
        lu_dump_list("try Block 3", list, dump_my_data);
    }
	
	
    //////////////////////////////////////////////////////////////
    // Try Release
    //////////////////////////////////////////////////////////////
    LOGI("remove all by lu_iterator()");
    lu_iterator(list, lucb_remove_all, NULL);
    lu_dump_list("remove all lu_iterator()", list, dump_my_data);

    lu_release_list(list);
}
*/

int main()
{
    TestData testdata[4];
    TestData matchdata;
    TestData* founddata;
    TaskListHandler* hdl = tl_create_handler(19966);
    tl_start_task_loop_thread(hdl);

    memset(testdata, 0, sizeof(testdata));
    testdata[0].id = 10;
    testdata[1].id = 11;
    testdata[2].id = 12;
    testdata[3].id = 13;

    tl_add_task(hdl,
                2000, // msec. time to invoke the callback function
                print_string, // timeout callback function
                &testdata[0]);

    tl_add_task(hdl,
                3000, // msec. time to invoke the callback function
                print_string, // timeout callback function
                &testdata[1]);

    tl_add_task(hdl,
                4000, // msec. time to invoke the callback function
                print_string, // timeout callback function
                &testdata[2]);

    tl_add_task(hdl,
                5000, // msec. time to invoke the callback function
                print_string, // timeout callback function
                &testdata[3]);

    // dump task
    tl_dump_tasks("dump task", hdl, dump_my_data);

    // add id+10 and dump tasks
    tl_iterator_task(hdl, change_data_id, NULL);
    tl_dump_tasks("add id+10 and dump tasks", hdl, dump_my_data);

    // remove specified task id
    int removeId = 20;
    LOGI("remove task id==%d", removeId);
    tl_iterator_task(hdl, remove_specific_data_id, &removeId);
    tl_dump_tasks("remove task id by tl_iterator_task()", hdl, dump_my_data);

    // try tl_find_tasks
    LOGI("found id == 21");
    matchdata.id = 21;
    founddata = tl_find_task(hdl, match_my_data, &matchdata);
    LOGI("founddata=%p, testdata=%p, %p, %p, %p", founddata, &testdata[0], &testdata[1], &testdata[2], &testdata[3]);

    LOGI("found & remove id == 20");
    matchdata.id = 21;
    founddata = tl_remove_task(hdl, match_my_data, &matchdata);
    LOGI("founddata=%p, testdata=%p, %p, %p, %p", founddata, &testdata[0], &testdata[1], &testdata[2], &testdata[3]);

    tl_dump_tasks("remove task id by tl_remove_task()", hdl, dump_my_data);

    sleep(6);
    tl_release_handler(hdl);

    uninit_log();

    return 0;
}

