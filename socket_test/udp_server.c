#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include "practical.h"
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include "socket_operation.h"
#include <errno.h>

static const int MAXPENDING = 5; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;

int server_socket_setup(int servSock, struct sockaddr_in servAddr){
    int clntSock;
    // Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
		perror("bind() failed\n");
        exit(1);
    }
    
	int flag = 1;
	if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag,
	  sizeof(int)) == -1) { 
        perror("setsockopt SO_REUSEADDR error\n"); 
        exit(1); 
    }

    return 0;
}

void send_socket_setup(int send_sock, struct sockaddr_in servAddr){    
    if (connect(send_sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		printf("connect() failed\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    char* routerIP = argv[2];     // 2nd arg: alt dest ip addr;
    in_port_t recvPort = (argc > 2) ? atoi(argv[3]) : 6379;

    char recv_buffer[20];
    char send_buffer[20];

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

	int send_sock, listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

    if ((send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

	// Construct local address structure
	struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);

    struct sockaddr_in routerAddr;                  // Local address
	memset(&routerAddr, 0, sizeof(routerAddr));       // Zero out structure
	routerAddr.sin_family = AF_INET;                // IPv4 address family
	routerAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
	routerAddr.sin_port = htons(recvPort+1);          // Local port
    int routerAddrLen = sizeof(routerAddr);
    
    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
    int clntAddrLen = sizeof(clntAddr);

    server_socket_setup(listen_sock, servAddr);
    //server_socket_setup(send_sock, routerAddr);

    //memset(&recv_buffer, 0, sizeof(recv_buffer));
    //memset(&send_buffer, 0, sizeof(send_buffer));

    alt_header Alt;
    memset(&Alt, 0, sizeof(alt_header));
    alt_header alt_response;
    memset(&alt_response, 0, sizeof(alt_header));
    ssize_t numBytesRcvd = 0;
    
	while(numBytesRcvd < 12*ITERS) {
        //printf("enter while loop\n");

        ssize_t recv_bytes = 0;
        while(recv_bytes < 12){
            //printf("enter recv loop\n");
            ssize_t numBytes = recvfrom(listen_sock, (void*)&Alt, sizeof(Alt), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    printf("recv() EAGAIN\n");
                    continue;
                }
                else{
                    printf("recv() failed\n");
                    exit(1);
                }                
            }
            else if (numBytes == 0){
                printf("recv no bytes\n");
            }
            else{                
                // char clntName[INET_ADDRSTRLEN]; // String to contain client address
                // if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntName, sizeof(clntName)) != NULL)
                //     printf("Handling client %s/ %d\n", clntName, ntohs(clntAddr.sin_port));
                // else
                //     printf("Unable to get client address\n");
                
                // printf("service_id:%" PRIu16 ",", Alt.service_id);
                // printf("request_id:%" PRIu32 ",", Alt.request_id);
                // printf("packet_id:%" PRIu32 ",", Alt.packet_id);
                // printf("options:%" PRIu32 ",", Alt.options);
                // struct in_addr dest_addr;
                // dest_addr.s_addr = Alt.alt_dst_ip;
                // char* dest_ip =inet_ntoa(dest_addr);
                // printf("alt_dst_ip:%s\n", dest_ip);

                recv_bytes = recv_bytes +  numBytes;
                numBytesRcvd = numBytesRcvd + numBytes;
                //printf("recv:%zd\n", numBytes);
            }
        }



        ssize_t send_bytes = 0;
        while(send_bytes < 12){
            //ssize_t numBytes = sendto(send_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            alt_response.service_id = 1;
            alt_response.request_id = 0;
            alt_response.packet_id = 0;
            alt_response.options = 1;
            alt_response.alt_dst_ip = clntAddr.sin_addr.s_addr;

            //printf("service_id:%" PRIu16 ",", alt_response.service_id);
            //printf("request_id:%" PRIu32 ",", alt_response.request_id);
            //printf("packet_id:%" PRIu32 ",", alt_response.packet_id);
            //printf("options:%" PRIu32 ",", alt_response.options);
            struct in_addr dest_addr;
            dest_addr.s_addr = alt_response.alt_dst_ip;
            char* dest_ip =inet_ntoa(dest_addr);
            //printf("alt_dst_ip:%s\n", dest_ip);

            //ssize_t numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &routerAddr, sizeof(routerAddr));
            ssize_t numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else if (numBytes == 0){
                printf("send no bytes\n");
            }
            else{
                send_bytes = send_bytes + numBytes;
                //printf("send:%zd\n", numBytes);
            }
        }

	}

	printf("closing sockets then\n");
    close(listen_sock);
    close(send_sock);
    //close(send_sock);

}