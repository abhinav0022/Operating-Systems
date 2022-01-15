#include "request.h"
#include "server_thread.h"
#include <pthread.h>
#include <string.h>
#include "common.h"
#include <stdbool.h>

struct queue {
	int idx;
	struct queue* next;
};

typedef struct file_struct{
	int active;
	char* key;
	struct file_data* data;
}fileStruct;

typedef struct cache_struct{
	fileStruct** table;
	int maxSize;
	int tableSize;
	struct queue* queue;
	int curSize;
}cacheStruct;

struct server {
	cacheStruct* cache;
	pthread_t** wThreads;
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	int in;
	int counterReq;
	int out;
	int* buffer;	
};

/* static functions */

fileStruct* insertCache(struct file_data *data, struct server* svr);
int cache_evict(int size, struct server* svr);
fileStruct* searchCache(char* name, struct server* svr);

void cache_init(int maxSize, struct server *svr);
void free_cache_item(fileStruct* file);

unsigned long create_idx(char* key, struct server* svr){
	unsigned long hash = 0;
	unsigned const char* us;
	us = (unsigned const char *) key;
	while(*us != '\0'){
		hash = 37*hash + *us;
		us++;
	}
	return hash%svr->cache->tableSize;
}

void addHead(int idx, struct queue* queue){
	if(queue == NULL) return; 
    struct queue* temp;
    struct queue* head;
	head = queue;
	struct queue* newQ;
    
    newQ  = (struct queue*)malloc(sizeof(struct queue));
    newQ->next = NULL;
	newQ->idx = idx;
    
    if(NULL != queue->next){ 
		temp = head;
		while(true){ 
			if(NULL == temp->next) break;
		    temp = temp->next;
		}
		temp->next = newQ;
		return;
	}
    else queue->next = newQ;
    
}

void addTail(int idx, struct queue* queue){
	struct queue* newQ;
	struct queue* cur;
	
	newQ  = (struct queue*)malloc(sizeof(struct queue));
    newQ->next = NULL;
	newQ->idx = idx;

	if(NULL == queue->next) {
		queue->next = newQ;
		return;
	}

    cur = queue->next; // first element of the list

    while(true) {
		if(cur->next == NULL) break;
        cur = cur->next;
    }

	cur->next = newQ;
}

void remove_qItem(int idx, struct queue* queue){
	struct queue* prev;
	struct queue* cur;

    if(queue != NULL && queue->next != NULL){			

		cur = queue->next;
		prev = queue;
		
		while(true) {
			if(NULL == cur->next || idx == cur->idx) break;
			prev = cur;
			cur = cur->next;
		}

		if(idx == cur->idx) {
			prev->next = cur->next;
			free(cur);
			cur = NULL;
		}
		
	}
	else return;
}

void addLRU(int idx, struct server* svr){
	addHead(idx, svr->cache->queue);
}

void updateLRU(int idx, struct server* svr){
	remove_qItem(idx, svr->cache->queue);
	addTail(idx, svr->cache->queue);
}

int deleteLRU(int size, struct server* svr){
	cacheStruct* cache = svr->cache;
	struct queue* cur;
	cur = cache->queue->next; 

	
	while(true) {
		if(NULL == cur || (size <= (cache->maxSize - cache->curSize))) break;
		fileStruct* file = cache->table[cur->idx];
		if(NULL != file && 0 == file->active) {
			cache->curSize -= file->data->file_size;
			free_cache_item(cache->table[cur->idx]);
			cache->table[cur->idx] = NULL;
			remove_qItem(cur->idx, cache->queue);
		}
		cur = cur->next;
	}	
	
	if(size > (cache->maxSize - cache->curSize))
		return 0;

	else return 1;
}

fileStruct* tableInsert(struct file_data *data, struct server* svr){
	unsigned long idx;
	idx = create_idx(data->file_name, svr);
	fileStruct* fstruct;
	
	while(true) {
		if(NULL == svr->cache->table || NULL == svr->cache->table[idx] || 0 == strcmp(svr->cache->table[idx]->key, data->file_name)) break;
        idx++;
        idx %= svr->cache->tableSize;
    }
	
	fstruct = (fileStruct*)malloc(sizeof(fileStruct));
	fstruct->key = (char *)malloc(sizeof(char)*(1 + strlen(data->file_name))); 
	strcpy(fstruct->key, data->file_name); 
	svr->cache->curSize += data->file_size;
	fstruct->active = 0;
	fstruct->data = data;
	fstruct->key[strlen(data->file_name)] = '\0';
	svr->cache->table[idx] = fstruct;
	addLRU(idx, svr);
	return svr->cache->table[idx];

}

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

///* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

pthread_cond_t full = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	if(0 >= sv->max_cache_size){
		/* read file, 
		 * fills data->file_buf with the file contents,
		 * data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	}
	else{
		pthread_mutex_lock(&cache_lock);
		fileStruct* file = searchCache(data->file_name, sv);
		if(NULL == file){
			pthread_mutex_unlock(&cache_lock);

			ret = request_readfile(rq);
			if (ret == 0)	goto out;

			pthread_mutex_lock(&cache_lock);
			file = insertCache(data, sv); 
			request_set_data(rq, data);
			if(NULL != file) {
				updateLRU(create_idx(data->file_name, sv), sv);
				file->active++;
			}
			pthread_mutex_unlock(&cache_lock);

			request_sendfile(rq);

			pthread_mutex_lock(&cache_lock);
			if(NULL != file) file->active--;
			pthread_mutex_unlock(&cache_lock); 

			goto out;

		}
		else if(NULL != file){
			request_set_data(rq, file->data);
			data = file->data; 
			if(NULL != file) file->active++;
			updateLRU(create_idx(data->file_name, sv), sv);
			pthread_mutex_unlock(&cache_lock);

			request_sendfile(rq); // send file

			pthread_mutex_lock(&cache_lock);
			if(NULL != file) file->active--;
			pthread_mutex_unlock(&cache_lock);

			goto out;
		}
	}
	out:
		request_destroy(rq);

}



void forward_to_buff(int connfd, struct server* sv){
	sv->buffer[sv->in] = connfd;
	if(!sv->counterReq) pthread_cond_broadcast(&empty);
	sv->counterReq++;
	sv->in = (1+sv->in)%sv->max_requests;
}

int read_buff(struct server* sv){
	int fd = sv->buffer[sv->out];
	if(sv->max_requests == sv->counterReq) pthread_cond_broadcast(&full);
	sv->counterReq--;
	sv->out = (1 + sv->out)%sv->max_requests;
	return fd;
}

void helper(struct server* sv){
	while(true){
		pthread_mutex_lock(&lock);
		
		while(true){
			if(0 != sv->counterReq) break;
			pthread_cond_wait(&empty, &lock);
			if(sv->exiting){
				pthread_mutex_unlock(&lock);
				pthread_exit(NULL);
			}
		}
	int fd = read_buff(sv);
	pthread_mutex_unlock(&lock);
	do_server_request(sv,fd);
	
	}

}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	pthread_mutex_lock(&lock);
	sv = Malloc(sizeof(struct server));
	
	sv->in = 0;
	sv->max_cache_size = max_cache_size;
	sv->counterReq = 0;
	sv->wThreads = NULL;
	sv->out = 0;
	sv->nr_threads = nr_threads;
	sv->buffer = NULL;
	sv->max_requests = max_requests;
	sv->exiting = 0;
	sv->cache = NULL;

	//server_basic_init(nr_threads, max_cache_size, sv, max_requests);	
	
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		if (0 < nr_threads){
			int i = 0;
			sv->wThreads = (pthread_t**)(malloc)(sizeof(pthread_t*)*nr_threads);
			while(true){
				if (i >= nr_threads) break;
				sv->wThreads[i] = (pthread_t*)malloc(sizeof(pthread_t));
				pthread_create(sv->wThreads[i], NULL, (void*)&helper, (void*)sv);
				i++;
			}
		}
		if(0 < max_cache_size) cache_init(max_cache_size, sv);
		if(0 < max_requests) sv->buffer = (int*)malloc(sizeof(int)*(1 + max_requests));
	}

	/* Lab 4: create queue of max_request size when max_requests > 0 */

	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */

	pthread_mutex_unlock(&lock);
	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&lock);
		while(sv->max_requests == sv->counterReq){
			pthread_cond_wait(&full, &lock);
			if(sv->exiting){
				pthread_mutex_unlock(&lock);
				pthread_exit(NULL);
			}
		}
		forward_to_buff(connfd,sv);
		pthread_mutex_unlock(&lock);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	int i = 0;
	pthread_cond_broadcast(&empty);
	pthread_cond_broadcast(&full);
	/* make sure to free any allocated resources */
	while(true){
		if (i >= sv->nr_threads) break;
		pthread_join(*(sv->wThreads[i]), NULL);
		free(sv->wThreads[i]);
		sv->wThreads[i] = NULL;
		i++;
	}
	
	if (0 < sv->nr_threads){
		free(sv->wThreads);
		sv->wThreads = NULL;
	}
	if (0 < sv->max_cache_size){
		int j = 0;
		while(true){
			if (sv->cache->tableSize <= j) break;
			if(NULL != sv->cache->table[j]){
				free_cache_item(sv->cache->table[j]);
				sv->cache->table[j] = NULL;
			}
			j++;
		}

		struct queue* next;  
		struct queue* cur = sv->cache->queue->next;  
		
		
		while (true) { 
			if(NULL == cur) break;
			next = cur->next;  
			free(cur);
			cur = NULL;
			cur = next;  
		}  
			
		sv->cache->queue = NULL; 
		free(sv->cache->table);
		sv->cache->table = NULL;
		free(sv->cache->queue);
		free(sv->cache);
		sv->cache = NULL;
	}
	if (0 < sv->max_requests){
		free(sv->buffer);
		sv->buffer = NULL;
	}
	
	
	
	
	free(sv);
	sv = NULL;
	return;
}

int cache_evict(int size, struct server* svr) {
	if(svr->cache->maxSize < size)
		return 0; 
	
	if(svr->cache->maxSize - svr->cache->curSize >= size)
		return 1; 
		
	if(NULL == svr->cache->queue->next) 
		return 0;

	return deleteLRU(size, svr);
}

fileStruct* searchCache(char* name, struct server* svr){
	int idx = 0;
	idx = create_idx(name, svr); 

	while(true) {
		if(NULL == svr->cache->table || NULL == svr->cache->table[idx] || 0 == strcmp(svr->cache->table[idx]->key, name)) break;
        idx++;
        idx %= svr->cache->tableSize;
    }


	if(NULL == svr->cache->table[idx] || 0 != strcmp(svr->cache->table[idx]->key, name))
		return NULL;

	else return svr->cache->table[idx];
}

fileStruct* insertCache(struct file_data* data, struct server* svr){
	if(1 == cache_evict(data->file_size, svr)) 
		return tableInsert(data, svr);
	
	if(NULL != searchCache(data->file_name,svr))
		return searchCache(data->file_name,svr); 

	return NULL;
}

void cache_init(int maxSize, struct server *svr) {
	svr->cache = (cacheStruct*)malloc(sizeof(cacheStruct));
	svr->cache->curSize = 0;
	svr->cache->tableSize = 2147000;
	svr->cache->queue = (struct queue*)malloc(sizeof(struct queue));
	svr->cache->maxSize = maxSize;
	svr->cache->queue->next = NULL;
	svr->cache->table = (fileStruct**)malloc(sizeof(fileStruct*)*svr->cache->tableSize);
	
	int i = 0;
	while (true){
		if (svr->cache->tableSize <= i) break;
		svr->cache->table[i] = NULL;
		i++;
	}
	
}

void free_cache_item(fileStruct* file){
	free(file->key);
	file->key = NULL;
	file_data_free(file->data);
	file->data = NULL;
}