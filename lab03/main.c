#include <stdio.h>
#include <stdlib.h>

void print_error(char* message, int error_code) {
    fprintf(stderr, message);
    exit(error_code);
}

int main(int argc, char* argv[])
{
    if (argc < 3 || argc > 4) {
        print_error("Invalid params.\nUsage: [hostname] [port] <file path>", 1);
    }



    return 0;
}
