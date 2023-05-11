#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

#include "user.h"

char **data;
int num_params = 0;
pthread_barrier_t barrier;

#ifdef MULTI_THREAD

void *multi_ops(void *index)
{
    char buffer[TO_READ];
    char write_buff[] = "Wathever content you would like.\n";
    int my_id = *(int *)index;
    int offset = 0;
    int ret = -1;
    ssize_t bytes_read = 0;
    int i = 0;
    int arg = 0;
    const char *filename = "/home/ludovico99/Scrivania/Progetto_SOA/user_data_management_project/mount/the-file";
    int fd = -1;

    // 66 % READERS, 33% WRITERS
    if (my_id == 0) {
        fd = open(filename, O_RDONLY);
        arg = DEV_READ;
    }    
    else if (my_id < (NUM_THREADS * 2) / 3)
        arg = GET_DATA;
    else if (my_id > (NUM_THREADS * 2) / 3 + NUM_THREADS / 6)
        arg = PUT_DATA;
    else
        arg = INVALIDATE_DATA;

    srand(time(NULL));

    pthread_barrier_wait(&barrier);

    for (i = 0; i < REQS; i++)
    {

        offset = rand() % NBLOCKS;
        switch (arg)
        {
        case DEV_READ:
            if (fd == -1)
            {
                printf("Errore nell'apertura del file, potrebbe richiedere il path assoluto\n");
                break;
            }
            memset(buffer, 0, TO_READ);
            ret = read(fd, buffer, TO_READ);
            if (ret >= 0) {
                buffer[ret] = '\0';          
                printf("DEV_READ (%d): %s\n", ret, buffer);
            }  
            break;
        case PUT_DATA:
            ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
            if (ret >= 0)
                printf("%s written into block at offset %d\n", write_buff, ret);
            break;
        case GET_DATA:
            memset(buffer, 0, TO_READ);
            ret = syscall(GET_DATA, offset, buffer, TO_READ);
            if (ret >= 0){
                buffer[ret] = '\0';
                printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
            }
            break;
        case INVALIDATE_DATA:
            ret = syscall(INVALIDATE_DATA, offset);
            if (ret >= 0)
                printf("The block at index %d invalidation ended with code %d\n", offset, ret);
            break;
        default:
            printf("Syscall number inserted is invalid\n");
            break;
        }
        if (ret < 0)
            printf("The system call %d ended with the following error message: %s\n", arg, strerror(-ret));
    }
    if (fd != -1) close(fd);

    pthread_exit(0);
}

void *my_thread(void *index)
{

    int my_id = *(int *)index;
    int my_part = 0;
    char **temp = &data[2];
    int count = 0;
    int upper_bound = 0;
    char buffer[TO_READ];
    char write_buff[] = "Wathever content you would like.\n";
    int offset = 0;
    int ret = -1;
    int i = 0;
    int arg;

    arg = strtol(data[1], NULL, 10);

    // AUDIT printf("The thread %d is executing the system call with NR: %d\n", my_id, arg);

    if (arg == PUT_DATA)
    {
        ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
        if (ret >= 0)
            printf("%s written into block at offset %d\n", write_buff, ret);
        else
            printf("The system call %d ended with the following error message: %d\n", arg, ret);
        pthread_exit(0);
    }

    my_part = my_id;
    if (arg == INVALIDATE_DATA)
        upper_bound = num_params;
    else
        upper_bound = NBLOCKS;
    while (my_part < upper_bound)
    {
        if (arg == INVALIDATE_DATA)
            offset = atoi(temp[my_part]);
        else
            offset = my_part;
        switch (arg)
        {

        case GET_DATA:
            memset(buffer, 0, TO_READ);
            ret = syscall(GET_DATA, offset, buffer, TO_READ);
            if (ret >= 0) {
                buffer[ret] = '\0';
                printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
            }
            break;
        case INVALIDATE_DATA:
            ret = syscall(INVALIDATE_DATA, offset);
            if (ret >= 0)
                printf("The block at index %d invalidation ended with code %d\n", offset, ret);
            break;
        default:
            printf("Syscall number inserted is invalid\n");
            break;
        }
        if (ret < 0)
            printf("The system call %d ended with the following error message: %s\n", arg, strerror(-ret));

        i += 1;
        my_part = my_id + NUM_THREADS * i;
    }
    pthread_exit(0);
}

void *same_blk(void *index)
{
    int my_id = *(int *)index;
    char buffer[TO_READ];
    char write_buff[] = "Wathever content you would like.\n";
    int offset = 0;
    int ret = -1;
    int i = 0;
    int arg;
    int op = 0;

    if (my_id < (NUM_THREADS * 2) / 3)
        arg = GET_DATA;
    else if (my_id > (NUM_THREADS * 2) / 3 + NUM_THREADS / 6)
        arg = PUT_DATA;
    else
        arg = INVALIDATE_DATA;

    pthread_barrier_wait(&barrier);
    for (i = 0; i < REQS; i++)
    {
        switch (arg)
        {
        case PUT_DATA:
            ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
            if (ret >= 0)
                printf("%s written into block at offset %d\n", write_buff, ret);
            break;
        case GET_DATA:
            memset(buffer, 0, TO_READ);
            ret = syscall(GET_DATA, offset, buffer, TO_READ);
            if (ret >= 0){
                buffer[ret] = '\0';
                printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
            }
            break;
        case INVALIDATE_DATA:
            ret = syscall(INVALIDATE_DATA, offset);
            if (ret >= 0)
                printf("The block at index %d invalidation ended with code %d\n", offset, ret);
            break;
        default:
            printf("Syscall number inserted is invalid\n");
            break;
        }
        if (ret < 0)
            printf("The system call %d ended with the following error message: %s\n", arg, strerror(-ret));
    }
    pthread_exit(0);
}
#endif

int main(int argc, char **argv)
{
    int arg;

#ifndef MULTI_THREAD
    char buffer[TO_READ] = "\0";
    char write_buff[] = "Wathever content you would like.\n";
    int offset = 0;
    int ret = -1;
    int fd;
    const char * filename = "/home/ludovico99/Scrivania/Progetto_SOA/user_data_management_project/mount/the-file";

    if (argc < 2)
    {
        printf("usage: prog sys_call-num [offset] \n");
        return EXIT_FAILURE;
    }

    arg = strtol(argv[1], NULL, 10);
    AUDIT printf("Invoked system call with NR: %d\n", arg);

    if (arg == GET_DATA && argc < 3)
    {
        printf("sys_get_data needs an offset to be specified\n");
        return EXIT_FAILURE;
    }

    switch (arg)
    {
    case DEV_READ:
         fd = open(filename, O_RDONLY);
            if (fd == -1)
            {
                printf("Errore nell'apertura del file, potrebbe richiedere il path assoluto\n");
                break;
            }
            memset(buffer, 0, TO_READ);
            ret = read(fd, buffer, TO_READ);
            if (ret >= 0) {
                buffer[ret] = '\0';
                printf("DEV_READ (%d): %s\n", ret, buffer);
            }
            close(fd);
            break;
    case PUT_DATA:
        ret = syscall(PUT_DATA, write_buff, strlen(write_buff));
        if (ret >= 0)
            AUDIT printf("%s written into block at index %d\n", write_buff, ret - 2);
        break;
    case GET_DATA:
        memset(buffer, 0, TO_READ);
        offset = strtol(argv[2], NULL, 10);
        ret = syscall(GET_DATA, offset, buffer, TO_READ);      
        if (ret >= 0){
            buffer[ret] = '\0';
            printf("Bytes read (%d) from block at index %d: %s\n", ret, offset, buffer);
        }
        break;
    case INVALIDATE_DATA:
        offset = strtol(argv[2], NULL, 10);
        ret = syscall(INVALIDATE_DATA, offset);
        if (ret >= 0)
            AUDIT printf("The block at index %d invalidation ended with code %d\n", offset, ret);
        break;
    default:
        AUDIT printf("Syscall number inserted is invalid\n");
        ret = 0;
        break;
    }
    if (ret < 0)
        AUDIT printf("The system call invoked ended with the following error message: %s\n", strerror(-ret));

#else
    int i = 0;
    pthread_t tids[NUM_THREADS];
    arg = strtol(argv[1], NULL, 10);
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

    pthread_barrier_init(&barrier, NULL, NUM_THREADS);

    for (i = 0; i < NUM_THREADS; i++)
    {
        array[i] = i;
        if (arg == SAME_BLOCK_OPS)
            pthread_create(&tids[i], NULL, same_blk, (void *)&array[i]);
        else if (arg == MULTI_OPS)
            pthread_create(&tids[i], NULL, multi_ops, (void *)&array[i]);
        else
            pthread_create(&tids[i], NULL, my_thread, (void *)&array[i]);
    }

    for (i = 0; i < NUM_THREADS; i++)
        pthread_join(tids[i], NULL);
    pthread_barrier_destroy(&barrier);
#endif
    return 0;
}
