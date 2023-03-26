

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
    char write_buff[SIZE] = "Wathever content you would like.\n";
    int offset = 0;
    int ret;

	if(argc < 2){
		printf("usage: prog sys_call-num [offset] \n");
		return EXIT_FAILURE;
	}	
	
    
	arg = strtol(argv[1],NULL,10);
    

    switch (arg){
        case PUT_DATA:
            ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
            printf ("%s written into block at index (error-code) %d\n",write_buff, ret);
            break;
        case GET_DATA:
            offset = strtol(argv[2], NULL, 10);
            int bytes_read = syscall(GET_DATA, offset, buffer, SIZE);
            printf ("Bytes read (%d) from block at index %d: %s\n", bytes_read, offset, buffer);
            break;
        case INVALIDATE_DATA:
            offset = strtol(argv[2], NULL, 10);
            int ret = syscall(INVALIDATE_DATA, offset);
            printf ("The block at index %d invalidation ended with code %d\n", offset, ret);
            break;
        default:
            printf("Syscall number inserted is invalid");
            break;
    }
    
	return 0;
}