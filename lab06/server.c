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

#include <iostream>
#include <map>
#include <cstdint>

#define replyBufSize 256
#define bufSize 4096
#define MAX_LENGTH 10
#define h_addr h_addr_list[0]

using namespace std;

const unsigned char ACK = 1;

struct fileInfo {
    FILE *file;
    char fileName[256];
    long totalBytesReceived;
    long fileSize;
};

uint64_t ip_port_to_number(uint32_t IPv4, uint16_t port);
void get_file_tcp(char *serverName, unsigned int port);
void get_file_udp(char *serverName, unsigned int port);
int tcp_processing(int descr, map < int, fileInfo * >&filesMap);
void tcp_oob_processing(int descr, map < int, fileInfo * >&filesMap);
void udp_processing(int server_socket_descriptor_udp, map <uint64_t, fileInfo*> &filesMap);
void intterrupt(int signo);
FILE *create_file(char *file_name, const char *folder_name);
void print_error(char* message, int error_code);


int server_socket_descriptor = -1;

void intterrupt(int signo)
{
    if (server_socket_descriptor != -1)
        close(server_socket_descriptor);

    _exit(0);
}


int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        cerr << "usage: main [-u] <host> <port> \n";
        return 1;
    }

    struct sigaction signal;
    signal.sa_handler = intterrupt;
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

    fd_set rfds, afds, xset;
    FD_ZERO(&afds);
    FD_ZERO(&xset);
    FD_SET(server_socket_descriptor, &afds);

    int nfds = getdtablesize();
    int rsd;

    socklen_t rlen;
    struct sockaddr_in remote;

    map < int, fileInfo * >filesMap;

    while (1) {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, NULL, &xset, NULL) < 0) {
            perror("Select");
            return;
        }
        if (FD_ISSET(server_socket_descriptor, &rfds)) {
            rlen = sizeof(remote);
            rsd =
                accept(server_socket_descriptor, (struct sockaddr *) &remote, &rlen);
            FD_SET(rsd, &afds);
            FD_SET(rsd, &xset);
        }
        for (rsd = 0; rsd < nfds; ++rsd) {
            // search descriptors with exceptions (e.g. out-of-band data)
            if ((rsd != server_socket_descriptor) && FD_ISSET(rsd, &xset)) {
                tcp_oob_processing(rsd, filesMap);
                FD_CLR(rsd, &xset);
            }
            // search descriptors ready to read
            if ((rsd != server_socket_descriptor) && FD_ISSET(rsd, &rfds)) {
                if (tcp_processing(rsd, filesMap) == 0) {
                    close(rsd);
                    FD_CLR(rsd, &afds);
                } else
                    FD_SET(rsd, &xset);
            }
        }
    }
}

int tcp_processing(int descr, map < int, fileInfo * >&filesMap)
{
    char buf[bufSize], requestBuf[replyBufSize];
    int recvSize;

    map < int, fileInfo * >::iterator pos = filesMap.find(descr);

    // descr not found in array
    if (pos == filesMap.end()) {
        recvSize =
            recv(descr, requestBuf, sizeof(requestBuf), MSG_WAITALL);
        if (recvSize == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        char *fileName = strtok(requestBuf, ":");
        if (fileName == NULL) {
            cerr << "Bad file name\n";
            exit(EXIT_FAILURE);
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            cerr << "Bad file size\n";
            exit(EXIT_FAILURE);
        }
        long fileSize = atoi(size);
        cout << "File size: " << fileSize << ", file name: "
            << fileName << "\n";

        FILE *file = create_file(fileName, "temp_folder");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }
        struct fileInfo *info = new fileInfo;
        *info = {file, {0}, 0, fileSize};
        strcpy(info->fileName, fileName);

        filesMap[descr] = info;

    } else {
        struct fileInfo *info = pos->second;

        recvSize = recv(descr, buf, sizeof(buf), MSG_DONTWAIT);
        if (recvSize < 0) {
            perror("recv");
            exit(EXIT_FAILURE);
        } else if (recvSize == 0) {
            cout << "File \"" << info->fileName << "\" received\n";
            fclose(info->file);
            delete info;
            filesMap.erase(pos);
            return 0;
        } else {
            info->totalBytesReceived += recvSize;
            fwrite(buf, 1, recvSize, info->file);
        }
    }
    return recvSize;
}


void tcp_oob_processing(int descr, map < int, fileInfo * >&filesMap)
{
    map < int, fileInfo * >::iterator pos = filesMap.find(descr);
    if (pos == filesMap.end())  // descr not found in array
        return;

    else {
        char oobBuf;
        struct fileInfo *info = pos->second;

        int recvSize = recv(descr, &oobBuf, sizeof(oobBuf), MSG_OOB);
        if (recvSize == -1)
            cerr << "recv OOB error\n";
        else
            cout << "OOB byte received. Total received bytes of \""
                << info->fileName << "\": "
                << info->totalBytesReceived << "\n";
    }
    return;
}

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

    struct timeval timeOut = { 30, 0 };
    struct timeval *timePTR;

    fd_set rfds;
    int nfds = getdtablesize();
    FD_ZERO(&rfds);
    map < uint64_t, fileInfo * >filesMap;

    while (1) {

        if (filesMap.size() == 0)
            timePTR = NULL;     // waiting for new clients infinitely
        else {
            timeOut = {30, 0};
            timePTR = &timeOut;
        }

        FD_SET(server_socket_descriptor_udp, &rfds);
        if (select(nfds, &rfds, (fd_set *) 0, (fd_set *) 0, timePTR) < 0) {
            perror("Select");
            return;
        }
        if (timePTR != NULL && timePTR->tv_sec == 0) {
            cerr << "Timeout error\n";
            return;
        }
        if (FD_ISSET(server_socket_descriptor_udp, &rfds)) {

            udp_processing(server_socket_descriptor_udp, filesMap);
        }
    }
}


void udp_processing(int server_socket_descriptor_udp, map <uint64_t, fileInfo*> &filesMap)
{
    char buf[bufSize];
    int recvSize;

    struct sockaddr_in addr;
    socklen_t rlen = sizeof(addr);

    recvSize =
        recvfrom(server_socket_descriptor_udp, buf, sizeof(buf), MSG_DONTWAIT,
                 (struct sockaddr *) &addr, &rlen);
    if (recvSize == -1) {
        perror("recv");
        exit(EXIT_FAILURE);
    }

    int bytesTransmitted = sendto(server_socket_descriptor_udp, &ACK, sizeof(ACK), 0,
                                  (struct sockaddr *) &addr, rlen);
    if (bytesTransmitted < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }

    uint64_t address = ip_port_to_number(addr.sin_addr.s_addr, addr.sin_port);

    map < uint64_t, fileInfo * >::iterator pos = filesMap.find(address);

    // client address not found in array
    if (pos == filesMap.end()) {
        char *fileName = strtok(buf, ":");
        if (fileName == NULL) {
            cerr << "Bad file name\n";
            exit(EXIT_FAILURE);
        }
        char *size = strtok(NULL, ":");
        if (size == NULL) {
            cerr << "Bad file size\n";
            exit(EXIT_FAILURE);
        }
        long fileSize = atoi(size);
        cout << "File size: " << fileSize << ", file name: "
            << fileName << "\n";

        FILE *file = create_file(fileName, "temp_folder");
        if (file == NULL) {
            perror("Create file error");
            exit(EXIT_FAILURE);
        }

        struct fileInfo *info = new fileInfo;
        *info = {file, {0}, 0, fileSize};
        strcpy(info->fileName, fileName);

        filesMap[address] = info;

    } else {
        struct fileInfo *info = pos->second;
        info->totalBytesReceived += recvSize;

        fwrite(buf, 1, recvSize, info->file);

        if (info->totalBytesReceived == info->fileSize) {
            cout << "File \"" << info->fileName << "\" received\n";
            fclose(info->file);
            delete info;
            filesMap.erase(pos);
        }
    }
    return;
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
uint64_t ip_port_to_number(uint32_t IPv4, uint16_t port)
{
    return (((uint64_t) IPv4) << 16) | (uint64_t) port;
}

