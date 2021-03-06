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

	char* routerIP = "10.0.0.18"; //argv[1];     // 1st arg: BESS IP address (dotted quad)
    char* destIP1 = "10.0.0.9"; //argv[2];     // 2nd arg: alt dest ip addr;
    char* destIP2 = "10.0.0.8"; //argv[3];     // 3rd arg: alt dest ip addr;

    in_port_t recvPort = 6379; //(argc > 3) ? atoi(argv[4]) : 6379;
    int is_direct_to_server = 0; //(argc > 4) ? atoi(argv[5]) : 1;
    //char* expname = (argc > 4) ? argv[5] : "bess_test";
    //const char filename_prefix[] = "/home/shw328/kvstore/log/bess_test/";
    //const char log[] = ".log";
    //char logfilename[100];
    //snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, log);
    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;
    //FILE* fp = fopen(logfilename,"w+");

  	// Create a reliable, stream socket using UDP
	int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

  	// Construct the server address structure
	struct sockaddr_in routerAddr;            // Server address
	memset(&routerAddr, 0, sizeof(routerAddr)); // Zero out structure
  	routerAddr.sin_family = AF_INET;          // IPv4 address family
    routerAddr.sin_port = htons(recvPort);    // Server port
    routerAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
    int routerAddrLen = sizeof(routerAddr);

    struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(destIP1); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);

    //our alt header
    alt_header Alt;    
    Alt.service_id = 1;
    Alt.request_id = 0;
    Alt.packet_id = 0;
    Alt.options = 10;
    Alt.alt_dst_ip1 = inet_addr(destIP1);
    Alt.alt_dst_ip2 = inet_addr(destIP2);
    //printf("sizeif Alt: %ld\n", sizeof(Alt));
    //alt_header recv_alt;

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
	for(int iter = 0; iter < 10; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);        
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(Alt)){
            //if(is_direct_to_server)
                //numBytes = sendto(send_sock, (void*) &Alt, sizeof(Alt), 0, (struct sockaddr *) &servAddr, (socklen_t) servAddrLen); 
            //else
            numBytes = sendto(send_sock, (void*) &Alt, sizeof(Alt), 0, (struct sockaddr *) &routerAddr, (socklen_t) routerAddrLen);

            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else{
                send_bytes = send_bytes + numBytes;
                //printf("send:%zd\n", numBytes);
            }
        }

        //sleep state->feedback_period_us 
        //clock_gettime(CLOCK_REALTIME, &ts1);
        //clock_gettime(CLOCK_REALTIME, &ts1);
        //realnanosleep(1000*1000, &ts1, &ts2);  //sleep x usec
        //Alt.request_id = Alt.request_id + 1;
        //Alt.packet_id = Alt.packet_id + 1;
        
        // ssize_t recv_bytes = 0;
        // while(recv_bytes < sizeof(alt_header)){
        //     numBytes = recvfrom(send_sock, (void*) &recv_alt, sizeof(recv_alt), 0, (struct sockaddr *) &servAddr, (socklen_t*) &servAddrLen);

        //     if (numBytes < 0){
        //         if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
        //             printf("recv EAGAIN\n");
        //             continue;
        //         }
        //         else{
        //             printf("recv() failed\n");
        //             exit(1);
        //         }
        //     }
        //     else if (numBytes == 0){
        //         printf("recv no bytes\n");
        //     }
        //     else{
        //         recv_bytes = recv_bytes +  numBytes;
        //         //printf("recv:%zd\n", numBytes);
        //     } 
        // }
        // clock_gettime(CLOCK_REALTIME, &ts2);
        // if(ts1.tv_sec == ts2.tv_sec){
        //     fprintf(fp, "%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec); 
        // }
        // else{ 
        //     uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
        //     uint64_t ts2_nsec = ts2.tv_nsec + 1000000000*ts2.tv_sec;                    
        //     fprintf(fp, "%" PRIu64 "\n", ts2_nsec - ts1_nsec);
        // } 
	}

	close(send_sock);
    //fclose(fp);

  	exit(0);
}