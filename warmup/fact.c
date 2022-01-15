#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

int factorial(int n);
bool isInteger(char *argument);

int
main(int argc, char **argv)
{
	if (argc == 2){
		if(isInteger(argv[1])){
			int number = atoi(argv[1]);
			if (number > 12)
				printf("Overflow\n");
			else if(number > 0){
				int result = factorial(number);
				printf("%d\n", result);
			}
			else
				printf("Huh?\n");
		}
		else
			printf("Huh?\n");
	}
	else{
		printf("Huh?\n");
	}
	return 0;
}

int factorial(int n)
{
	if (n == 1 || n == 0)
		return 1;
	return n*factorial(n-1);
}

bool isInteger(char* argument)
{
	size_t length = strlen(argument);
	for (int i = 0; i < length; i++){
		if (isdigit(argument[i]) == 0){
			return false;
		} 
	}
	return true;
}