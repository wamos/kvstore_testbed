#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <linux/tcp.h>
#include "nanosleep.h"

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;
    //uint64_t wait_interval = (argc > 3) ? strtoull(argv[3], NULL, 10): 0;
    //const char* delay_time = (argc > 4) ? argv[4] : "test";
    //const char delay_duration[] = "/home/shw328/kvstore_client/log";
    //const char log[] = ".log";
    //char logfilename[100];
    //snprintf(logfilename, sizeof(delay_duration) + sizeof(log) + 30, "%s%s%s", delay_duration, delay_time, log);
    //FILE* fp = fopen(logfilename,"w+");

	char sendbuffer[20];
	memset(&sendbuffer, 1, sizeof(sendbuffer));
    char recvbuffer[20];
    memset(&recvbuffer, 1, sizeof(recvbuffer));

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

  	// Create a reliable, stream socket using TCP
	int send_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

  	// Construct the server address structure
	struct sockaddr_in servAddr;            // Server address
	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
  	servAddr.sin_family = AF_INET;          // IPv4 address family
    servAddr.sin_port = htons(recvPort);    // Server port
    servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface

	if (connect(send_sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		printf("connect() failed\n");
        exit(1);
    }

    //sleep(5);

    for(int i = 0; i < 5; i++){
        ssize_t numBytes = send(send_sock, sendbuffer, 20, 0);
        if (numBytes < 0){
            printf("send() failed\n");
            exit(1);
        }else{
            printf("send:%zd\n", numBytes);
        }

        numBytes = recv(send_sock, recvbuffer, 20, 0);
        if (numBytes < 0){
            printf("recv() failed\n");
            exit(1);
        }else{
            printf("recv:%zd\n", numBytes);
        }
    }


	close(send_sock);
    //fclose(fp);

  	exit(0);
}