#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include "common.h"

/* make sure to use syserror() when a system call fails. see common.h */

void copy_file(char* src_path, char* dest_path);
void create_directory(char* dest_path);
void traversing(char* src_path, char* dest_path);

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	
	
	if(opendir(argv[1]) == NULL)
		syserror(opnedir, argv[1]);
	
	create_directory(argv[2]);
	
	traversing(argv[1], argv[2]);
	
	
	return 0;
}

void copy_file(char* src_path, char* dest_path){
	int fdSrc;
	fdSrc = open(src_path, O_RDONLY);
	if(fdSrc < 0){
		syserror(open, src_path);
	}
	
	int fdDest = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
	if(fdDest < 0){
		syserror(open, dest_path);
	}
	char buf[4096];
	int ret; 
	while((ret = read(fdSrc, buf, 4096)) > 0){
		if(ret != write(fdDest, buf,ret))
			syserror(write, dest_path);
	}
	
	if (ret == -1)
		syserror(read, src_path);
	
	
	if(close(fdDest) == -1)
		syserror(close,dest_path);
	if(close(fdSrc) == -1)
		syserror(close,src_path);
}

void create_directory(char* dest_path){

	int dir_result;
	if((dir_result = mkdir(dest_path,S_IRWXU)) == -1){
		syserror(mkdir, dest_path);
	}
}

void traversing(char* src_path, char* dest_path){
	DIR* dSrc = opendir(src_path);
	
	if(dSrc == NULL)
		return;
	
	struct dirent* dirPtr = NULL;
	
	while((dirPtr = readdir(dSrc)) != NULL){
		if ((strcmp(".",dirPtr->d_name) != 0) && (strcmp("..",dirPtr->d_name) != 0)){
				
			char entryName[256];
			char slash[] = "/";
			char modSrc[PATH_MAX];
			char modDest[PATH_MAX];
			strcpy(entryName, dirPtr->d_name);
			strcpy(modDest, dest_path);
			strcat(modDest, slash);
			strcat(modDest, entryName);
			strcpy(modSrc, src_path);
			strcat(modSrc, slash);
			strcat(modSrc, entryName);
			struct stat sbuf;
			if(stat(modSrc, &sbuf) == -1)
				syserror(stat, modSrc);
			
			if(S_ISREG(sbuf.st_mode)){
				copy_file(modSrc, modDest);
				if(chmod(modDest, sbuf.st_mode) < 0)
					syserror(chmod, modDest);
			}
			else if(S_ISDIR(sbuf.st_mode)){
				create_directory(modDest);
				traversing(modSrc, modDest);
			}

			
			
		}
	}
	struct stat sb;
	if(stat(src_path, &sb) == -1)
		syserror(stat, src_path);
	if(chmod(dest_path, sb.st_mode) < 0)
		syserror(chmod, dest_path);
	closedir(dSrc);
}
