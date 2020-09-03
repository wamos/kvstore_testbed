#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "timestamping.h"
#include "nanosleep.h"
#include "multi_dest_header.h"

#define HARDWARE_TIMESTAMPING_ENABLE 1
#define NUM_REQS 10 //1000*100

// typedef struct __attribute__((__packed__)) {
//   uint16_t service_id;    // Type of Service.
//   uint16_t request_id;    // Request identifier.
//   uint16_t packet_id;     // Packet identifier.
//   uint16_t options;       // Options (could be request length etc.).
//   in_addr_t alt_dst_ip1;
//   in_addr_t alt_dst_ip2;
//   in_addr_t alt_dst_ip3;
// } alt_header;

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
    char* destIP = "10.0.0.9";    
    char* destIP2 = "10.0.0.8"; 
    char* destIP3 = "10.0.0.5";
    char* clientIP1 = "10.0.0.2";    
    char* clientIP2 = "10.0.0.3"; 
    char* clientIP3 = "10.0.0.4";

    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;
    //int is_direct_to_server = (argc > 3) ? atoi(argv[4]) : 1;
    char* expname = (argc > 3) ? argv[3] : "timestamp_test";
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

    #ifdef HARDWARE_TIMESTAMPING_ENABLE 
    struct timespec send_ts, recv_ts;
    uint32_t opt_id = 0;
    struct timestamp_info send_info = {send_ts, opt_id};
    struct timestamp_info recv_info = {recv_ts, opt_id};

    if(enable_nic_timestamping(interface_name) < 0){
        printf("NIC timestamping can't be enabled\n");
    }

    if(sock_enable_timestamping(send_sock) < 0){
       printf("socket %d timestamping can't be enabled\n", send_sock); 
    }
    #endif

    struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);
    
    //our alt header
    alt_header Alt;    
    Alt.service_id = 13;
    Alt.request_id = 0;
    Alt.options = 3;
    Alt.alt_dst_ip = inet_addr(clientIP1);
    Alt.alt_dst_ip2 = inet_addr(clientIP2);
    Alt.alt_dst_ip3 = inet_addr(clientIP3);
    printf("size:%zu\n", sizeof(alt_header));

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
    #ifdef HARDWARE_TIMESTAMPING_ENABLE 
    struct iovec tx_iovec;
    tx_iovec.iov_base = (void*) &Alt;
    tx_iovec.iov_len = sizeof(alt_header);

    struct msghdr tx_hdr = {0};
    tx_hdr.msg_name = (void*) &servAddr;
    tx_hdr.msg_namelen =  (socklen_t) servAddrLen;
    tx_hdr.msg_iov = &tx_iovec;
    tx_hdr.msg_iovlen = 1;
    #endif

    //seed the rand() function
    srand(1); 

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
    uint32_t last_optid=0;
    int outoforder_timestamp = 0;
	for(int iter = 0; iter < NUM_REQS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            #ifdef HARDWARE_TIMESTAMPING_ENABLE
            numBytes= sendmsg(send_sock, &tx_hdr, 0);
            #else
            numBytes = sendto(send_sock, (void*) &Alt, sizeof(Alt), 0, (struct sockaddr *) &servAddr, (socklen_t) servAddrLen); 
            #endif            

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
        clock_gettime(CLOCK_REALTIME, &ts2);

        //clock_gettime(CLOCK_REALTIME, &sleep_ts1);
        //realnanosleep(5*1000, &sleep_ts1, &sleep_ts2);


        //TODO: fix reordering in tx_timestamps
        #ifdef HARDWARE_TIMESTAMPING_ENABLE
        // Handle udp_get_tx_timestamp failures
        // we can make it a while loop to get the proper tx_timestamp
        int print_tx_counter = 0;
        while(udp_get_tx_timestamp(send_sock, &send_info) < 0){
            if(print_tx_counter){
                printf("+");
            }
            else{
                printf("extracting tx timestamp fails at iter %d", iter);
                print_tx_counter++;
            }
        }
        if(print_tx_counter){
            printf("\n");
        }
        //hardware_timestamp_array[send_info.optid].send_ts = send_info.time;

        //TODO: solbve the reordering issue with tx_timestamps?
        if(send_info.optid <= last_optid && send_info.optid!=0){
            printf("out-of-order based on optid\n");
            printf("send_info.optid:%u, last_optid:%u\n",send_info.optid,last_optid);
            outoforder_timestamp++;
        }
        else{
            last_optid = send_info.optid;
        }

        #endif  

        print_timespec_ns(&send_info.time, "NIC tx_timestamp");
        //print_timespec_ns(&recv_info.time, "NIC rx_timestamp");
        print_timespec_ns(&ts1, "Sys tx_timestamp");        
        //print_timespec_ns(&ts2, "Sys rx_timestamp");

        #ifdef HARDWARE_TIMESTAMPING_ENABLE
        uint64_t clock_drift = clock_gettime_diff_ns(&ts1, &send_info.time);
        fprintf(fp, "%" PRIu64 "\n", clock_drift);

        // uint64_t network_rtt = clock_gettime_diff_ns(&recv_info.time, &send_info.time);
        // uint64_t req_rtt = clock_gettime_diff_ns(&ts1, &ts2);
        // if(network_rtt > req_rtt){
        //     fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
        //     printf("\n--------------\n network_rtt > req rtt \n--------------\n");
        //     printf("req rtt:%" PRIu64 "\n", req_rtt);
        //     printf("network_rtt:%" PRIu64 "\n", network_rtt);
        // }
        // else{
        //     fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
        // } 
        #else
        uint64_t req_rtt = clock_gettime_diff_ns(&ts1, &ts2);
        fprintf(fp, "%" PRIu64 "\n", req_rtt);
        #endif           
	}

    #ifdef HARDWARE_TIMESTAMPING_ENABLE
    printf("out-of-order timestamps:%d\n", outoforder_timestamp);
    disable_nic_timestamping(interface_name);
    #endif    

	close(send_sock);
    fclose(fp);

  	exit(0);
}
