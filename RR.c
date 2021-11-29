/*
 * class	: Operating System(MS)
 * Project 01	: Round Robin Scheduling Simulation
 * Author	: jaeil Park, junseok Tak
 * Student ID	: 32161786, 32164809
 * Date		: 2021-11-16
 * Professor	: seehwan Yoo
 * Left freedays: 2
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <malloc.h>

#define MAX_PROCESS 10
#define TIME_TICK 1000// 0.01 second(10ms).
#define TIME_QUANTUM 5// 0.03 seconds(30ms).

 //////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Node {
	struct Node* next;
	int procNum;
	int pid;
	int cpuTime;
	int ioTime;
} Node;

typedef struct NodeList {
	Node* head;
	Node* tail;
	int ListSize;
} NodeList;

typedef struct Table {
	int* ValidBit;
	int* SwapBit;
	int LV2occupyBit;
	int* Adr;
	int* TbAdr;
} Table;

struct data_iocpu {
	int pid;
	int cpuTime;
	int ioTime;
};

struct data_memaccess {
	int VAadr[10];
	//int procNum;
};

// message buffer that contains child process's data.
struct msgbuf_iocpu {
	long mtype;
	struct data_iocpu mdata;
};

struct msgbuf_memaccess {
	long mtype;
	struct data_memaccess mdata;
};

void InitNodeList(NodeList* list);
void pushBackNode(NodeList* list, int procNum, int cpuTime, int ioTime);
void popFrontNode(NodeList* list, Node* runNode);
bool isEmptyList(NodeList* list);
void Delnode(NodeList* list);

void writeNode(NodeList* readyQueue, NodeList* waitQueue, Node* cpuRunNode, FILE* wfp);
void signal_timeTick(int signo);
void signal_RRcpuSchedOut(int signo);
void signal_ioSchedIn(int signo);
void cmsgSnd_iocpu(int key, int cpuBurstTime, int ioBurstTime);
void pmsgRcv_iocpu(int curProc, Node* nodePtr);

//Project 2 Code
void cmsgSnd_memaccess(int procNum, int* VAadr);
void pmsgRcv_memaccess(int procNum, int* VAbuffer);
int MemAccess(int* VAadr, int procNum);
int FindFreeLV2(Table* LV2Table);
int FindFreeFrame(int* MemFreeFrameList, int option);
int FindLRUPage(int* List);
void PageCopy(int* target1, int target1num, int* target1list, int* target2, int target2num, int* target2list);

NodeList* waitQueue;
NodeList* readyQueue;
NodeList* subReadyQueue;
Node* cpuRunNode;
Node* ioRunNode;
FILE* rfp;
FILE* wfp;

int CPID[MAX_PROCESS];// child process pid.
int KEY[MAX_PROCESS];// key value for message queue.
int CONST_TICK_COUNT;
int TICK_COUNT;
int RUN_TIME;

//Project 2 Code
int* Memory;
int* Disk;
int* MemFreeFrameList;
int MemFreeFrameListSize;
int* DiskFreeFrameList;
Table* LV1Table;
Table* LV2Table;

//////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[]) {
	int originCpuBurstTime[3000];
	int originIoBurstTime[3000];
	int ppid = getpid();// get parent process id.

	//Project 2 Code
	//Total Memory 1MB, PageSize = 1KB(5bits - LV1table, 5bits - LV2table, 10bits - Offset)
	//Total Disk 1MB, PageSize = 1KB
	//Mem(Disk)FreePageList = 1MB/1KB = 1024(0x400)
	Memory = (int*)malloc(sizeof(int) * 0x40000);						//1MB = 32bits(4byte) * 0x4:0000
	Disk = (int*)malloc(sizeof(int) * 0x40000);
	MemFreeFrameList = (int*)malloc(sizeof(int) * 0x400);				//Free page frame list : 1MB = 1KB * 0x400
	DiskFreeFrameList = (int*)malloc(sizeof(int) * 0x400);
	MemFreeFrameListSize = 0x400;										//# of left Free page frame


	memset(MemFreeFrameList, 0, malloc_usable_size(MemFreeFrameList));
	memset(DiskFreeFrameList, 0, malloc_usable_size(DiskFreeFrameList));
	memset(Memory, 0, malloc_usable_size(Memory));
	memset(Disk, 0, malloc_usable_size(Disk));
	LV1Table = (Table*)malloc(sizeof(Table) * 0xA);					//LV1 Table = 10 Process
	LV2Table = (Table*)malloc(sizeof(Table) * 0xA * 0x400);			//LV2 Table = 10 * 2^10 <10(proc) * 2^5(LV1) * 2^5(LV2)>

	for (int i = 0; i < 10; i++) {									//LV1 Table Setting(5bits)
		LV1Table[i].TbAdr = malloc(sizeof(int) * 0x20);				//32(0x20)
		LV1Table[i].ValidBit = malloc(sizeof(int) * 0x20);
		for (int t = 0; t < 0x20; t++) {							//LV1 Table Variable Setting
			LV1Table[i].TbAdr[t] = 0;
			LV1Table[i].ValidBit[t] = 0;
		}
	}
	for (int i = 0; i < 0x2800; i++) {								//LV2 Table Setting(5bits)
		LV2Table[i].Adr = malloc(sizeof(int) * 0x20);
		LV2Table[i].SwapBit = malloc(sizeof(int) * 0x20);
		LV2Table[i].ValidBit = malloc(sizeof(int) * 0x20);
		LV2Table[i].LV2occupyBit = 0;
		for (int t = 0; t < 0x20; t++) {							//LV2 Table Variable Setting
			LV2Table[i].Adr[t] = 0;
			LV2Table[i].ValidBit[t] = 0;
			LV2Table[i].SwapBit[t] = 0;
		}
	}
	
	struct itimerval new_itimer;
	struct itimerval old_itimer;

	new_itimer.it_interval.tv_sec = 0;
	new_itimer.it_interval.tv_usec = TIME_TICK;
	new_itimer.it_value.tv_sec = 1;
	new_itimer.it_value.tv_usec = 0;

	struct sigaction tick;
	struct sigaction cpu;
	struct sigaction io;

	memset(&tick, 0, sizeof(tick));
	memset(&cpu, 0, sizeof(cpu));
	memset(&io, 0, sizeof(io));

	tick.sa_handler = &signal_timeTick;
	cpu.sa_handler = &signal_RRcpuSchedOut;
	io.sa_handler = &signal_ioSchedIn;

	sigaction(SIGALRM, &tick, NULL);
	sigaction(SIGUSR1, &cpu, NULL);
	sigaction(SIGUSR2, &io, NULL);

	waitQueue = malloc(sizeof(NodeList));
	readyQueue = malloc(sizeof(NodeList));
	subReadyQueue = malloc(sizeof(NodeList));
	cpuRunNode = malloc(sizeof(Node));
	ioRunNode = malloc(sizeof(Node));

	if (waitQueue == NULL || readyQueue == NULL || subReadyQueue == NULL) {
		perror("list malloc error");
		exit(EXIT_FAILURE);
	}
	if (cpuRunNode == NULL || ioRunNode == NULL) {
		perror("node malloc error");
		exit(EXIT_FAILURE);
	}

	// initialize ready, sub-ready, wait queues.
	InitNodeList(waitQueue);
	InitNodeList(readyQueue);
	InitNodeList(subReadyQueue);

	wfp = fopen("RR_schedule_dump.txt", "w");
	if (wfp == NULL) {
		perror("file open error");
		exit(EXIT_FAILURE);
	}
	fclose(wfp);

	CONST_TICK_COUNT = 0;
	TICK_COUNT = 0;
	RUN_TIME = 0;

	// create message queue key.
	for (int innerLoopIndex = 0; innerLoopIndex < MAX_PROCESS; innerLoopIndex++) {
		KEY[innerLoopIndex] = 0x6123 * (innerLoopIndex + 1);
		msgctl(msgget(KEY[innerLoopIndex], IPC_CREAT | 0666), IPC_RMID, NULL);
		KEY[innerLoopIndex] = 0x3216 * (innerLoopIndex + 1);
		msgctl(msgget(KEY[innerLoopIndex], IPC_CREAT | 0666), IPC_RMID, NULL);
	}

	// handle main function arguments.
	if (argc == 1 || argc == 2) {
		printf("COMMAND <TEXT FILE> <RUN TIME(sec)>\n");
		printf("./RR.o time_set.txt 10\n");
		exit(EXIT_SUCCESS);
	}
	else {
		// open time_set.txt file.
		rfp = fopen((char*)argv[1], "r");
		if (rfp == NULL) {
			perror("file open error");
			exit(EXIT_FAILURE);
		}

		int preCpuTime;
		int preIoTime;

		// read time_set.txt file.
		for (int innerLoopIndex = 0; innerLoopIndex < 3000; innerLoopIndex++) {
			if (fscanf(rfp, "%d , %d", &preCpuTime, &preIoTime) == EOF) {
				printf("fscanf error");
				exit(EXIT_FAILURE);
			}
			originCpuBurstTime[innerLoopIndex] = preCpuTime;
			originIoBurstTime[innerLoopIndex] = preIoTime;
		}
		// set program run time.
		RUN_TIME = atoi(argv[2]);
		RUN_TIME = RUN_TIME * 1000000 / TIME_TICK;
	}
	printf("\x1b[33m");
	printf("TIME TICK   PROC NUMBER   REMAINED CPU TIME\n");
	printf("\x1b[0m");

	//////////////////////////////////////////////////////////////////////////////////////////////////

	for (int outerLoopIndex = 0; outerLoopIndex < MAX_PROCESS; outerLoopIndex++) {
		// create child process.
		int ret = fork();

		// parent process part.
		if (ret > 0) {
			CPID[outerLoopIndex] = ret;
			pushBackNode(readyQueue, outerLoopIndex, originCpuBurstTime[outerLoopIndex], originIoBurstTime[outerLoopIndex]);
		}

		// child process part.
		else {
			int BurstCycle = 1;
			int procNum = outerLoopIndex;
			int cpuBurstTime = originCpuBurstTime[procNum];
			int ioBurstTime = originIoBurstTime[procNum];
			int VAadr[10];

			// child process waits until a tick happens.
			kill(getpid(), SIGSTOP);

			// cpu burst part.
			while (true) {
				cpuBurstTime--;// decrease cpu burst time by 1.
				printf("            %02d            %02d\n", procNum, cpuBurstTime);
				printf("───────────────────────────────────────────\n");
				for (int i = 0; i < 10; i++) {
					srand(time(NULL) + (CONST_TICK_COUNT<<(i*2)) + i);						//time변수 시드 초기화가 1초라서 cpuBurstTime과 i를 더해서 랜덤하게 만듬
					VAadr[i] = rand() % 0x40000;
					//printf("rand value %d\n", VAadr[i]);
				}

				// cpu task is over.
				if (cpuBurstTime == 0) {
					cpuBurstTime = originCpuBurstTime[procNum + (BurstCycle * 10)];	// set the next cpu burst time.

					// send the data of child process to parent process.
					cmsgSnd_iocpu(KEY[procNum], cpuBurstTime, ioBurstTime);
					ioBurstTime = originIoBurstTime[procNum + (BurstCycle * 10)];	// set the next io burst time.

					BurstCycle++;
					if (BurstCycle > 298)
					{
						BurstCycle = 1;
					}
					cmsgSnd_memaccess(procNum, VAadr);
					kill(ppid, SIGUSR2);
				}
				// cpu task is not over.
				else {
					cmsgSnd_memaccess(procNum, VAadr);
					kill(ppid, SIGUSR1);
				}
				// child process waits until the next tick happens.
				kill(getpid(), SIGSTOP);
			}
		}
	}

	// get the first node from ready queue.
	popFrontNode(readyQueue, cpuRunNode);
	setitimer(ITIMER_REAL, &new_itimer, &old_itimer);

	// parent process excutes until the run time is over.
	while (RUN_TIME != 0);
	for (int innerLoopIndex = 0; innerLoopIndex < MAX_PROCESS; innerLoopIndex++) {
		msgctl(msgget(KEY[innerLoopIndex], IPC_CREAT | 0666), IPC_RMID, NULL);
		KEY[innerLoopIndex] = 6123 * (innerLoopIndex + 1);
		msgctl(msgget(KEY[innerLoopIndex], IPC_CREAT | 0666), IPC_RMID, NULL);
		kill(CPID[innerLoopIndex], SIGKILL);
	}
	writeNode(readyQueue, waitQueue, cpuRunNode, wfp);// write ready, wait queue dump to txt file.
	// remove message queues and terminate child processes.


	// free dynamic memory allocation.
	Delnode(readyQueue);
	Delnode(subReadyQueue);
	Delnode(waitQueue);
	free(readyQueue);
	free(subReadyQueue);
	free(waitQueue);

	free(cpuRunNode);
	free(ioRunNode);

	for (int i = 0; i < 10; i++) {
		free(LV1Table[i].TbAdr);
		free(LV1Table[i].ValidBit);
	}
	for (int i = 0; i < 0x2800; i++) {
		free(LV2Table[i].Adr);
		free(LV2Table[i].SwapBit);
		free(LV2Table[i].ValidBit);
	}
	free(LV1Table);
	free(LV2Table);
	free(Memory);
	free(Disk);
	free(MemFreeFrameList);
	free(DiskFreeFrameList);
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

/*
* void signal_timeTick(int signo);
*   This function is signal handler of SIGALARM which is called every time tick.
*
* parameter: int
*   signo:
*
* return: none.
*/
void signal_timeTick(int signo) {								//SIGALRM
	if (RUN_TIME == 0) {
		return;
	}
	writeNode(readyQueue, waitQueue, cpuRunNode, wfp);			// write ready, wait queue dump to txt file.
	CONST_TICK_COUNT++;
	printf("%05d       PROC NUMBER   REMAINED CPU TIME\n", CONST_TICK_COUNT);

	// io burst part.
	Node* NodePtr = waitQueue->head;
	int waitQueueSize = 0;

	// get the size of wait queue.
	while (NodePtr != NULL) {
		NodePtr = NodePtr->next;
		waitQueueSize++;
	}

	for (int i = 0; i < waitQueueSize; i++) {
		popFrontNode(waitQueue, ioRunNode);
		ioRunNode->ioTime--;// decrease io time by 1.

		// io task is over, then push node to ready queue.
		if (ioRunNode->ioTime == 0) {
			pushBackNode(readyQueue, ioRunNode->procNum, ioRunNode->cpuTime, ioRunNode->ioTime);
		}
		// io task is not over, then push node to wait queue again.
		else {
			pushBackNode(waitQueue, ioRunNode->procNum, ioRunNode->cpuTime, ioRunNode->ioTime);
		}
	}

	// cpu burst part.
	if (cpuRunNode->procNum != -1) {
		kill(CPID[cpuRunNode->procNum], SIGCONT);
	}


	RUN_TIME--;// run time decreased by 1.
	return;
}

/*
* void signal_RRcpuSchedOut(int signo);
*   This function pushes the current cpu preemptive process to the end of the ready queue,
*	and pop the next process from the ready queue to excute cpu task.
*
* parameter: int
*	signo:
*
* return: none.
*/
void signal_RRcpuSchedOut(int signo) {							//SIGUSR1
	int VAbuffer[10];
	TICK_COUNT++;
	//Memory Process
	pmsgRcv_memaccess(cpuRunNode->procNum, VAbuffer);
	MemAccess(VAbuffer, cpuRunNode->procNum);

	// scheduler changes cpu preemptive process at every time quantum.
	if (TICK_COUNT >= TIME_QUANTUM) {
		pushBackNode(readyQueue, cpuRunNode->procNum, cpuRunNode->cpuTime, cpuRunNode->ioTime);

		// pop the next process from the ready queue.
		popFrontNode(readyQueue, cpuRunNode);
		TICK_COUNT = 0;
	}
	return;
}

/*
* void signal_ioSchedIn(int signo);
*   This function checks the child process whether it has io tasks or not,
*	and pushes the current cpu preemptive process to the end of the ready queue or wait queue.
*	Then, pop the next process from the ready queue to excute cpu task.
*
* parameter: int
*	signo:
*
* return: none.
*/
void signal_ioSchedIn(int signo) {								//SIGUSR2
	int VAbuffer[10];
	pmsgRcv_iocpu(cpuRunNode->procNum, cpuRunNode);
	//Memory Process
	pmsgRcv_memaccess(cpuRunNode->procNum, VAbuffer);
	MemAccess(VAbuffer, cpuRunNode->procNum);

	// process that has no io task go to the end of the ready queue.
	if (cpuRunNode->ioTime == 0) {
		pushBackNode(readyQueue, cpuRunNode->procNum, cpuRunNode->cpuTime, cpuRunNode->ioTime);
	}
	// process that has io task go to the end of the wait queue.
	else {
		pushBackNode(waitQueue, cpuRunNode->procNum, cpuRunNode->cpuTime, cpuRunNode->ioTime);
	}

	// pop the next process from the ready queue.
	popFrontNode(readyQueue, cpuRunNode);
	TICK_COUNT = 0;
	return;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

/*
* void initList(List* list);
*   This function initializes the list to a null value.
*
* parameter: List*
*	list: the list which has to be initialized.
*
* return: none.
*/
void InitNodeList(NodeList* list) {
	list->head = NULL;
	list->tail = NULL;
	list->ListSize = 0;
	return;
}

/*
* void pushBackNode(List* list, int procNum, int cpuTime, int ioTime);
*   This function creates a new node and pushes to the end of the list.
*
* parameter: List*, int, int, int
*	list: the list that the new node will be pushed.
*	procNum: the index of the process.
*	cpuTime: the cpu burst time of the process.
*	ioTime: the io burst time of the process.
*
* return: none.
*/
void pushBackNode(NodeList* list, int procNum, int cpuTime, int ioTime) {
	Node* newNode = (Node*)malloc(sizeof(Node));
	if (newNode == NULL) {
		perror("push node malloc error");
		exit(EXIT_FAILURE);
	}

	newNode->next = NULL;
	newNode->procNum = procNum;
	newNode->cpuTime = cpuTime;
	newNode->ioTime = ioTime;

	// the first node case.
	if (list->head == NULL) {
		list->head = newNode;
		list->tail = newNode;
	}
	// another node cases.
	else {
		list->tail->next = newNode;
		list->tail = newNode;
	}
	return;
}

/*
* void popFrontNode(List* list, Node* runNode);
*   This function pops the front node from the list.
*
* parameter: List*, Node*
*	list: the list that the front node will be poped.
*	runNode: the node pointer that pointed the poped node.
*
* return: none.
*/
void popFrontNode(NodeList* list, Node* runNode) {
	Node* oldNode = list->head;

	// empty list case.
	if (isEmptyList(list) == true) {
		runNode->cpuTime = -1;
		runNode->ioTime = -1;
		runNode->procNum = -1;
		return;
	}

	// pop the last node from a list case.
	if (list->head->next == NULL) {
		list->head = NULL;
		list->tail = NULL;
	}
	else {
		list->head = list->head->next;
	}

	*runNode = *oldNode;
	free(oldNode);
	return;
}

/*
* bool isEmptyList(List* list);
*   This function checks whether the list is empty or not.
*
* parameter: List*
*	list: the list to check if it's empty or not.
*
* return: bool
*	true: the list is empty.
*	false: the list is not empty.
*/
bool isEmptyList(NodeList* list) {
	if (list->head == NULL)
		return true;
	else
		return false;
}

void Delnode(NodeList* list) {
	while (isEmptyList(list) == false) {
		Node* delnode;
		delnode = list->head;
		list->head = list->head->next;
		free(delnode);
		//printf("delete  node\n");
	}
}

/*
* void cmsgSnd(int key, int cpuBurstTime, int ioBurstTime)
*   This function is a function in which the child process puts data in the msg structure and sends it to the message queue.
*
* parameter: int, int, int
*	key: the key value of message queue.
*	cpuBurstTime: child process's cpu burst time.
*	ioBurstTime: child process's io burst time.
*
* return: none.
*/
void cmsgSnd_iocpu(int key, int cpuBurstTime, int ioBurstTime) {
	int qid = msgget(key, IPC_CREAT | 0666);// create message queue ID.

	struct msgbuf_iocpu msg;
	memset(&msg, 0, sizeof(msg));

	msg.mtype = 1;
	msg.mdata.pid = getpid();
	msg.mdata.cpuTime = cpuBurstTime;// child process cpu burst time.
	msg.mdata.ioTime = ioBurstTime;// child process io burst time.

	// child process sends its data to parent process.
	if (msgsnd(qid, (void*)&msg, sizeof(struct data_iocpu), 0) == -1) {
		perror("child msgsnd error");
		exit(EXIT_FAILURE);
	}
	return;
}

void cmsgSnd_memaccess(int procNum, int* VAadr) {
	int key = 0x6123 * (procNum + 1);
	int qid = msgget(key, IPC_CREAT | 0666);// create message queue ID.

	struct msgbuf_memaccess msg;
	memset(&msg, 0, sizeof(msg));

	msg.mtype = 1;
	//msg.mdata.procNum = procNum;

	//Memory Access Adr
	for (int i = 0; i < 10; i++) {
		msg.mdata.VAadr[i] = VAadr[i];
	}

	// child process sends its data to parent process.
	if (msgsnd(qid, (void*)&msg, sizeof(struct data_memaccess), 0) == -1) {
		perror("child msgsnd error");
		exit(EXIT_FAILURE);
	}
	return;
}

/*
* void pmsgRcv(int procNum, Node* nodePtr);
*   This function is a function in which the parent process receives data from the message queue and gets it from the msg structure.
*
* parameter: int, Node*
*	procNum: the index of current cpu or io running process.
*	nodePtr:
*
* return: none.
*/
void pmsgRcv_iocpu(int procNum, Node* nodePtr) {
	int key = 0x3216 * (procNum + 1);// create message queue key.
	int qid = msgget(key, IPC_CREAT | 0666);

	struct msgbuf_iocpu msg;
	memset(&msg, 0, sizeof(msg));

	// parent process receives child process data.
	if (msgrcv(qid, (void*)&msg, sizeof(msg), 0, 0) == -1) {
		perror("msgrcv error");
		exit(1);
	}

	// copy the data of child process to nodePtr.
	nodePtr->pid = msg.mdata.pid;
	nodePtr->cpuTime = msg.mdata.cpuTime;
	nodePtr->ioTime = msg.mdata.ioTime;
	return;
}

void pmsgRcv_memaccess(int procNum, int* VAbuffer) {
	int key = 0x6123 * (procNum + 1);// create message queue key.
	int qid = msgget(key, IPC_CREAT | 0666);

	struct msgbuf_memaccess msg;
	memset(&msg, 0, sizeof(msg));
	// parent process receives child process data.
	if (msgrcv(qid, (void*)&msg, sizeof(msg), 0, 0) == -1) {
		perror("msgrcv error");
		exit(1);
	}

	// copy the data of child process to nodePtr.
	for (int i = 0; i < 10; i++) {
		VAbuffer[i] = msg.mdata.VAadr[i];
		//printf("msg rnad val : %d\n", msg.mdata.VAadr[i]);
	}

	return;
}

/*
* void writeNode(List* readyQueue, List* waitQueue, Node* cpuRunNode, FILE* wfp);
*   This function write the ready queue dump and wait queue dump to scheduler_dump.txt file.
*
* parameter: List*, List*, Node*, FILE*
*   readyQueue: List pointer that points readyQueue List.
*	waitQueue: List pointer that points waitQueue List.
*	cpuRunNode: Node pointer that points cpuRunNode.
*	wfp: file pointer that points stream file.
*
* return: none.
*/
void writeNode(NodeList* readyQueue, NodeList* waitQueue, Node* cpuRunNode, FILE* wfp) {
	Node* nodePtr1 = readyQueue->head;
	Node* nodePtr2 = waitQueue->head;

	wfp = fopen("RR_schedule_dump.txt", "a+");// open stream file append+ mode.
	fprintf(wfp, "───────────────────────────────────────────────────────\n");
	fprintf(wfp, " TICK   %04d\n\n", CONST_TICK_COUNT);
	fprintf(wfp, " RUNNING PROCESS\n");
	fprintf(wfp, " %02d\n\n", cpuRunNode->procNum);
	fprintf(wfp, " READY QUEUE\n");

	if (nodePtr1 == NULL)
		fprintf(wfp, " none");
	while (nodePtr1 != NULL) {// write ready queue dump.
		fprintf(wfp, " %02d ", nodePtr1->procNum);
		nodePtr1 = nodePtr1->next;
	}

	fprintf(wfp, "\n\n");
	fprintf(wfp, " WAIT QUEUE\n");

	if (nodePtr2 == NULL)
		fprintf(wfp, " none");
	while (nodePtr2 != NULL) {// write wait queue dump.
		fprintf(wfp, " %02d ", nodePtr2->procNum);
		nodePtr2 = nodePtr2->next;
	}

	fprintf(wfp, "\n");
	fclose(wfp);
	return;
}

//Project2 Code Add Part
int MMU(int VAadr) {
	int PAadr;


}

int MemAccess(int* VAadr, int procNum) {
	int VAadrbuffer;
	int LV1buffer;
	int LV2buffer;
	int Offsetbuffer;
	int Memorybuffer;
	int LRUbuffer;
	int SwapDiskAdr;
	int LV2num;
	int FreeFramenum;
	
	for (int i = 0; i < 10; i++) {
		printf("test %d \n", MemFreeFrameListSize);
		if (MemFreeFrameListSize < 0x370) {									//Swapping (total 0x400 75% use -> swapping)
			printf("Swap Out\n");
			LRUbuffer = FindLRUPage(MemFreeFrameList);
			LV1buffer = (MemFreeFrameList[LRUbuffer] >> 27) & 0x1F;
			LV2num = LV1Table[procNum].TbAdr[LV1buffer];
			LV2buffer = (MemFreeFrameList[LRUbuffer] >> 22) & 0x1F;
			LV2Table[LV2num].SwapBit[LV2buffer] = 1;
			FreeFramenum = FindFreeFrame(DiskFreeFrameList, 1);
			PageCopy(Disk, FreeFramenum, DiskFreeFrameList, Memory, LRUbuffer, MemFreeFrameList);
			MemFreeFrameListSize++;
			printf("Data Move : Memory[0x%x - 0x%x] -> Disk[0x%x - 0x%x]\n", (LRUbuffer * 0x400), (((LRUbuffer + 1) * 0x400) - 1), (FreeFramenum * 0x400), (((FreeFramenum + 1) * 0x400) - 1));
		}
		VAadrbuffer = VAadr[i];
		printf("VAadr[%d] = 0x%x\n", i, VAadrbuffer);
		LV1buffer = (VAadrbuffer >> 15) & 0x1F;		//0x1F(5bits)
		LV2buffer = (VAadrbuffer >> 10) & 0x1F;		//0x1F(5bits)
		Offsetbuffer = VAadrbuffer & 0x3FF;			//0x3FF(10bits)

		if (LV1Table[procNum].ValidBit[LV1buffer] == 0) {			//LV1 Page fault -> alloc LV2 Table
			printf("LV1 Page fault\n");
			LV2num = FindFreeLV2(LV2Table);
			LV1Table[procNum].ValidBit[LV1buffer] = 1;
			LV1Table[procNum].TbAdr[LV1buffer] = LV2num;
		}
		else {
			printf("LV1 Page hit\n");
			LV2num = LV1Table[procNum].TbAdr[LV1buffer];
		}
		if (LV2Table[LV2num].ValidBit[LV2buffer] == 0) {
			printf("LV2 Page fault\n");
			FreeFramenum = FindFreeFrame(MemFreeFrameList, 0);
			LV2Table[LV2num].ValidBit[LV2buffer] = 1;
			LV2Table[LV2num].Adr[LV2buffer] = FreeFramenum;
		}
		else {
			printf("LV2 Page hit\n");
			if (LV2Table[LV2num].SwapBit[LV2buffer] == 1) {
				printf("Swap In\n");
				FreeFramenum = FindFreeFrame(MemFreeFrameList, 0);
				SwapDiskAdr = LV2Table[LV2num].Adr[LV2buffer];
				PageCopy(Memory, FreeFramenum, MemFreeFrameList, Disk, SwapDiskAdr, DiskFreeFrameList);
				printf("Data Move : Disk[0x%x - 0x%x] -> Memory[0x%x - 0x%x]\n", (SwapDiskAdr * 0x400), (((SwapDiskAdr + 1) * 0x400) - 1), (FreeFramenum * 0x400), (((FreeFramenum + 1) * 0x400) - 1));
				LV2Table[LV2num].SwapBit[LV2buffer] = 0;
				LV2Table[LV2num].Adr[LV2buffer] = FreeFramenum;
			}
			else {
				FreeFramenum = LV2Table[LV2num].Adr[LV2buffer];
			}
			//FreeFramePageList (5bits = LV1 info, 5bits = LV2 info, 21bits = LRU count bit, 1bit = Page Valid bit)
			MemFreeFrameList[FreeFramenum] += 0x2;																			//LRU bit +1
			MemFreeFrameList[FreeFramenum] += ((LV2num & 0x1F) << 27);
			MemFreeFrameList[FreeFramenum] += ((LV2buffer & 0x1f) << 22);													//0x1F(5bits)
		}
		printf("PAadr[%d] = 0x%x\n", i, ((FreeFramenum) * 0x400) + Offsetbuffer);

		//Memory Write Read Part
		//Memory [1bit = valid bit, 31bits = data]
		Memorybuffer = Memory[((FreeFramenum * 0x400) + Offsetbuffer) / 4];
		if (((Memorybuffer >> 31) & 0x1) == 0) {																			//No data is written to memory(valid bit = 0)
			Memory[((FreeFramenum * 0x400) + Offsetbuffer) / 4] = (CONST_TICK_COUNT + 0x80000000);							//0x8000:000 -> valid bit		
			printf("Data Write -> PAadr[0x%x] = %d\n\n", ((FreeFramenum) * 0x400) + Offsetbuffer, CONST_TICK_COUNT);		
		}
		else {																												//data is written to memory(valid bit = 1)
			printf("Data Read -> PAadr[0x%x] = %d\n\n", ((FreeFramenum) * 0x400) + Offsetbuffer, (Memorybuffer - 0x80000000));
			MemFreeFrameList[FreeFramenum] += 0x2;
		}
	}
	return 0;
}

int FindFreeLV2(Table* LV2Table) {
	int LV2num = 0;
	for (int i = 0; i < 0x2800; i++) {															//10240(0x2800)
		if (LV2Table[LV2num].LV2occupyBit == 0) {
			LV2Table[LV2num].LV2occupyBit = 1;
			break;
		}
		LV2num++;
	}
	return LV2num;
}

int FindFreeFrame(int* List, int option) {
	int FreeFramenum = 2;
	int* FreeList = List;
	
	//FreeFramePageList (5bits = LV1 info, 5bits = LV2 info, 21bits = LRU count bit, 1bit = Page Valid bit)
	for (int i = 0; i < 0x400; i++) {
		if ((FreeList[i] & 0x1) == 0) {
			if (option == 0) {																	//option -> MemFreeFrameList type(0 = memory freeframelist, else = disk freeframelist)
				MemFreeFrameListSize--;
			}
			FreeFramenum = i;
			FreeList[i] += 0x3;																	//LRU bit update
			break;
		}
	}

	return FreeFramenum;
}

int FindLRUPage(int* List) {
	int* FreeList = List;
	int LRUPage = 0;
	int LRUCount = 0xFFFFFFFF;

	//FreeFramePageList (5bits = LV1 info, 5bits = LV2 info, 21bits = LRU count bit, 1bit = Page Valid bit)
	for (int i = 0; i < 0x400; i++) {
		if (FreeList[i] & 0x1 == 1) {
			if ((FreeList[i] >> 1) & 0x1FFFFF == 1) {											//0x1F:FFFF(21bits)
				LRUPage = i;
				break;
			}
			if((FreeList[i] >> 1) & 0x1FFFFF < LRUCount) {
				LRUPage = i;
				LRUCount = (FreeList[i] >> 1) & 0x1FFFFF;
			}
		}
	}
	printf("test %d\n",LRUPage);
	return LRUPage;
}

void PageCopy(int* target1, int target1num, int* target1list, int* target2, int target2num, int* target2list) {
	for (int i = 0; i < 0x100; i++) {														//1KB / 4byte = 256(0x100)
		target1[(target1num * 0x100) + i] = target2[(target2num * 0x100) + i];
		target2[(target2num * 0x100) + i] = 0;
	}
	target1list[target1num] = target2list[target2num];
	target2list[target2num] = target2list[target2num] & 0xFFFFFFFE;							//valid bits reset & LRU bit remain
}