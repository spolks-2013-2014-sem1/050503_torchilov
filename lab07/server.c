#ifdef __cplusplus 
extern "C" {
#endif

#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus 
}
#endif

#include <iostream>
#include <map>
#include <list>
#include <cstdint>

#define replyBufSize 256
#define bufSize 4096
#define MAX_LENGTH 10
#define h_addr h_addr_list[0]

using namespace std;

void get_file_tcp(char *serverName, unsigned int port);
void get_file_udp(char *serverName, unsigned int port);

void *tcp_processing_thread(void *ptr);
void *udp_processing_thread(void *ptr);

void print_error(char* message, int error_code);
FILE *create_file(char *file_name, const char *folder_name);
char *get_file_size_pointer(char *str, int size);
uint64_t ip_port_to_number(uint32_t IPv4, uint16_t port);
void safe_print(pthread_mutex_t* mutex, const char* message, ...);
int receive_to_buf(int descriptor, char *buf, int len);

int server_socket_descriptor = -1;
pthread_mutex_t printMutex;

void interrupt(int signo)
{
    if (server_socket_descriptor != -1)
        close(server_socket_descriptor);

    _exit(0);
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: main [-u] <host> <port>\n");
        return 1;
    }

    struct sigaction signal;
    signal.sa_handler = interrupt;
    sigaction(SIGINT, &signal, NULL);

    if (argc == 3)
        get_file_tcp(argv[1], atoi(argv[2]));
    else
        get_file_udp(argv[2], atoi(argv[3]));

    return 0;
}

void get_file_tcp(char *host, unsigned int port)
{
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

    if (pthread_mutex_init(&printMutex, NULL) != 0) {
        cerr << "Initialize mutex error...\n";
        exit(EXIT_FAILURE);
    }

    list < pthread_t > threads;

    while (1) {

        intptr_t rsd = accept(server_socket_descriptor, NULL, NULL);
        if (rsd == -1) {
            perror("Accept");
            exit(EXIT_FAILURE);
        }

        pthread_t th;
        if (pthread_create(&th, NULL, tcp_processing_thread, (void *) rsd)
            != 0) {
            cerr << "Creating thread error\n";
            exit(EXIT_FAILURE);
        }

        threads.push_back(th);

        list < pthread_t >::iterator i = threads.begin();
        while (i != threads.end()) {
            if (pthread_tryjoin_np(*i, NULL) == 0)
                i = threads.erase(i);
            else
                i++;
        }
    }
}

void *tcp_processing_thread(void *ptr)
{
    int rsd = (intptr_t) ptr;
    char replyBuf[replyBufSize], buf[bufSize];

    // Receive file name and file size
    if (receive_to_buf(rsd, replyBuf, sizeof(replyBuf)) <= 0) {
        close(rsd);
        fprintf(stderr, "Error receiving file name and file size\n");
        return NULL;
    }

    char *size = get_file_size_pointer(replyBuf, sizeof(replyBuf));
    if (size == NULL) {
        close(rsd);
        fprintf(stderr, "Bad file size\n");
        return NULL;
    }
    long fileSize = atoi(size);

    char *fileName = replyBuf;

    safe_print(&printMutex, "File size: %ld, file name: %s\n", fileSize,
               fileName);

    FILE *file = create_file(fileName, "temp_folder");
    if (file == NULL) {
        perror("Create file error");
        exit(EXIT_FAILURE);
    }
    // Receiving file   
    int recvSize;
    long total_bytes_received = 0;

    fd_set rset, xset;
    FD_ZERO(&rset);
    FD_ZERO(&xset);

    while (total_bytes_received < fileSize) {

        FD_SET(rsd, &rset);
        select(rsd + 1, &rset, NULL, &xset, NULL);

        if (FD_ISSET(rsd, &xset)) {
            safe_print(&printMutex,
                       "Receive OOB byte. Total bytes of \"%s\" received: %ld\n",
                       fileName, total_bytes_received);

            char oobBuf;
            int n = recv(rsd, &oobBuf, 1, MSG_OOB);
            if (n == -1)
                fprintf(stderr, "receive OOB error\n");
            FD_CLR(rsd, &xset);
        }

        if (FD_ISSET(rsd, &rset)) {
            recvSize = recv(rsd, buf, sizeof(buf), 0);

            if (recvSize > 0) {
                total_bytes_received += recvSize;
                fwrite(buf, 1, recvSize, file);
            } else if (recvSize == 0) {
                safe_print(&printMutex, "Received EOF\n");
                break;
            } else {
                if (errno == EINTR)
                    continue;
                else {
                    perror("receive error");
                    break;
                }
            }
            FD_SET(rsd, &xset);
        }
    }
    fclose(file);
    safe_print(&printMutex,
               "Receiving file \"%s\" completed. %ld bytes received.\n",
               fileName, total_bytes_received);
    close(rsd);

    return NULL;
}

struct fileInfo {
    FILE *file;
    char fileName[256];
    long total_bytes_received;
    long fileSize;
};

struct udo_args {
    char buf[bufSize];
    int recvSize;
    struct sockaddr_in addr;
};

int UdpServerDescr = -1;

const unsigned char ACK = 1;

map < uint64_t, fileInfo * >filesMap;
pthread_mutex_t mapMutex;

void get_file_udp(char *host, unsigned int port)
{
    struct sockaddr_in client_address;

    int server_socket_descriptor_udp;
	
	memset(&client_address, 0, sizeof(client_address));
	client_address.sin_family = AF_INET;
	
	struct hostent* host_name = gethostbyname(host);
	
	if (host_name != NULL) {
		memcpy(&client_address.sin_addr, host_name->h_addr, host_name->h_length);
	} else {
		print_error("Invalid server host in udp", 13);
	}
	
	client_address.sin_port = htons(port);
	
	struct protoent* protocol_type = getprotobyname("UDP");
	server_socket_descriptor_udp = socket(AF_INET, SOCK_DGRAM, protocol_type->p_proto);
	if (server_socket_descriptor_udp < 0) {
		print_error("Error in creating socket udp\n", 14);
	}
	
	if (bind(server_socket_descriptor_udp, (struct sockaddr*) &client_address, sizeof(client_address)) < 0) {
		print_error("Can't bind socket in udp", 20);
	}

    if (pthread_mutex_init(&mapMutex, NULL) != 0) {
        fprintf(stderr, "Initialize mutex error...\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&printMutex, NULL) != 0) {
        fprintf(stderr, "Initialize mutex error...\n");
        exit(EXIT_FAILURE);
    }

    struct timeval timeOut = {30, 0}, noTimeOut = {0, 0};

    struct sockaddr_in remote;
    socklen_t rlen = sizeof(remote);
    int recvSize;

    list < pthread_t > threads;

    while (1) {
        pthread_mutex_lock(&mapMutex);
        if (filesMap.size() > 1)
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &timeOut, 
                       sizeof(struct timeval));      // set timeout
        else
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &noTimeOut, 
                       sizeof(struct timeval));    // disable timeout
        pthread_mutex_unlock(&mapMutex);

        struct udo_args *arg = new udo_args;        // argument for new thread      

        recvSize =
            recvfrom(UdpServerDescr, arg->buf, sizeof(arg->buf), 0,
                     (struct sockaddr *) &remote, &rlen);
        if (recvSize < 0) {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        arg->addr = remote;
        arg->recvSize = recvSize;

        pthread_t th;
        if (pthread_create(&th, NULL, udp_processing_thread, (void *) arg)
            != 0) {
            fprintf(stderr, "Creating thread error\n");
            exit(EXIT_FAILURE);
        }
        threads.push_back(th);

        list < pthread_t >::iterator i = threads.begin();
        while (i != threads.end()) {
            if (pthread_tryjoin_np(*i, NULL) == 0)
                i = threads.erase(i);
            else
                i++;
        }
    }
}


void *udp_processing_thread(void *ptr)
{
    struct udo_args *arg = (struct udo_args *) ptr;

    socklen_t rlen = sizeof(arg->addr);

    uint64_t address =
        ip_port_to_number(arg->addr.sin_addr.s_addr, arg->addr.sin_port);

    pthread_mutex_lock(&mapMutex);
    map < uint64_t, fileInfo * >::iterator pos = filesMap.find(address);
    pthread_mutex_unlock(&mapMutex);

    // client address not found in array
    if (pos == filesMap.end()) {

        char *size = get_file_size_pointer(arg->buf, sizeof(arg->buf));
        if (size == NULL) {
            fprintf(stderr, "Bad file size\n");
            return NULL;
        }
        long fileSize = atoi(size);

        char *fileName = arg->buf;

        safe_print(&printMutex, "File size: %ld, file name: %s\n",
                   fileSize, fileName);

        FILE *file = create_file(fileName, "temp_folder");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        struct fileInfo *info = new fileInfo;
        *info = {file, {0}, 0, fileSize};
        strcpy(info->fileName, fileName);

        pthread_mutex_lock(&mapMutex);
        filesMap[address] = info;
        pthread_mutex_unlock(&mapMutex);

    } else {
        struct fileInfo *info = pos->second;
        info->total_bytes_received += arg->recvSize;

        fwrite(arg->buf, 1, arg->recvSize, info->file);

        if (info->total_bytes_received == info->fileSize) {
            safe_print(&printMutex, "File \"%s\" received\n",
                       info->fileName);
            fclose(info->file);
            delete info;

            pthread_mutex_lock(&mapMutex);
            filesMap.erase(pos);
            pthread_mutex_unlock(&mapMutex);
        }
    }

    int bytesTransmitted = sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                                  (struct sockaddr *) &(arg->addr), rlen);
    if (bytesTransmitted < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }

    delete arg;
    return NULL;
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

void print_error(char* message, int error_code) {
    fprintf(stderr, message);
    exit(error_code);
}

char *get_file_size_pointer(char *str, int size)
{
    for (int i = 0; i < size; i++) {
        if (str[i] == ':') {
            str[i] = 0;
            return &str[i + 1];
        }
    }
    return NULL;
}

uint64_t ip_port_to_number(uint32_t IPv4, uint16_t port)
{
    return (((uint64_t) IPv4) << 16) | (uint64_t) port;
}

void safe_print(pthread_mutex_t* mutex, const char* message, ...)
{        
    va_list args;
    va_start( args, message );

        pthread_mutex_lock(mutex);
    vprintf(message, args);
        pthread_mutex_unlock(mutex);

    va_end(args);        
}

int receive_to_buf(int descriptor, char *buf, int len)
{
    int recvSize = 0;
    int number_of_bytes;
    while (recvSize < len) {
        number_of_bytes =
            recv(descriptor, buf + recvSize, len - recvSize, 0);
        if (number_of_bytes == 0)
            break;
        else if (number_of_bytes < 0)
            return -1;
        else
            recvSize += number_of_bytes;
    }
    return recvSize;
}
