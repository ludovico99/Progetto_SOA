

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>

#define PUT_DATA 156
#define GET_DATA 174
#define INVALIDATE_DATA 177
#define SIZE 4096

int main(int argc, char** argv){
	
	long int arg;	
    char buffer[SIZE];
    int offset = 0;

	if(argc < 2){
		printf("usage: prog sys_call-num [offset] \n");
		return EXIT_FAILURE;
	}	
	
    
	arg = strtol(argv[1],NULL,10);
    

    switch (arg){
        case PUT_DATA:
            syscall(PUT_DATA, buffer, SIZE);
            break;
        case GET_DATA:
            offset = strtol(argv[2], NULL, 10);
            int bytes_read = syscall(GET_DATA, offset, buffer, SIZE);
            printf ("Bytes read (%d) from block at offset %d: %s\n", bytes_read, offset, buffer);
            break;
        case INVALIDATE_DATA:
            offset = strtol(argv[2], NULL, 10);
            syscall(INVALIDATE_DATA, offset);
            break;
        default:
            printf("Syscall number inserted is invalid");
            break;
    }
    
	return 0;
}