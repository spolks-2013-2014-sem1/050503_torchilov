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

void send_file(char* host, int port, char* file_path);
void print_error(char* message, int error_code);
long get_file_size(FILE* file);
void get_file(char* host, int port);
void intterrupt(int signo);
FILE *create_file(char *file_name, const char *folder_name);
void urgHandler(int signo);

int client_socket_descriptor = -1;
int server_socket_descriptor = -1;

int oobFlag = 0;

int main(int argc, char* argv[])
{
    if (argc < 3 || argc > 4) {
        print_error("Invalid params.\nUsage: [hostname] [port] <file path>", 1);
    }
	
	struct sigaction signal;
    signal.sa_handler = intterrupt;
    sigaction(SIGINT, &signal, NULL);

    if (argc == 3) {
        get_file(argv[1], atoi(argv[2]));
	} else {
        send_file(argv[1], atoi(argv[2]), argv[3]);
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
                perror("Sending error");
                exit(EXIT_FAILURE);
            }
        }		
    }

    printf("Sending file completed. Total bytes sent: %ld\n",
           number_of_send_bytes);

    close(client_socket_descriptor);
    fclose(file);
}

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
		
		struct sigaction urgSignal;
        urgSignal.sa_handler = urgHandler;
        sigaction(SIGURG, &urgSignal, NULL);

        if (fcntl(descriptor, F_SETOWN, getpid()) < 0) {
            perror("fcntl()");
            exit(-1);
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
			if (sockatmark(descriptor) == 1 && oobFlag == 1) {
                printf("Receive OOB byte. Total bytes received: %ld\n", get_length);

                char oobBuf;
                int n = recv(descriptor, &oobBuf, 1, MSG_OOB);
				
                if (n == -1) {
					fprintf(stderr, "receive OOB error\n");
				}
                oobFlag = 0;
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

long get_file_size(FILE* file) {
    long current_position = ftell(file);

    fseek(file, 0L, SEEK_END);

    long full_size = ftell(file);

    fseek(file, current_position, SEEK_SET);

    return full_size;
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


void urgHandler(int signo)
{
    oobFlag = 1;
}

