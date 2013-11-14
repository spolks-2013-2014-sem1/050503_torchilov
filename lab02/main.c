#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int parse_port(char* port_string)
{
    char* end;
    const short radix = 10;

    long port = strtol(port_string, &end, radix);

    if (end != strchr(port_string, '\0') || port < 0 || port > 65535) {
        printf("Invalid value for port.\n");
        exit(1);
    }

    return port;
}


int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Enter port for listening.\n");

        return 2;
    }

    int port = parse_port(argv[1]);
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener < 0) {
        printf("Error in socket creating.\n");

        return 3;
    }

    struct sockaddr_in server_address;

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(listener, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
        printf("Error in binding server address.\n");

        return 4;
    }

    return 0;
}
