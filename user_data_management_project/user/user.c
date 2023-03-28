

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#include "user.h"

#define MULTI_OPS -1
#define PUT_DATA 156
#define GET_DATA 174
#define INVALIDATE_DATA 177
#define SIZE 4096

char **data;
int num_params = 0;

void *my_thread(void *index)
{
    int my_id = *(int*)index;
    int my_part = 0;
    char **temp = &data[2];
    int count = 0;
    char buffer[SIZE];
    char write_buff[SIZE] = "Wathever content you would like.\n";
    int offset = 0;
    int ret = -1;
    int i = 0;
    int arg;

    arg = strtol(data[1], NULL, 10);
    
    if (arg == MULTI_OPS){
        int op = my_id % 3;
        if (op == 0) arg = GET_DATA;
        else if (op == 1) arg = INVALIDATE_DATA;
        else arg = PUT_DATA;
        AUDIT printf("The thread %d is executing the system call with NR: %d\n", my_id, arg);
    }
    else AUDIT printf("The thread %d is executing the system call with NR: %d\n", my_id, arg);

  
    if (arg == PUT_DATA){
        ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
        if (ret >= 0)
                AUDIT printf("%s written into block at offset %d\n", write_buff, ret);
        else
            AUDIT printf("The system call %d ended with the following error message: %s\n",arg, strerror(-ret));
        pthread_exit(0);
    }

    my_part = my_id;
    while (my_part < num_params)
    {
        offset = atoi(temp[my_part]);
        
        switch (arg)
        {
      
        case GET_DATA:
            memset(buffer,0,SIZE);
            ret = syscall(GET_DATA, offset, buffer, SIZE);
            if (ret >= 0)
                printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
            break;
        case INVALIDATE_DATA:
            ret = syscall(INVALIDATE_DATA, offset);
            if (ret >= 0)
                AUDIT printf("The block at index %d invalidation ended with code %d\n", offset, ret);
            break;
        default:
            AUDIT printf("Syscall number inserted is invalid");
            break;
        }
        if (ret < 0)
           AUDIT printf("The system call %d ended with the following error message: %s\n",arg, strerror(-ret));

        i += 1;
        my_part = my_id + NUM_THREADS * i;
    }
    pthread_exit(0);
}

int main(int argc, char **argv)
{
    int arg;

#ifndef MULTI_THREAD
    char buffer[SIZE];
    char write_buff[SIZE] = "Wathever content you would like.\n";
    int offset = 0;
    int ret = -1;
    if (argc < 2)
    {
        printf("usage: prog sys_call-num [offset] \n");
        return EXIT_FAILURE;
    }

    arg = strtol(argv[1], NULL, 10);
    AUDIT printf("Invoked system call with NR: %d\n", arg);

    switch (arg)
    {
    case PUT_DATA:
        ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
        if (ret >= 0)
            AUDIT printf("%s written into block at offset %d\n", write_buff, ret);
        break;
    case GET_DATA:
        offset = strtol(argv[2], NULL, 10);
        ret = syscall(GET_DATA, offset, buffer, SIZE);
        if (ret >= 0)
            printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
        break;
    case INVALIDATE_DATA:
        offset = strtol(argv[2], NULL, 10);
        ret = syscall(INVALIDATE_DATA, offset);
        if (ret >= 0)
            AUDIT printf("The block at index %d invalidation ended with code %d\n", offset, ret);
        break;
    default:
        AUDIT printf("Syscall number inserted is invalid");
        break;
    }
    if (ret < 0)
        AUDIT printf("The system call invoked ended with the following error message: %s\n", strerror(-ret));

#else
    int i = 0;
    pthread_t tids[NUM_THREADS];
    int array[NUM_THREADS];
    if (argc < 2)
    {
        printf("usage: prog sys_call-num [offset 1, offset 2, offset 3 ....] \n");
        return EXIT_FAILURE;
    }

    data = argv;
    num_params = argc - 2; // Total offsets passed as arguments
    AUDIT printf("num params: %d \n", num_params);
    AUDIT printf("Spawning %d threads ...\n", NUM_THREADS);
    for (i = 0; i < NUM_THREADS; i++){
        array[i] = i;
        pthread_create(&tids[i], NULL, my_thread, (void*)&array[i]);
    }

    for (i = 0; i < NUM_THREADS; i++)
        pthread_join(tids[i], NULL);
#endif
    return 0;
}
