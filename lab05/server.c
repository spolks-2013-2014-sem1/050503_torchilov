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

#ifndef F_SETOWN
#define F_SETOWN	8	/* Get owner (process receiving SIGIO).  */
#endif

#define BUFFER_SIZE 2048
#define ADDITIONAL_BUFFER_SIZE 256
#define MAX_LENGTH 10
#define h_addr h_addr_list[0]

void print_error(char* message, int error_code);
void get_file(char* host, int port);
void intterrupt(int signo);
FILE *create_file(char *file_name, const char *folder_name);
void urg_handler(int signo);

int server_socket_descriptor = -1;

int oob_flag = 0;

void get_file(char* host, int port) {
	struct sockaddr_in client_address;
	
	memset(&client_address, 0, sizeof(client_address));
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
        print_error("Can't create server socket", 9);
    } 
	
	int bool_option = 1;
	
	if (setsockopt(server_socket_descriptor, 
		SOL_SOCKET, SO_REUSEADDR, &bool_option, sizeof(int)) == -1) {
		print_error("Can't set reuse address socket option", 10);
	}
	
	if (bind(server_socket_descriptor, (struct sockaddr*) &client_address, sizeof(client_address)) < 0) {
		print_error("Can't bind socket", 11);
	}
	
	if (listen(server_socket_descriptor, MAX_LENGTH) == -1) {
		print_error("Listen socket error", 12);
	}
	
	char header_buffer[ADDITIONAL_BUFFER_SIZE], buffer[BUFFER_SIZE];
	
	while (1) {
		printf("\nServer waiting\n");
		
		int descriptor = accept(server_socket_descriptor, NULL, NULL);
		
		struct sigaction urg_signal;
        urg_signal.sa_handler = urg_handler;
        sigaction(SIGURG, &urg_signal, NULL);

        if (fcntl(descriptor, F_SETOWN, getpid()) < 0) {
            print_error("fcntl", 21)
        }
		
		if (save_data_to_buffer(descriptor, header_buffer, sizeof(header_buffer)) <= 0) {
			close(descriptor);
			printf("Error in sending header\n");
			continue;
		}
		
		char *file_name = strtok(header_buffer, ":");
        if (file_name == NULL) {
            close(descriptor);
            printf("Error in file name\n");
            continue;
        }
		
		char *size = strtok(NULL, ":");
		
		if (size == NULL) {
			close(descriptor);
			printf("Error in file size\n");
			continue;
		}
		
		long file_size = atoi(size);
		
		FILE *file = create_file(file_name, "temp_folder");
		
		if (file == NULL) {
			print_error("Error in creating file", 11);
		}
		
		fd_set server_fds;
		FD_ZERO(&server_fds);
		FD_SET(descriptor, &server_fds);
	
		struct timespec timeout;
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;
		int temp = 1;
		
		
		long get_length = 0;
		int read_length;
		
		while (1) {
			if (sockatmark(descriptor) == 1 && oob_flag == 1) {
                printf("Receive OOB byte. Total bytes received: %ld\n", get_length);

                char oobBuf;
                int n = recv(descriptor, &oobBuf, 1, MSG_OOB);
				
                if (n == -1) {
					fprintf(stderr, "receive OOB error\n");
				}
                oob_flag = 0;
            }	
		
			read_length = recv(descriptor, buffer, sizeof(buffer), 0);
			
			temp = pselect(descriptor + 1, NULL, &server_fds, NULL, &timeout, NULL);
			
			if (temp == 0 || temp == -1){
				close(descriptor);
				fclose(file);
				print_error("Connection terminated\n", 666);
			}
			
			if (read_length == 0) {
				break;
			} else if (read_length > 0) {
				fwrite(buffer, 1, read_length, file);
                get_length += read_length;
			} else if (errno == EINTR) {
                    continue;
			} else {
				printf("Unknown error");
				break;
			} 
		}
		fclose(file);
		printf("File obtained: %ld", get_length);
		close(descriptor);
	}
}

void print_error(char* message, int error_code) {
    fprintf(stderr, message);
    exit(error_code);
}

int save_data_to_buffer(int descriptor, char* buffer, int length) {
	int get_length = 0;
	int read_length;
	
	fd_set server_fds;
	FD_ZERO(&server_fds);
	FD_SET(descriptor, &server_fds);
	
	struct timespec timeout;
	timeout.tv_sec = 10;
	timeout.tv_nsec = 0;
	int temp = 1;
	
	while (get_length < length) {
		read_length = recv(descriptor, buffer + get_length, length - get_length, 0);
		
		temp = pselect(descriptor + 1, NULL, &server_fds, NULL, &timeout, NULL);
			
		if (temp == 0 || temp == -1){
			close(descriptor);
			print_error("Connection terminated\n", 666);
		}
		
		if (read_length == 0) {
			break;
		} else if (read_length < 0) {
			return -1;
		} else {
			get_length += read_length;
		}
	}
	
	return get_length;
}

FILE *create_file(char *file_name, const char *folder_name)
{
    char file_path[4096];

    struct stat st = { 0 };
    if (stat(folder_name, &st) == -1) {
        if (mkdir(folder_name, 0777) == -1) {
            return NULL;
		}
    }

    strcpy(file_path, folder_name);
    strcat(file_path, "/");
    strcat(file_path, file_name);

    return fopen(file_path, "wb");
}

void intterrupt(int signo)
{
    if (server_socket_descriptor != -1)
        close(server_socket_descriptor);
    else
        close(client_socket_descriptor);
    _exit(0);
}

void urg_handler(int signo)
{
    oob_flag = 1;
}