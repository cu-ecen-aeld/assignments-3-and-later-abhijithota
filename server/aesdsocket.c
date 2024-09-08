#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <pthread.h>

#define OUTPUT_FILE_PATH "/var/tmp/aesdsocketdata"

char now_buff[100];
pthread_mutex_t output_file_mutex;

int socket_fd;
bool is_signaled = false;

struct thread_data
{
    pthread_t thread_id;
    int client_fd;
    int done;
    SLIST_ENTRY(thread_data)
    thread_entries;
};

SLIST_HEAD(thread_head, thread_data)
thread_list;

void signal_handler(int signal_number)
{
    if ((signal_number == SIGTERM) || (signal_number == SIGINT))
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        close(socket_fd);
        is_signaled = true;
    }
}

void *timer_thread(void *arg)
{
    // Open File
    int out_fd = open(OUTPUT_FILE_PATH, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (out_fd < 0)
    {
        perror("Error in opening data file");
        return -1;
    }

    // Run until program received signal
    while (!is_signaled)
    {
        // Get current time
        time_t now_time = time(NULL);
        struct tm *now_tm = localtime(&now_time);

        // Format timestamp per RFC 2822 compliance
        strftime(now_buff, sizeof(now_buff), "timestamp: %a, %d %b %Y %H:%M:%S %z\n", now_tm);

        // Write to file atomically
        pthread_mutex_lock(&output_file_mutex);
        write(out_fd, now_buff, strlen(now_buff));
        pthread_mutex_unlock(&output_file_mutex);

        // Sleep for 10 seconds
        sleep(10);
    }

    close(out_fd);
}

void *server_thread(void *arg)
{
    struct thread_data *current_thread_data = (struct thread_data *)arg;
    int current_client_fd = current_thread_data->client_fd;

    char incoming_buffer[256];
    int out_fd = open(OUTPUT_FILE_PATH, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (out_fd < 0)
    {
        perror("Error in opening data file");
        return -1;
    }

    off_t start_pos = lseek(out_fd, 0, SEEK_CUR);
    bool is_the_end = false;
    pthread_mutex_lock(&output_file_mutex);
    while (!is_the_end && !is_signaled)
    {
        memset(incoming_buffer, 0, sizeof(incoming_buffer));
        int bytes_recv = recv(current_client_fd, incoming_buffer, sizeof(incoming_buffer), 0);

        if (bytes_recv <= 0)
        {
            perror("Error in recv");
        }

        char *newline_ptr = strchr(incoming_buffer, '\n');
        if (newline_ptr != NULL)
        {
            if (write(out_fd, incoming_buffer, newline_ptr - incoming_buffer + 1) < 0)
            {
                perror("Error in write with newline");
            }
            is_the_end = true;
        }
        else
        {
            write(out_fd, incoming_buffer, bytes_recv);
        }
    }

    lseek(out_fd, start_pos, SEEK_SET);
    is_the_end = false;
    while (!is_the_end && !is_signaled)
    {
        memset(incoming_buffer, 0, sizeof(incoming_buffer));
        int read_size = read(out_fd, incoming_buffer, sizeof(incoming_buffer));
        // printf("Read Size %d\n", read_size);

        if (read_size == -1)
        {
            perror("Error in read");
        }
        else if (read_size == 0)
        {
            is_the_end = true;
        }
        else
        {
            if (send(current_client_fd, incoming_buffer, read_size, 0) < 0)
            {
                perror("Error in send");
                return -1;
            }
        }
    }
    pthread_mutex_unlock(&output_file_mutex);

    close(out_fd);
    current_thread_data->done = 1;
}

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    if (open(OUTPUT_FILE_PATH, O_CREAT | O_APPEND | O_RDWR, 0644) > 0)
    {
        remove(OUTPUT_FILE_PATH);
    }

    // Check for daemon mode request
    bool is_daemon = false;
    if (argc > 2)
    {
        syslog(LOG_ERR, "Need to pass at max 1 argument");
        return -1;
    }

    if (argc == 2)
    {
        if (strcmp(argv[1], "-d") == 0)
        {
            is_daemon = true;
        }
    }

    // Attach Signal Handlers
    struct sigaction signal_actions;
    signal_actions.sa_handler = signal_handler;
    signal_actions.sa_flags = 0;
    sigemptyset(&signal_actions.sa_mask);
    if (sigaction(SIGINT, &signal_actions, NULL) == -1)
    {
        perror("Error in registering SIGNINT\n");
    }
    if (sigaction(SIGTERM, &signal_actions, NULL) == -1)
    {
        perror("Error in registering SIGTERM\n");
    }

    int pthread_return;
    pthread_mutex_init(&output_file_mutex, NULL);

    // Start 10 second time for timestamp
    pthread_t timer_thread_id;
    pthread_attr_t attr;
    struct sched_param sched;
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    sched.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &sched);
    pthread_return = pthread_create(&timer_thread_id, &attr, timer_thread, NULL);
    if (pthread_return != 0)
    {
        perror("Error in creating timer thread\n");
    }

    // Setup socket
    struct sockaddr_in socket_address;

    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        perror("socket error:");
        return -1;
    }

    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        perror("Error in setsockopt");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *server_info = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, "9000", &hints, &server_info) != 0)
    {
        perror("Error in getaddrinfo");
        return -1;
    }

    if (bind(socket_fd, server_info->ai_addr, server_info->ai_addrlen) != 0)
    {
        perror("Error in bind");
        return -1;
    }

    free(server_info);

    if (is_daemon)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            perror("Error in Fork");
            return -1;
        }
        else if (pid > 0)
        {
            return 0;
        }
    }

    if (listen(socket_fd, 10) != 0)
    {
        perror("Error in listen");
        return -1;
    }

    struct thread_data *new_thread_data;
    struct thread_data *current_thread_data;

    SLIST_INIT(&thread_list);

    while (!is_signaled)
    {
        struct sockaddr client_info;
        socklen_t client_info_size = sizeof(client_info);

        int client_fd = accept(socket_fd, &client_info, &client_info_size);
        if (client_fd == -1)
        {
            perror("Error in accept");
            continue;
        }

        syslog(LOG_DEBUG, "Accepted connection from %u.%u.%u.%u : %u\n",
               (unsigned char)client_info.sa_data[2], (unsigned char)client_info.sa_data[3],
               (unsigned char)client_info.sa_data[4], (unsigned char)client_info.sa_data[5],
               (unsigned short)((unsigned short)(client_info.sa_data[0] << 8) | (unsigned short)client_info.sa_data[1]));

        // Create thread info
        new_thread_data = malloc(sizeof(struct thread_data));
        new_thread_data->done = 0;
        new_thread_data->client_fd = client_fd;

        pthread_return = pthread_create(&(new_thread_data->thread_id), NULL, server_thread, (void *)new_thread_data);
        if (pthread_return != 0)
        {
            perror("Error in creating server thread with error\n");
        }
        SLIST_INSERT_HEAD(&thread_list, new_thread_data, thread_entries);

        SLIST_FOREACH(current_thread_data, &thread_list, thread_entries)
        {
            if (current_thread_data->done == 1)
            {
                if (pthread_join(current_thread_data->thread_id, NULL) != 0)
                {
                    perror("Error in thread join");
                }
                current_thread_data->done = -1;
            }
        }

        syslog(LOG_DEBUG, "Closed connection from %u.%u.%u.%u : %u\n",
               (unsigned char)client_info.sa_data[2], (unsigned char)client_info.sa_data[3],
               (unsigned char)client_info.sa_data[4], (unsigned char)client_info.sa_data[5],
               (unsigned short)((unsigned short)(client_info.sa_data[0] << 8) | (unsigned short)client_info.sa_data[1]));
    }

    while (!SLIST_EMPTY(&thread_list))
    {
        current_thread_data = SLIST_FIRST(&thread_list);
        SLIST_REMOVE_HEAD(&thread_list, thread_entries);
        free(current_thread_data);
    }

    SLIST_INIT(&thread_list);

    if (pthread_join(timer_thread_id, NULL) != 0)
    {
        perror("Error in thread join for timer thread");
    }

    close(socket_fd);
    closelog();

    return 0;
}