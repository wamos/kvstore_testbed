#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "timestamping.h"
#include "nanosleep.h"
#include "multi_dest_header.h"

//#define HARDWARE_TIMESTAMPING_ENABLE 1
#define NUM_REQS 1000*100

typedef struct txrx_timespec{
    struct timespec send_ts;
    struct timespec recv_ts;
} txrx_timespec;

void print_timespec_ns(struct timespec* ts1, const char* str){
    printf("%s:", str);
	printf("tv_nsec %ld, tv_sec %ld\n", ts1->tv_nsec, ts1->tv_sec);
}

/*int main(int argc, char *argv[]){
    char* interface_name = argv[1];
    if(enable_nic_timestamping(interface_name) < 0){
        printf("NIC timestamping can't be enabled\n");
    }

    int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }
    return 0;
}*/

int main(int argc, char *argv[]) {

    char* interface_name = "enp59s0";
	char* routerIP = argv[1];     // 1st arg: BESS IP address (dotted quad)
    char* destIP = argv[2];     // 2nd arg: alt dest ip addr;
    in_port_t recvPort = (argc > 2) ? atoi(argv[3]) : 6379;
    //int is_direct_to_server = (argc > 3) ? atoi(argv[4]) : 1;
    char* expname = (argc > 3) ? argv[4] : "timestamp_test";
    //const char filename_prefix[] = "/home/shw328/kvstore/log/";
    const char filename_prefix[] = "/tmp/";
    const char log[] = ".log";
    char logfilename[100];
    snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, log);
    printf("filename:%s\n", logfilename);
    FILE* fp = fopen(logfilename,"w+");

    if(fp == NULL){
        printf("FILE* isn't good\n");
        fclose(fp);
        exit(0);
    }

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;
    // Create a socket using UDP
	int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

  	// Construct the server address structure
	// struct sockaddr_in routerAddr;            // Server address
	// memset(&routerAddr, 0, sizeof(routerAddr)); // Zero out structure
  	// routerAddr.sin_family = AF_INET;          // IPv4 address family
    // routerAddr.sin_port = htons(recvPort);    // Server port
    // routerAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
    // int routerAddrLen = sizeof(routerAddr);

    struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(destIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);

    // connect UDP socket so we can use sendmsg() and recvmsg()
    //if(connect(send_sock, (struct sockaddr *)&servAddr, sizeof(sockaddr_in)) < 0){
    //    printf("UDP socket connect() fails\n");
    //}
    
    //txrx_timespec hardware_timestamp_array[NUM_REQS]={0};
    //txrx_timespec system_timestamp_array[NUM_REQS]={0};
    //our alt header
    alt_header Alt;    
    Alt.service_id = 1;
    Alt.request_id = 0;
    Alt.options = 10;
    Alt.alt_dst_ip1 = inet_addr(destIP);
    //printf("sizeif Alt: %ld\n", sizeof(Alt));
    alt_header recv_alt;

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
    uint32_t last_optid=0;
    int outoforder_timestamp = 0;
	for(int iter = 0; iter < NUM_REQS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            numBytes = sendto(send_sock, (void*) &Alt, sizeof(Alt), 0, (struct sockaddr *) &servAddr, (socklen_t) servAddrLen);            

            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else{
                send_bytes = send_bytes + numBytes;
                //printf("send:%zd\n", numBytes);
            }
        }
        Alt.request_id = Alt.request_id + 1;
        
        ssize_t recv_bytes = 0;
        while(recv_bytes < sizeof(alt_header)){
            numBytes = recvfrom(send_sock, (void*) &recv_alt, sizeof(recv_alt), 0, (struct sockaddr *) &servAddr, (socklen_t*) &servAddrLen);                  
            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    printf("recv EAGAIN\n");
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
                recv_bytes = recv_bytes +  numBytes;
                //printf("recv:%zd\n", numBytes);
            } 
        }

        clock_gettime(CLOCK_REALTIME, &ts2);
        uint64_t req_rtt = clock_gettime_diff_ns(&ts1, &ts2);
        fprintf(fp, "%" PRIu64 "\n", req_rtt);           
	}
   

	close(send_sock);
    fclose(fp);

  	exit(0);
}
