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
//#include <linux/tcp.h>
#include "socket_operation.h"

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;
    uint64_t wait_interval = (argc > 3) ? strtoull(argv[3], NULL, 10): 0;
    const char* delay_time = (argc > 4) ? argv[4] : "test";
    const char delay_duration[] = "/home/shw328/redis_exp/redis/latency_log/delay_duration_";
    const char log[] = ".log";
    char logfilename[100];
    snprintf(logfilename, sizeof(delay_duration) + sizeof(log) + 30, "%s%s%s", delay_duration, delay_time, log);
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

    if(SetTCPNoDelay(send_sock, 1) == -1){
        perror("setsockopt TCP_NODELAY error\n");
        exit(1);
    }

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
	for(int iter = 0; iter < ITERS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
		ssize_t numBytes = send(send_sock, sendbuffer, 20, 0);

		if (numBytes < 0){
			printf("send() failed\n");
            exit(1);
        }else{
            printf("send:%zd\n", numBytes);
        }

        numBytes = recv(send_sock, recvbuffer, 20, 0);
        //clock_gettime(CLOCK_REALTIME, &ts2);

        //sleep_ts1 = ts2;
        //realnanosleep(wait_interval, &sleep_ts1, &sleep_ts2);

        if (numBytes < 0){
			printf("recv() failed\n");
            exit(1);
        }
        else if (numBytes == 0){
            printf("recv no bytes\n");
        }
        else{
            if(ts1.tv_sec == ts2.tv_sec){
                //fprintf(fp,"%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec); 
                printf("%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec); 
            }
            else{ 
                uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
                uint64_t ts2_nsec = ts2.tv_nsec + 1000000000*ts2.tv_sec;                    
                //fprintf(fp,"%" PRIu64 "\n", ts2_nsec - ts1_nsec);
                printf("%" PRIu64 "\n", ts2_nsec - ts1_nsec);
            }    

            printf("recv:%zd\n", numBytes);
        }
	}

	close(send_sock);
    //fclose(fp);

  	exit(0);
}