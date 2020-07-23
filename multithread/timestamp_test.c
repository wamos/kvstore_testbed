#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "timestamping.h"
#include "nanosleep.h"

#define NUM_REQS 1000*100

typedef struct __attribute__((__packed__)) {
  uint16_t service_id;    // Type of Service.
  uint16_t request_id;    // Request identifier.
  uint16_t packet_id;     // Packet identifier.
  uint16_t options;       // Options (could be request length etc.).
  in_addr_t alt_dst_ip1;
  in_addr_t alt_dst_ip2;
  //in_addr_t alt_dst_ip3;
} alt_header;

void print_timespec_ns(struct timespec* ts1){
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
    struct timespec send_ts, recv_ts;
    uint32_t opt_id = 0;
    struct timestamp_info ts_info = {recv_ts, opt_id};    

    if(enable_nic_timestamping(interface_name) < 0){
        printf("NIC timestamping can't be enabled\n");
    }

  	// Create a reliable, stream socket using UDP
	int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

    if(sock_enable_timestamping(send_sock) < 0){
       printf("socket %d timestamping can't be enabled\n", send_sock); 
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
    
    //our alt header
    alt_header Alt;    
    Alt.service_id = 1;
    Alt.request_id = 0;
    Alt.packet_id = 0;
    Alt.options = 10;
    Alt.alt_dst_ip1 = inet_addr(destIP);
    //printf("sizeif Alt: %ld\n", sizeof(Alt));
    alt_header recv_alt;

    /*
    struct iovec {
        void  *iov_base;    // Starting address
        size_t iov_len;     // Number of bytes to transfer
    };

    struct msghdr {
        void         *msg_name;       // Optional address 
        socklen_t     msg_namelen;    // Size of address
        struct iovec *msg_iov;        // Scatter/gather array
        size_t        msg_iovlen;     // # elements in msg_iov 
        void         *msg_control;    //Ancillary data, see below
        size_t        msg_controllen; //Ancillary data buffer len
        int           msg_flags;      //Flags (unused)
    };
    */
    struct iovec tx_iovec;
    tx_iovec.iov_base = (void*) &Alt;
    tx_iovec.iov_len = sizeof(alt_header);

    struct msghdr tx_hdr = {0};
    tx_hdr.msg_name = (void*) &servAddr;
    tx_hdr.msg_namelen =  (socklen_t) servAddrLen;
    tx_hdr.msg_iov = &tx_iovec;
    tx_hdr.msg_iovlen = 1;

    char recv_control[CONTROL_LEN] = {0};
    struct iovec rx_iovec;
    rx_iovec.iov_base = (void*) &recv_alt;
    rx_iovec.iov_len = sizeof(alt_header);
    
    struct msghdr rx_hdr = {0};
    rx_hdr.msg_name = (void*) &servAddr;
    rx_hdr.msg_namelen =  (socklen_t) servAddrLen;
    rx_hdr.msg_iov = &rx_iovec;
    rx_hdr.msg_iovlen = 1;
    rx_hdr.msg_control = recv_control;
	rx_hdr.msg_controllen = CONTROL_LEN;

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
    int outoforder_timestamp = 0;
	for(int iter = 0; iter < NUM_REQS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            //numBytes = sendto(send_sock, (void*) &Alt, sizeof(Alt), 0, (struct sockaddr *) &servAddr, (socklen_t) servAddrLen); 
            numBytes= sendmsg(send_sock, &tx_hdr, 0);

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
        Alt.packet_id = Alt.packet_id + 1;
        
        ssize_t recv_bytes = 0;
        while(recv_bytes < sizeof(alt_header)){
            //numBytes = recvfrom(send_sock, (void*) &recv_alt, sizeof(recv_alt), 0, (struct sockaddr *) &servAddr, (socklen_t*) &servAddrLen);
            numBytes = recvmsg(send_sock, &rx_hdr, 0);

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

        clock_gettime(CLOCK_REALTIME, &sleep_ts1);
        realnanosleep(1*1000, &sleep_ts1, &sleep_ts2); 

        if(udp_get_tx_timestamp(send_sock, &send_ts) < 0){
            printf("extracting tx timestamp fails\n");
            continue;
        }

        if(extract_timestamp(&rx_hdr, &ts_info) < 0){
            printf("extracting rx timestamp fails\n");
        }

        clock_gettime(CLOCK_REALTIME, &ts2);

        //print_timespec_ns(&send_ts);
        //print_timespec_ns(&ts_info.time);
        //print_timespec_ns(&ts1);
        //print_timespec_ns(&ts2);

        //uint64_t recv_stack_overhead = clock_gettime_diff_ns(&ts_info.time, &ts2);
        //printf("recv_stack_overhead:%" PRIu64 "\n", recv_stack_overhead);
        uint64_t network_rtt = clock_gettime_diff_ns(&send_ts, &ts_info.time);
        uint64_t req_rtt = clock_gettime_diff_ns(&ts1,&ts2);
        if(network_rtt > req_rtt){
            //fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
            //printf("\n--------------\n network_rtt > req rtt \n--------------\n");
            //printf("req rtt:%" PRIu64 "\n", req_rtt);
            //printf("network_rtt:%" PRIu64 "\n", network_rtt);
            outoforder_timestamp++;
        }
        else{
            fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
            //printf("req rtt:%" PRIu64 "\n", req_rtt);
            //printf("network_rtt:%" PRIu64 "\n", network_rtt);
        }        
        
        /*if(ts1.tv_sec == ts2.tv_sec){
            fprintf(fp, "%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec); 
        }
        else{ 
            uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
            uint64_t ts2_nsec = ts2.tv_nsec + 1000000000*ts2.tv_sec;                    
            fprintf(fp, "%" PRIu64 "\n", ts2_nsec - ts1_nsec);
        }*/
	}

    printf("out-of-order timestamps:%d\n", outoforder_timestamp);

    disable_nic_timestamping(interface_name);

	close(send_sock);
    fclose(fp);

  	exit(0);
}