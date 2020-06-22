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
//#include <netinet/tcp.h>
#include "socket_operation.h"

static const int MAXPENDING = 5; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;

int recv_socket_setup(int servSock, struct sockaddr_in servAddr, struct sockaddr_in clntAddr){
    int clntSock;
    // Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
		perror("bind() failed\n");
        exit(1);
    }
    
	// Mark the socket so it will listen for incoming connections
	if (listen(servSock, MAXPENDING) < 0){
		perror("listen() failed\n");
        exit(1); 
    }

	int flag = 1;
	if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag,
	  sizeof(int)) == -1) { 
        perror("setsockopt SO_REUSEADDR error\n"); 
        exit(1); 
    }

    int val = 1;
    if (setsockopt(servSock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1){
        perror("setsockopt TCP_NODELAY error\n");
    }

    socklen_t clntAddrLen = sizeof(clntAddr);	
    // Wait for a client to connect
    clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
    if (clntSock < 0){
        perror("accept() failed\n");
        exit(1);
    }

    // clntSock is connected to a client!
    char clntName[INET_ADDRSTRLEN]; // String to contain client address
    if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntName, sizeof(clntName)) != NULL)
        printf("Handling client %s/ %d\n", clntName, ntohs(clntAddr.sin_port));
    else
        printf("Unable to get client address\n");

    return clntSock;
}

void send_socket_setup(int send_sock, struct sockaddr_in servAddr){    
    if (connect(send_sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		printf("connect() failed\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;

    char recv_buffer[20];
    char send_buffer[20];

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

	int recv_sock, listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

	// Construct local address structure
	struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    
    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));

    recv_sock = recv_socket_setup(listen_sock, servAddr, clntAddr);

    if(SetTCPNoDelay(recv_sock, 1) == -1){
        perror("setsockopt TCP_NODELAY error\n");
        exit(1);
    }
    
    memset(&recv_buffer, 0, sizeof(recv_buffer));
    memset(&send_buffer, 0, sizeof(send_buffer));
    ssize_t numBytesRcvd;
    
	while(numBytesRcvd < 20*ITERS) {
        ssize_t numBytes = recv(recv_sock, recv_buffer, 20, 0);
        numBytesRcvd = numBytesRcvd + numBytes;

        if (numBytes < 0){
			printf("recv() failed\n");
            exit(1);
        }
        else if (numBytes == 0){
            printf("recv no bytes\n");
        }
        else{
            printf("recv:%zd\n", numBytes);
        }

        clock_gettime(CLOCK_REALTIME, &ts1);
        sleep_ts1=ts1;
        realnanosleep(25*1000, &sleep_ts1, &sleep_ts2); // processing time 25 us

        numBytes = send(recv_sock, send_buffer, 20, 0);
        if (numBytes < 0){
			printf("send() failed\n");
            exit(1);
        }else{
            printf("send:%zd\n", numBytes);
        }

	}

	printf("closing sockets then\n");
    close(listen_sock);
    close(recv_sock);
    //close(send_sock);

}