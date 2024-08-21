#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    int ret;
    const int ms_to_us_mul = 1000;
    struct thread_data* thread_data = (struct thread_data *) thread_param;

    ret = usleep(thread_data->wait_to_obtain_ms * ms_to_us_mul);
    if(ret != 0)
    {
        perror("Could not sleep before obtaining Mutex");
        return NULL;
    }

    ret = pthread_mutex_lock(thread_data->thread_mutex);
    if(ret != 0)
    {
        perror("Could not lock Mutex");
        return NULL;
    }

    ret = usleep(thread_data->wait_to_release_ms * ms_to_us_mul);
    if(ret != 0)
    {
        perror("Could not sleep after obtaining Mutex");
        return NULL;
    }

    ret = pthread_mutex_unlock(thread_data->thread_mutex);
    if(ret != 0)
    {
        perror("Could not unlock Mutex");
        return NULL;
    }

    thread_data->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    struct thread_data * thread_data = malloc(sizeof(struct thread_data));

    if(thread_data == NULL)
    {
        perror("Couldn't allocate thread data");
        return false;
    }

    thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_data->wait_to_release_ms = wait_to_release_ms;
    thread_data->thread_mutex = mutex;
    thread_data->thread_complete_success = false;

    int thread_return = pthread_create(thread, NULL, threadfunc, thread_data);

    if(thread_return != 0)
    {
        perror("Error in creating a thread");
        return false;
    }

    return true;
}

