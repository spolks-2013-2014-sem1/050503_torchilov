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

#ifdef __cplusplus 
}
#endif

#include <cstdint>
#include <iostream>
#include <map>

#define replyBufSize 256
#define bufSize 4096
#define MAX_LENGTH 10
#define h_addr h_addr_list[0]

using namespace std;

void receiveFileTCP(char *serverName, unsigned int port);
void receiveFileUDP(char *serverName, unsigned int port);

void TCP_Processing(int rsd);
void UDP_Processing(unsigned char *buf, int size, struct sockaddr_in &addr);
char *getFileSizePTR(char *str, int size);
void print_error(char* message, int error_code);
uint64_t IpPortToNumber(uint32_t IPv4, uint16_t port);
int receive_to_buf(int descriptor, char *buf, int len);
FILE *create_file(char *file_name, const char *folder_name);

int server_socket_descriptor = -1;

void intHandler(int signo)
{
    if (server_socket_descriptor != -1)
        close(server_socket_descriptor);

    _exit(0);
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: main <host> <port> [-u]\n");
        return 1;
    }
    
    struct sigaction intSignal;
    intSignal.sa_handler = intHandler;
    sigaction(SIGINT, &intSignal, NULL);

    signal(SIGCHLD, SIG_IGN);   // make SIGCHLD ignored to avoid zombies

    if (argc == 3)
        receiveFileTCP(argv[1], atoi(argv[2]));
    else
        receiveFileUDP(argv[1], atoi(argv[2]));

    return 0;
}

void receiveFileTCP(char *host, unsigned int port)
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

    while (1) {

        int rsd = accept(server_socket_descriptor, NULL, NULL);
        if (rsd == -1) {
            perror("Accept");
            exit(EXIT_FAILURE);
        }

        switch (fork()) {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);

        case 0:                // child process
            TCP_Processing(rsd);
            return;

        default:               // parrent process
            close(rsd);
            break;
        }
    }
}

void TCP_Processing(int rsd)
{
    char replyBuf[replyBufSize], buf[bufSize];

    // Receive file name and file size
    if (receive_to_buf(rsd, replyBuf, sizeof(replyBuf)) <= 0) {
        close(rsd);
        fprintf(stderr, "Error receiving file name and file size\n");
        return;
    }

    char *size = getFileSizePTR(replyBuf, sizeof(replyBuf));
    if (size == NULL) {
        close(rsd);
        fprintf(stderr, "Bad file size\n");
        return;
    }
    long fileSize = atoi(size);

    char *fileName = replyBuf;

    printf("File size: %ld, file name: %s\n", fileSize, fileName);

    FILE *file = create_file(fileName, "temp_folder");
    if (file == NULL) {
        perror("Create file error");
        exit(EXIT_FAILURE);
    }
    // Receiving file   
    int recvSize;
    long totalBytesReceived = 0;

    fd_set rset, xset;
    FD_ZERO(&rset);
    FD_ZERO(&xset);

    while (totalBytesReceived < fileSize) {

        FD_SET(rsd, &rset);
        select(rsd + 1, &rset, NULL, &xset, NULL);

        if (FD_ISSET(rsd, &xset)) {
            printf
                ("Received OOB byte. Total bytes of \"%s\" received: %ld\n",
                 fileName, totalBytesReceived);

            char oobBuf;
            int n = recv(rsd, &oobBuf, 1, MSG_OOB);
            if (n == -1)
                fprintf(stderr, "receive OOB error\n");
            FD_CLR(rsd, &xset);
        }

        if (FD_ISSET(rsd, &rset)) {
            recvSize = recv(rsd, buf, sizeof(buf), 0);

            if (recvSize > 0) {
                totalBytesReceived += recvSize;
                fwrite(buf, 1, recvSize, file);
            } else if (recvSize == 0) {
                printf("Received EOF\n");
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
    printf("Receiving file \"%s\" completed. %ld bytes received.\n",
           fileName, totalBytesReceived);
    close(rsd);

    return;
}

struct fileInfo {
    FILE *file;
    char fileName[256];
};


int UdpServerDescr = -1;

const unsigned char ACK = 1;
const unsigned char END = 2;

map < uint64_t, fileInfo * >filesMap;


void receiveFileUDP(char *host, unsigned int port)
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

    struct timeval timeOut = {30, 0}, noTimeOut = {0, 0};

    struct sockaddr_in addr;
    socklen_t rlen = sizeof(addr);
    int recvSize;

    while (1) {
        if (filesMap.size() > 1)
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &timeOut, sizeof(struct timeval));      // set timeout
        else
            setsockopt(UdpServerDescr, SOL_SOCKET, SO_RCVTIMEO, &noTimeOut, sizeof(struct timeval));    // disable timeout

        unsigned char buf[bufSize];

        recvSize =
            recvfrom(UdpServerDescr, buf, sizeof(buf), 0,
                     (struct sockaddr *) &addr, &rlen);
        if (recvSize < 0) {
            perror("recvfrom()");
            exit(EXIT_FAILURE);
        }

        UDP_Processing(buf, recvSize, addr);
    }
}


void UDP_Processing(unsigned char *buf, int recvSize, struct sockaddr_in &addr)
{
    socklen_t rlen = sizeof(addr);
    int bytesTransmitted;

    uint64_t address =
        IpPortToNumber(addr.sin_addr.s_addr, addr.sin_port);

    map < uint64_t, fileInfo * >::iterator pos =
        filesMap.find(address);

    // client address not found in array
    if (pos == filesMap.end()) {

        char *fileSizeStr = getFileSizePTR((char *) buf, recvSize);
        if (fileSizeStr == NULL) {
            fprintf(stderr, "Bad file size\n");
            return;
        }
        long fileSize = atoi(fileSizeStr);

        char *fileName = (char *) buf;

        printf("File size: %ld, file name: %s\n", fileSize, fileName);

        FILE *file = create_file(fileName, "temp_folder");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        struct fileInfo *info = new fileInfo;
        *info = {file, {0}};
        strcpy(info->fileName, fileName);

        filesMap[address] = info;


        bytesTransmitted = sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                                  (struct sockaddr *) &(addr), rlen);
        if (bytesTransmitted < 0) {
            perror("send");
            exit(EXIT_FAILURE);
        }

    } else if (buf[0] == END && recvSize == 1) {

        struct fileInfo *info = pos->second;

        printf("File \"%s\" received\n", info->fileName);
        fclose(info->file);
        delete info;
        filesMap.erase(pos);

    } else {

        switch (fork()) {
        case -1:
            perror("fork()");
            exit(EXIT_FAILURE);

        case 0:                // child process
            {
                struct fileInfo *info = pos->second;
                if (fwrite(buf, 1, recvSize, info->file) < (size_t) recvSize) {
                    fprintf(stderr, "write file error\n");
                    exit(EXIT_FAILURE);
                }

                bytesTransmitted =
                    sendto(UdpServerDescr, &ACK, sizeof(ACK), 0,
                           (struct sockaddr *) &(addr), rlen);
                if (bytesTransmitted < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);
            }

        default:
            break;
        }

    }
    return;
}

char *getFileSizePTR(char *str, int size)
{
    for (int i = 0; i < size; i++) {
        if (str[i] == ':') {
            str[i] = 0;
            return &str[i + 1];
        }
    }
    return NULL;
}

void print_error(char* message, int error_code) {
    fprintf(stderr, message);
    exit(error_code);
}

uint64_t IpPortToNumber(uint32_t IPv4, uint16_t port)
{
    return (((uint64_t) IPv4) << 16) | (uint64_t) port;
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