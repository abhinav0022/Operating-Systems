#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include "thread.h"
#include "interrupt.h"



#define READY 100
#define RUN 200
#define BLOCK 300
#define EXIT 400
#define EMPTY 500
#define KILL 600



volatile static int numActiveThd = 0;

volatile static Tid currThdID;



typedef struct tNode{
	struct tNode* next;
	Tid tid;
	//void* stPtr;
	struct tNode* prev;
}threadNode;

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
	threadNode* head;
	threadNode* tail;
};

//typedef struct tQueue{
//	threadNode* head;
//	threadNode* tail;
//}threadQueue;

static struct wait_queue readyQueue;


/* This is the thread control block */
struct thread{
	ucontext_t thread_context;
	void* t_sp;
	int state;
	struct wait_queue* w_queue;
};

static struct thread availableThd[THREAD_MAX_THREADS];

void queue_push(struct wait_queue* thdQ, Tid thdID);
Tid queue_headPop(struct wait_queue* thdQ);
Tid queue_pop(struct wait_queue* thdQ, Tid thdID);
void free_exit_threads();
void thread_stub(void (*thread_main)(void *), void *arg);

void
thread_init(void)
{
	/* your optional code here */
	unsigned i = 0;
	int err = 0;
	while(i <= THREAD_MAX_THREADS-1){
		availableThd[i].w_queue = NULL;
		availableThd[i].state = EMPTY;
		i++;
	}
	currThdID = 0;
	availableThd[currThdID].state = RUN;
	numActiveThd = 1;
	availableThd[currThdID].w_queue = wait_queue_create();
	err = getcontext(&availableThd[currThdID].thread_context);
	assert(!err);
	
}

Tid
thread_id()
{
	
	return currThdID;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if (THREAD_MAX_THREADS - 1 < numActiveThd) {
		interrupts_set(enabled);
		return THREAD_NOMORE;
	}
	numActiveThd++;
	int i = 0, thdNum;
	void* sp = malloc(THREAD_MIN_STACK);
	if (sp == NULL) return THREAD_NOMEMORY;
	while(i <= THREAD_MAX_THREADS-1){
		if(EXIT == availableThd[i].state || EMPTY == availableThd[i].state){
			if(EXIT == availableThd[i].state){
				free(availableThd[i].t_sp);
				availableThd[i].t_sp = NULL;
			}
			thdNum = i;
			getcontext(&availableThd[i].thread_context);
			break;
		}
		i++;
	}
	
	availableThd[thdNum].state = READY;
	availableThd[thdNum].t_sp = sp;
	availableThd[thdNum].thread_context.uc_stack.ss_size = THREAD_MIN_STACK;
	availableThd[thdNum].thread_context.uc_stack.ss_sp = availableThd[thdNum].t_sp;
	availableThd[thdNum].thread_context.uc_link = NULL;
	availableThd[thdNum].thread_context.uc_stack.ss_flags = 0;
	availableThd[thdNum].w_queue = wait_queue_create();
	
	sp = availableThd[thdNum].thread_context.uc_stack.ss_sp;
	sp += availableThd[thdNum].thread_context.uc_stack.ss_size;
	sp -= (unsigned long long)sp%16;
	sp -= 8;
	
	availableThd[thdNum].thread_context.uc_mcontext.gregs[REG_RSP] = (unsigned long)(greg_t)sp;
	availableThd[thdNum].thread_context.uc_mcontext.gregs[REG_RDI] = (unsigned long)(greg_t)fn;
	availableThd[thdNum].thread_context.uc_mcontext.gregs[REG_RSI] = (unsigned long)(greg_t)parg;
	availableThd[thdNum].thread_context.uc_mcontext.gregs[REG_RIP] = (unsigned long)(greg_t)&thread_stub;
	
	queue_push(&readyQueue, thdNum);
	interrupts_set(enabled);
	return thdNum;
}

Tid
thread_yield(Tid want_tid)
{
	int enabled = interrupts_set(0);
	//Tid id = thread_id();
	int error = 0;
	volatile bool switched = false;
	struct ucontext_t uc;
	//if (EXIT != availableThd[id].state) free_exit_threads();
	bool shouldPop = false;
	if(THREAD_ANY == want_tid){
		if(availableThd[currThdID].state == RUN && numActiveThd == 1) {
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		else{
			shouldPop = true;
			want_tid = queue_headPop(&readyQueue);
		}
	}
	else if(THREAD_SELF == want_tid){
		interrupts_set(enabled);
		return currThdID;
	}
	
	//id = thread_id();
	
	if (!((THREAD_MAX_THREADS > want_tid && -1 < want_tid) && ((RUN == availableThd[want_tid].state) || (KILL == availableThd[want_tid].state) || (READY == availableThd[want_tid].state)))) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
		
	error = getcontext(&uc);
	free_exit_threads();
	if(!switched && error == 0){
		if(RUN == availableThd[currThdID].state){
			availableThd[currThdID].state = READY;
			queue_push(&readyQueue, currThdID);
		}
//		id = thread_id();

		if(KILL != availableThd[want_tid].state) availableThd[want_tid].state = RUN;
		availableThd[currThdID].thread_context = uc;
		uc = availableThd[want_tid].thread_context;
		currThdID = want_tid;
		if(!shouldPop) queue_pop(&readyQueue, want_tid);
		switched = true;
		error = setcontext(&uc);
	}
	if(KILL == availableThd[currThdID].state){
		//interrupts_set(enabled);
		thread_exit();
	}
	interrupts_set(enabled);
	return want_tid;
}

void
thread_exit()
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if(numActiveThd > 1 || NULL != availableThd[currThdID].w_queue->head){
		--numActiveThd;
		availableThd[currThdID].state = EXIT;
		thread_wakeup(availableThd[currThdID].w_queue, 1);
		interrupts_set(enabled);
		thread_yield(THREAD_ANY);
	}
	else if( NULL == availableThd[currThdID].w_queue->head && numActiveThd <= 1) {
		interrupts_set(enabled);
		return exit(0);
	}
}

Tid
thread_kill(Tid tid)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if(tid != currThdID && tid < THREAD_MAX_THREADS && tid >= 0){
		if(BLOCK != availableThd[tid].state && READY != availableThd[tid].state){
			interrupts_set(enabled);
			return THREAD_INVALID;
		}
		availableThd[tid].state = KILL;
	}
	else if(tid == currThdID || tid >= THREAD_MAX_THREADS || tid < 0){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	interrupts_set(enabled);
	return tid;

}

void free_exit_threads(){
	int i = 1, enabled = 0;
	enabled = interrupts_set(0);

	while(i <= THREAD_MAX_THREADS -1){
		if (currThdID != i && EXIT == availableThd[i].state){
			availableThd[i].state = EMPTY;
			free(availableThd[i].t_sp);
			availableThd[i].t_sp = NULL;
			wait_queue_destroy(availableThd[i].w_queue);
			availableThd[i].w_queue = NULL;
		}
		i++;
	}
	interrupts_set(enabled);
}
void queue_push(struct wait_queue* thdQ, Tid thdID){
	int enabled = 0;
	threadNode* newNode = (threadNode*)malloc(sizeof(threadNode));
	assert(newNode);
	(*newNode).next = NULL;
	(*newNode).prev = (*thdQ).tail;
	(*newNode).tid = thdID;
	enabled = interrupts_set(0);
	//(*newNode).stPtr = stackP;
	if((*thdQ).head != NULL)
		(*((*thdQ).tail)).next = newNode;
	else
		(*thdQ).head = newNode;
	
	(*thdQ).tail = newNode;
	interrupts_set(enabled);
}

Tid queue_headPop(struct wait_queue* thdQ){
	Tid poppedId;
	int enabled = 0;
	threadNode* headPrev = NULL;
	enabled = interrupts_set(0);
	headPrev = (*thdQ).head;
	if (NULL != headPrev) (*thdQ).head = (*headPrev).next;
	else return THREAD_NONE;
	if (NULL != (*thdQ).head) (*((*thdQ).head)).prev = NULL;
	else (*thdQ).tail = NULL;
	poppedId = (*headPrev).tid;
	free(headPrev);
	headPrev = NULL;
	interrupts_set(enabled);
	return poppedId;
}

Tid queue_pop(struct wait_queue* thdQ, Tid thdID){
	threadNode* curr = NULL;
	int enabled = 0;
	Tid poppedID;
	enabled = interrupts_set(0);
	//Tid t_ID = thread_id();
//	if (THREAD_ANY == thdID){
//		if(numActiveThd == 1 && RUN ==  availableThd[t_ID].state){
//			return THREAD_NONE;
//		}
//		else thdID = (*((*thdQ).head)).tid;
//	}
//	else if (THREAD_SELF == thdID) thdID = t_ID;
	
	curr = (*thdQ).head;
	while(true){
		if (NULL == curr) break;
		if(thdID != (*curr).tid){
			curr = (*curr).next;
			continue;
		}
		if (NULL != (*curr).prev) (*((*curr).prev)).next =  (*curr).next;
		else (*thdQ).head = (*curr).next;
		if (NULL != (*curr).next) (*((*curr).next)).prev =  (*curr).prev;
		else (*thdQ).tail = (*curr).prev;
		poppedID = (*curr).tid;
		free(curr);
		curr = NULL;
		interrupts_set(enabled);
		return poppedID;
	}
	interrupts_set(enabled);
	return THREAD_INVALID;
	
}

void thread_stub(void (*thread_main)(void *), void *arg){
	//Tid ret;
	interrupts_on();
	thread_main(arg); 
	thread_exit();

}


/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	(*wq).head = NULL;
	(*wq).tail = (*wq).head;
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	threadNode* next = NULL;
	threadNode* curr = (*wq).head;
	while(true){
		if(NULL == curr) break;
		next = (*curr).next;
		free(curr);
		curr = NULL;
		curr = next;
	}
	
	free(wq);
	wq = NULL;
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if(NULL != queue){
		if(numActiveThd != 1){
			availableThd[currThdID].state = BLOCK;
			queue_push(queue,currThdID);
			numActiveThd--;
			interrupts_set(enabled);
			return thread_yield(THREAD_ANY);
		}
		else{
			interrupts_set(enabled);
			return THREAD_NONE;
		}
	}
	else{
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	
	
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if(NULL != queue){
		if(all == 0){
			Tid pId = queue_headPop(queue);
			if(THREAD_NONE != pId){
				if(KILL != availableThd[pId].state){
					availableThd[pId].state = READY;
				}
				queue_push(&readyQueue, pId);
				numActiveThd++;
				interrupts_set(enabled);
				return 1;
			}
			else{
				interrupts_set(enabled);
				return 0;
			}
			
		}
		else{
			int count = 0;
			while(0 != thread_wakeup(queue,0)) count++;
			interrupts_set(enabled);
			return count;
		}
	}
	else{
		interrupts_set(enabled);
		return 0;
	}
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	if(EMPTY != availableThd[tid].state && EXIT != availableThd[tid].state && tid != currThdID && tid < THREAD_MAX_THREADS && tid >= 0){
		Tid id = thread_sleep(availableThd[tid].w_queue);
		if(THREAD_NONE == id) exit(0);
		interrupts_set(enabled);
		return tid;
	}
	else{
		interrupts_set(enabled);
		return THREAD_INVALID;
	}	
}

struct lock {
	/* ... Fill this in ... */
	struct wait_queue* lockWQ;
	Tid acquireId;
};

struct lock *
lock_create()
{
	int enabled = 0;
	enabled = interrupts_set(0);
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	(*lock).acquireId = THREAD_NONE;
	(*lock).lockWQ = wait_queue_create();
	assert((*lock).lockWQ);
	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	assert(lock != NULL);
	wait_queue_destroy((*lock).lockWQ);
	interrupts_set(enabled);
	free(lock);
	lock = NULL;
}

void
lock_acquire(struct lock *lock)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	assert(lock != NULL);
	while(__sync_val_compare_and_swap(&((*lock).acquireId), THREAD_NONE, currThdID) != THREAD_NONE) thread_sleep((*lock).lockWQ);
	interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
	int enabled = 0;
	enabled = interrupts_set(0);
	assert(lock != NULL);
	(*lock).acquireId = THREAD_NONE;
	thread_wakeup((*lock).lockWQ,1);
	interrupts_set(enabled);
}

struct cv {
	/* ... Fill this in ... */
	struct wait_queue* cvWQ;
};

struct cv *
cv_create()
{
	int enabled = 0;
	enabled = interrupts_set(0);
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	(*cv).cvWQ = wait_queue_create();
	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	wait_queue_destroy((*cv).cvWQ);

	free(cv);
	cv = NULL;
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	int enabled = 0;
	if(currThdID == (*lock).acquireId){
		enabled = interrupts_set(0);
		lock_release(lock);
		thread_sleep((*cv).cvWQ);
		interrupts_set(enabled);
		lock_acquire(lock);
	}


}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	if(currThdID == (*lock).acquireId){
		thread_wakeup((*cv).cvWQ, 0);
	}
	
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);
	if(currThdID == (*lock).acquireId){
		thread_wakeup((*cv).cvWQ, 1);
	}

	
}
