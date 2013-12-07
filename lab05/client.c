#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <sys/stat.h>
#include <libgen.h>
#include <bits/sigaction.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 2048
#define ADDITIONAL_BUFFER_SIZE 256
#define MAX_LENGTH 10
#define h_addr h_addr_list[0]

void send_file(char* host, int port, char* file_path);
void print_error(char* message, int error_code);
void intterrupt(int signo);
long get_file_size(FILE* file);

int client_socket_descriptor = -1;

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
	
	fd_set server_fds;
	FD_ZERO(&server_fds);
    FD_SET(client_socket_descriptor, &server_fds);
	
	struct timespec timeout;
	timeout.tv_sec = 10;
    timeout.tv_nsec = 0;
	int temp = 1;

    sprintf(header_buffer, "%s:%ld", basename(file_path), file_size);
		
	
    if (send(client_socket_descriptor, header_buffer, sizeof(header_buffer), 0) == -1) {
        print_error("Send header error", 6);
    }	

    char buffer[BUFFER_SIZE];
    long number_of_send_bytes = 0;
    size_t number_of_read_bytes;
	
	int middle = (file_size / BUFFER_SIZE) / 2;
    if (middle == 0) {
		middle = 1;
	}

	printf("Start sending file.\n");
    int i = 0;
	
    while (number_of_send_bytes < file_size) {
        number_of_read_bytes = fread(buffer, 1, sizeof(buffer), file);
		
		temp = pselect(client_socket_descriptor + 1, NULL, &server_fds, NULL, &timeout, NULL);
		if (temp == 0 || temp == -1){
			close(client_socket_descriptor);
			fclose(file);
			print_error("Connection terminated\n", 666);
		}
		
        int temp_send = send(client_socket_descriptor, buffer, number_of_read_bytes, 0);

        if (temp_send < 0) {
            print_error("Error while sending file", 7);
        }
        number_of_send_bytes += temp_send;
		
        if (++i == middle) {
            printf("Sent OOB byte. Total bytes sent: %ld\n",   number_of_send_bytes);
            temp_send = send(client_socket_descriptor, "!", 1, MSG_OOB);
            if (temp_send < 0) {
                print_error("Sending_error", 22);
            }
        }		
    }

    printf("Sending file completed. Total bytes sent: %ld\n",
           number_of_send_bytes);

    close(client_socket_descriptor);
    fclose(file);
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

void intterrupt(int signo)
{
    if (server_socket_descriptor != -1)
        close(server_socket_descriptor);
    else
        close(client_socket_descriptor);
    _exit(0);
}