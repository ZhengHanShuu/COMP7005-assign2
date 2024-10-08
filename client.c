#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void           parse_arguments(int argc, char *argv[], char **target_address, char **port, char **file_path);
static void           handle_arguments(const char *binary_name, const char *target_address, const char *port_str, in_port_t *port, const char *file_path);
static in_port_t      parse_in_port_t(const char *binary_name, const char *port_str);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
static void           convert_address(const char *address, struct sockaddr_storage *addr);
static int            socket_create(int domain, int type, int protocol);
static void           socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void           shutdown_socket(int client_fd, int how);
static void           socket_close(int client_fd);

#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define MAX_BUFFER_SIZE 4096
#define LINE_LEN 1024
#define BASE_TEN 10

int main(int argc, char *argv[])
{
    char                   *file_path;
    FILE                   *file;
    char                   *address;
    char                   *port_str;
    in_port_t               port;
    struct sockaddr_storage addr;
    int                     sockfd;
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read;

    char                    line[LINE_LEN];
    char                   *saveptr;

    address  = NULL;
    port_str = NULL;
    file_path = NULL;
    parse_arguments(argc, argv, &address, &port_str, &file_path);
    handle_arguments(argv[0], address, port_str, &port, file_path);
    file = fopen(file_path, "rb");

    if(file == NULL) {

        perror("fopen");
        exit(EXIT_FAILURE);
    }

    convert_address(address, &addr);
    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
    socket_connect(sockfd, &addr, port);

//    while(fgets(line, sizeof(line), file) != NULL)
//    {
//        const char *word;
//
//        word = strtok_r(line, " \t\n", &saveptr);
//
//        while(word != NULL)
//        {
//            size_t  word_len;
//            uint8_t size;
//
//            word_len = strlen(word);
//
//            if(word_len > UINT8_MAX)
//            {
//                fprintf(stderr, "Word exceeds maximum length\n");
//                fclose(file);
//                close(sockfd);
//                exit(EXIT_FAILURE);
//            }
//
//            // Write the size of the word as uint8_t
//            size = (uint8_t)word_len;
//            send(sockfd, &size, sizeof(uint8_t), 0);
//
//            // Write the word
//            write(sockfd, word, word_len);
//            word = strtok_r(NULL, " \t\n", &saveptr);
//        }
//    }
    // Send the file contents to the server
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (send(sockfd, buffer, bytes_read, 0) < 0)
        {
            perror("send");
            fclose(file);
            socket_close(sockfd);
            exit(EXIT_FAILURE);
        }
    }
    fclose(file);
    shutdown_socket(sockfd, SHUT_WR);

    // Wait for the server's response
    char response[64]; // Buffer to hold the server response
    ssize_t response_length = recv(sockfd, response, sizeof(response) - 1, 0);
    if (response_length < 0)
    {
        perror("recv");
        socket_close(sockfd);
        exit(EXIT_FAILURE);
    }
    response[response_length] = '\0'; // Null-terminate the response

    printf("Server response: %s\n", response);
    socket_close(sockfd);

    return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **target_address, char **port, char **file_path)
{
    int opt;

    opterr = 0;

    while((opt = getopt(argc, argv, "h")) != -1)
    {
        switch(opt)
        {
            case 'h':
            {
                usage(argv[0], EXIT_SUCCESS, NULL);
            }
            case '?':
            {
                char message[UNKNOWN_OPTION_MESSAGE_LEN];

                snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                usage(argv[0], EXIT_FAILURE, message);
            }
            default:
            {
                usage(argv[0], EXIT_FAILURE, NULL);
            }
        }
    }

    if(optind + 2 >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "Too few arguments.");
    }

    if(optind < argc - 3)
    {
        usage(argv[0], EXIT_FAILURE, "Too many arguments.");
    }

    *target_address = argv[optind];
    *port           = argv[optind + 1];
    *file_path      = argv[optind + 2];
}

static void handle_arguments(const char *binary_name, const char *target_address, const char *port_str, in_port_t *port, const char *file_path)
{
    if(target_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The target address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    if(file_path == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The file name is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <target address> <port> <file_path>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    exit(exit_code);
}

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if(inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len            = sizeof(struct sockaddr_in);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len             = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        const char *msg;

        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

static void shutdown_socket(int client_fd, int how)
{
    if(shutdown(client_fd, how) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

static void socket_close(int client_fd)
{
    if(close(client_fd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

