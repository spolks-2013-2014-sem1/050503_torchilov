#include <stdio.h>
#include <stdlib.h>

int parsePort(char* portString)
{
    char* end;
    const short radix = 10;

    long port = strtol(portString, &end, radix);

    if (end != strchr(portString, '\0') || port < 0 || port > 65535) {
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

    int port = parsePort(argv[1]);

    return 0;
}
