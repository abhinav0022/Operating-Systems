#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "common.h"
#include "wc.h"


typedef struct dataItem{
	char* key;
	int value;
	struct dataItem* next;
}item;

item* create_item(char *key);
void insert_item(struct wc* hashtable,  char* key);
unsigned long create_hashkey(char* key, long size);
//item* search_item(struct wc* hashtable, char* key);

struct wc {
	/* you can define this struct to have whatever fields you want. */
	long size;
	item** table;
};

struct wc *
wc_init(char* word_array, long size)
{
	long hSize = 2*size;
	struct wc* wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	(*wc).table = malloc(hSize*(sizeof(item**)));
	(*wc).size = hSize;
	assert((*wc).table);
	long idx = 0;
	
	while (idx < hSize){
		(*wc).table[idx] = NULL;
		idx++;
	}
	
	char* actualWord;
	int wordLength = 0;
	idx = 0;
	char* arrayPointer = word_array;
	int spaceCount = 0;
	
	while (idx < size || word_array[idx] != '\0'){
		if (wordLength == 0 && isspace(word_array[idx]))
				spaceCount++;
		else if(wordLength != 0 && ((idx == size-1) || isspace(word_array[idx]))){
			actualWord = (char*)malloc(sizeof(char)*(wordLength+1));
			actualWord[wordLength] = '\0';
			strncpy(actualWord, spaceCount + arrayPointer, wordLength);
			arrayPointer += wordLength + spaceCount + 1;
			wordLength = 0;
			spaceCount = 0;
			insert_item(wc, actualWord);
		}
		else
			wordLength++;
		idx++;
	}
	return wc;
}

void
wc_output(struct wc* wc)
{
	int idx = 0;
	while (idx < (*wc).size){
		item* temp = (*wc).table[idx];
		while(temp != NULL){
			printf("%s:%d\n", temp->key, temp->value);
			temp = temp->next;
		}
		idx++;
	}
}


void
wc_destroy(struct wc* wc)
{
	int idx = 0;
	while(idx < (*wc).size){
		item* next = NULL;
		item* current = (*wc).table[idx];
		while (true){
			if (current == NULL)
				break;
			next = (*current).next;
			free((*current).key);
			(*current).key = NULL;
			free(current);
			current = NULL;
			current = next;
		}
		(*wc).table[idx] = NULL;
		idx++;
	}
	free((*wc).table);
	(*wc).table = NULL;
	free(wc);
	wc = NULL;
}

item* create_item(char* key){
	item* newItem = (item*)malloc(sizeof(item));
	newItem->value = 1;
	newItem->key = key;
	newItem->next = NULL;
	return newItem;
}

void insert_item(struct wc* hashtable, char* key){
	item* prevItem = NULL;
	unsigned long hashKey = create_hashkey(key, hashtable->size);
	item* curItem = (*hashtable).table[hashKey];
	bool keyExists = false;
	
	while(curItem != NULL){
		if (strcmp(key, (*curItem).key) == 0){
			keyExists = true;
			break;
		}
		prevItem = curItem;
		curItem = (*curItem).next;
	}
	
	if(!keyExists){
		item* newItem = create_item(key);
		assert(newItem);
		if (prevItem == NULL)
			(*hashtable).table[hashKey] = newItem;
		else
			(*prevItem).next = newItem;
	}
	else if(keyExists){
		(*curItem).value += 1;
		free(key);
	}
	
}

unsigned long create_hashkey(char* key, long size){
	unsigned long hash = 0;
	unsigned const char* us;
	us = (unsigned const char *) key;
	while(*us != '\0'){
		hash = 37*hash + *us;
		us++;
	}
	return hash%size;
}

//item* search_item(struct wc* hashtable, char* key){
//	unsigned long hKey = create_hashkey(key, hashtable->size);
//	item* entry = (*hashtable).table[hKey];
//	while(entry != NULL){
//		if(strcmp(key , (*entry).key) == 0)
//			break;
//		entry = (*entry).next;
//	}
//	return entry;
//}