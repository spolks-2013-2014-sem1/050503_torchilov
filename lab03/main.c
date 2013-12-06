#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

#define BUFFER_SIZE 2048
#define ADDITIONAL_BUFFER_SIZE 256

void send_file(char* host, int port, char* file_path);
void print_error(char* message, int error_code);
long get_file_size(FILE* file);
void get_file(char host, int port);

int client_socket_descriptor = -1;
int server_socket_descriptor = -1;

int main(int argc, char* argv[])
{
    if (argc < 3 || argc > 4) {
        print_error("Invalid params.\nUsage: [hostname] [port] <file path>", 1);
    }



    return 0;
}

void send_file(char* host, int port, char* file_path) {
    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;

    struct hostent* host_name = gethostbyname(host);

    if (host_name != NULL) {
        memcpy(&server_address.sin_addr, host_name->h_addr, host_name->h_length);
    } else {
        print_error("Invalid server host", 2);
    }

    server_address.sin_port = htons(port);

    struct protoent* protocol_type = getprotobyname("TCP");
    client_socket_descriptor = socket(AF_INET, SOCK_STREAM, protocol_type->p_proto);

    if (client_socket_descriptor < 0) {
        print_error("Can't create client socket", 3);
    }

    if (connect(client_socket_descriptor, (struct sockaddr*) &server_address,
                sizeof(server_address)) < 0) {
        print_error("Error in connect", 4);
    }

    FILE *file = fopen(file_path, "r+");

    if (file == NULL) {
        print_error("Can't open file in r+ mode", 5);
    }

    long file_size = get_file_size(file);
    char header_buffer[256];

    sprintf(header_buffer, "%s:%ld", basename(file_path), file_size);
    if (send(client_socket_descriptor, header_buffer, sizeof(header_buffer), 0) == -1) {
        print_error("Send header error", 6);
    }

    char buffer[BUFFER_SIZE];
    long number_of_send_bytes = 0;
    size_t number_of_read_bytes;

    while (number_of_send_bytes < file_size) {
        number_of_read_bytes = fread(buffer, 1, sizeof(buffer), file);

        int temp_send = send(client_socket_descriptor, buffer, number_of_read_bytes, 0);

        if (temp_send < 0) {
            print_error("Error while sending file", 7);
        }
        number_of_send_bytes += temp_send;
    }

    printf("Sending finished\n");

    close(client_socket_descriptor);
    fclose(file);
}

void get_file(chat* host, int port) {
	struct sockaddr_in client_address;
	
	memset(&server_address, 0, sizeof(client_address));
    client_address.sin_family = AF_INET;

    struct hostent* host_name = gethostbyname(host);

    if (host_name != NULL) {
        memcpy(&client_address.sin_addr, host_name->h_addr, host_name->h_length);
    } else {
        print_error("Invalid client host", 8);
    }

    client_address.sin_port = htons(port);

    struct protoent* protocol_type = getprotobyname("TCP");
    server_socket_descriptor = socket(AF_INET, SOCK_STREAM, protocol_type->p_proto);

    if (server_socket_descriptor < 0) {
        print_error("Can't create server socket", 3);
    } 
	

}

void print_error(char* message, int error_code) {
    fprintf(stderr, message);
    exit(error_code);
}

long get_file_size(FILE* file) {
    long current_position = ftell(file);

    fseek(file, 0L, SEEK_END);

    long full_size = ftell(file);

    fseek(file, current_position, SEEK_SET);

    return full_size;
}
