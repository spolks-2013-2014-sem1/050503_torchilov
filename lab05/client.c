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

void send_file_tcp(char* host, int port, char* file_path);
void send_file_udp(char* host, int port, char* file_path);
void print_error(char* message, int error_code);
void intterrupt(int signo);
long get_file_size(FILE* file);

const unsigned char ACK = 1;
int client_socket_descriptor = -1;

int main(int argc, char *argv[])
{
	if (argc < 4 || argc > 5) {
        print_error("Invalid params.\nUsage: [-u] [hostname] [port] <file path>", 1);
		return 1;
    }
	
	struct sigaction signal;
    signal.sa_handler = intterrupt;
    sigaction(SIGINT, &signal, NULL);

    if (argc == 4) {
        send_file_tcp(argv[2], atoi(argv[3]), argv[4]);
	} else {
        send_file_udp(argv[1], atoi(argv[2]), argv[3]);
	}

    return 0;
}

void send_file_udp(char* host, int port, char* file_path) {
	struct sockaddr_in server_address;
	int client_socket_descriptor_udp;
	
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	
	struct hostent* host_name = gethostbyname(host);
	
	if (host_name != NULL) {
		memcpy(&server_address.sin_addr, host_name->h_addr, host_name->h_length);
	} else {
		print_error("Invalid server host in udp", 13);
	}
	
	server_address.sin_port = htons(port);
	
	struct protoent* protocol_type = getprotobyname("UDP");
	client_socket_descriptor_udp = socket(AF_INET, SOCK_DGRAM, protocol_type->p_proto);
	if (client_socket_descriptor_udp < 0) {
		print_error("Error in creating socket udp\n", 14);
	}
	
	if (connect(client_socket_descriptor_udp, (struct sockaddr*) &server_address,
                sizeof(server_address)) < 0) {
		print_error("Error in connect udp\n", 15);
	}
	
	struct timeval tv;
    tv.tv_sec = 30;             
    tv.tv_usec = 0;
    setsockopt(client_socket_descriptor_udp, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv,
               sizeof(struct timeval));
	
	char header_buffer[256];
	unsigned char flag_buffer;
	
	FILE *file = fopen(file_path, "r+");

    if (file == NULL) {
        print_error("Can't open file in r+ mode", 5);
    }
	
	long file_size = get_file_size(file);
	
	sprintf(header_buffer, "%s:%ld", basename(file_path), file_size);
	
	int bytes_transmitted = send(client_socket_descriptor_udp, header_buffer, sizeof(header_buffer), 0);
	
	if (bytes_transmitted == -1) {
        print_error("Send header error", 6);
    }
	
	bytes_transmitted = recv(client_socket_descriptor_udp, &flag_buffer, sizeof(flag_buffer), 0);
    if (bytes_transmitted == -1 || flag_buffer != ACK) {
        print_error("No server response\n", 16);
    }
	
	    printf("Start sending file.\n");
    char buffer[BUFFER_SIZE];
    long total_bytes_sent = 0;
    size_t bytes_read;

    // Sending file
    while (total_bytes_sent < file_size) {

        bytes_read = fread(buffer, 1, sizeof(buffer), file);
        int bytes_transmitted = send(client_socket_descriptor_udp, buffer, bytes_read, 0);
        if (bytes_transmitted < 0) {
            print_error("Error in sending", 18);
        }
        total_bytes_sent += bytes_transmitted;

        bytes_transmitted =
            recv(client_socket_descriptor_udp, &flag_buffer, sizeof(flag_buffer), 0);
        if (bytes_transmitted == -1 || flag_buffer != ACK) {
            print_error("No server response\n", 19);
        }
    }
    printf("Sending file completed. Total bytes sent: %ld\n",
           total_bytes_sent);
}

void send_file_tcp(char* host, int port, char* file_path) {
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
	if (client_socket_descriptor != -1) {
		close(client_socket_descriptor);
	}
	
    _exit(0);
}