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

int socket_fd;
bool is_signaled = false;

void signal_handler(int signal_number)
{
    if ((signal_number == SIGTERM) || (signal_number == SIGINT))
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        close(socket_fd);
        is_signaled = true;
    }
}

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    if(open("/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, 0644) > 0) {
		remove("/var/tmp/aesdsocketdata");
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
    if (sigaction(SIGINT, &signal_actions, NULL) == -1)
    {
        perror("Error in registering SIGNINT\n");
    }
    if (sigaction(SIGTERM, &signal_actions, NULL) == -1)
    {
        perror("Error in registering SIGTERM\n");
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
    hints.ai_family = AF_UNSPEC;
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

    char incoming_buffer[256];

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

        int out_fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_RDWR | O_APPEND, 0644);
        if (out_fd < 0)
        {
            perror("Error in opening data file");
            return -1;
        }

        bool is_the_end = false;
        while (!is_the_end && !is_signaled)
        {
            memset(incoming_buffer, 0, sizeof(incoming_buffer));
            int bytes_recv = recv(client_fd, incoming_buffer, sizeof(incoming_buffer), 0);
            // printf("Bytes Recvd %d\n", bytes_recv);

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

        lseek(out_fd, 0, SEEK_SET);
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
                if (send(client_fd, incoming_buffer, read_size, 0) < 0)
                {
                    perror("Error in send");
                    return -1;
                }
            }
        }

        close(out_fd);
        close(client_fd);

        syslog(LOG_DEBUG, "Closed connection from %u.%u.%u.%u : %u\n",
               (unsigned char)client_info.sa_data[2], (unsigned char)client_info.sa_data[3],
               (unsigned char)client_info.sa_data[4], (unsigned char)client_info.sa_data[5],
               (unsigned short)((unsigned short)(client_info.sa_data[0] << 8) | (unsigned short)client_info.sa_data[1]));
    }

    close(socket_fd);
    closelog();

    return 0;
}