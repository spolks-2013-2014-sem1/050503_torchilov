#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 128

void print_error(char* text, int error_code)
{
    printf(text);
    exit(error_code);
}

int parse_port(char* port_string)
{
    char* end;
    const short radix = 10;

    long port = strtol(port_string, &end, radix);

    if (end != strchr(port_string, '\0') || port < 0 || port > 65535) {
        print_error("Invalid value for port.\n", 1);
    }

    return port;
}


int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_error("Enter port for listening.\n", 2);
    }

    int port = parse_port(argv[1]);
    int server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket_descriptor < 0) {
        print_error("Error in socket creating.\n", 3);
    }

    struct sockaddr_in server_address;

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket_descriptor, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
        print_error("Error in binding server address.\n", 4);
        close(server_socket_descriptor);
    }

    if (listen(server_socket_descriptor, 3) == -1) {
        print_error("Error in listen.\n", 5);
        close(server_socket_descriptor);
    }

    struct sockaddr_in client_address;
    int client_socket_descriptor;
    socklen_t length = sizeof(struct sockaddr_in);
    char buffer[BUFFER_SIZE];

    while (1) {
        client_socket_descriptor = accept(server_socket_descriptor, (struct sockaddr*) &client_address, &length);

        if (client_socket_descriptor < 0) {
            print_error("Error in accept.\n", 6);
            close(server_socket_descriptor);
        }

        printf("Transmission started.\n");

        while(1) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = recv(client_socket_descriptor, buffer, BUFFER_SIZE, 0);

            if (bytes_read != 0) {
                printf("%s", buffer);
                send(client_socket_descriptor, buffer, sizeof(buffer), 0);
            } else {
                printf("Transmission closed.\n");
                close (client_socket_descriptor);
                break;
            }
        }
    }

    return 0;
}

