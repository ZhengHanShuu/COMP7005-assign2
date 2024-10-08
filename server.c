#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

static void setup_signal_handler(void);
static void sigint_handler(int signum);
static void parse_arguments(int argc, char *argv[], in_port_t *port);
static int socket_create(int domain, int type, int protocol);
static void socket_bind(int sockfd, in_port_t port);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd);
static void *handle_connection(void *client_sockfd_ptr);
static void socket_close(int sockfd);
static void usage(const char *program_name, int exit_code, const char *message);
static int count_alphabetic_letters(const char *buffer, size_t length);

#define BASE_TEN 10
#define MAX_BUFFER_SIZE 4096

static volatile sig_atomic_t exit_flag = 0; // Global exit flag for SIGINT

int main(int argc, char *argv[])
{
    in_port_t port;

    parse_arguments(argc, argv, &port);
    int server_fd = socket_create(AF_INET, SOCK_STREAM, 0);
    socket_bind(server_fd, port);
    start_listening(server_fd, SOMAXCONN);
    setup_signal_handler();

    while (!exit_flag)
    {
        int client_fd = socket_accept_connection(server_fd);
        if (client_fd != -1)
        {
            pthread_t thread_id;
            int *client_sockfd_ptr = malloc(sizeof(int));
            if (client_sockfd_ptr)
            {
                *client_sockfd_ptr = client_fd;
                if (pthread_create(&thread_id, NULL, handle_connection, client_sockfd_ptr) != 0)
                {
                    perror("Failed to create thread");
                    socket_close(client_fd);
                    free(client_sockfd_ptr);
                }
                else
                {
                    pthread_detach(thread_id);
                }
            }
            else
            {
                perror("Failed to allocate memory for client socket");
                socket_close(client_fd);
            }
        }
    }

    socket_close(server_fd);
    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], in_port_t *port)
{
    if (argc != 2)
    {
        usage(argv[0], EXIT_FAILURE, "Usage: <port>");
    }
    *port = (in_port_t)atoi(argv[1]);
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd = socket(domain, type, protocol);
    if (sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

static void socket_bind(int sockfd, in_port_t port)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", port);
}

static void start_listening(int server_fd, int backlog)
{
    if (listen(server_fd, backlog) == -1)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
}

static int socket_accept_connection(int server_fd)
{
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1)
    {
        if (errno != EINTR)
        {
            perror("Accept failed");
        }
    }
    return client_fd;
}

static void *handle_connection(void *client_sockfd_ptr)
{
    int client_sockfd = *((int *)client_sockfd_ptr);
    free(client_sockfd_ptr);

    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read;
    int letter_count = 0;

    // Receive data from the client
    while ((bytes_read = read(client_sockfd, buffer, sizeof(buffer))) > 0)
    {
        letter_count += count_alphabetic_letters(buffer, bytes_read);
    }

    if (bytes_read < 0)
    {
        perror("Error reading from client socket");
    }

    // Send the letter count back to the client
    char response[64];
    snprintf(response, sizeof(response), "%d", letter_count);
    ssize_t response_length = strlen(response);
    if (send(client_sockfd, response, response_length, 0) < 0)
    {
        perror("Failed to send response to client");
    }

    printf("Sent letter count: %d\n", letter_count);
    socket_close(client_sockfd);
    return NULL;
}

static int count_alphabetic_letters(const char *buffer, size_t length)
{
    int count = 0;
    for (size_t i = 0; i < length; i++)
    {
        if ((buffer[i] >= 'a' && buffer[i] <= 'z') || (buffer[i] >= 'A' && buffer[i] <= 'Z'))
        {
            count++;
        }
    }
    return count;
}

static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

static void socket_close(int sockfd)
{
    if (close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void usage(const char *program_name, int exit_code, const char *message)
{
    if (message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s <port>\n", program_name);
    exit(exit_code);
}

