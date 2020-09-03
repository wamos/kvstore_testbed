#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "timestamping.h"
#include "nanosleep.h"
#include "multi_dest_header.h"

#define HARDWARE_TIMESTAMPING_ENABLE 1
#define NUM_REQS 1000*100

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
    char* destIP = "10.0.0.5";    
    char* destIP2 = "10.0.0.8"; 
    char* destIP3 = "10.0.0.9";
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;
    int is_direct_to_server = (argc > 3) ? atoi(argv[3]) : 1;
    char* expname = (argc > 4) ? argv[4] : "timestamp_test";
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
    //Alt.service_id = 1; // pass through request
    Alt.service_id = 11; // selection request
    Alt.request_id = 0;
    Alt.options = 10;
    //Alt.alt_dst_ip = inet_addr(destIP);
    //Alt.alt_dst_ip2 = inet_addr(destIP2);
    //Alt.alt_dst_ip3 = inet_addr(destIP3);
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
    #ifdef HARDWARE_TIMESTAMPING_ENABLE 
    struct iovec tx_iovec;
    tx_iovec.iov_base = (void*) &Alt;
    tx_iovec.iov_len = sizeof(alt_header);

    struct msghdr tx_hdr = {0};
    if(is_direct_to_server == 1){
        tx_hdr.msg_name = (void*) &servAddr;
        tx_hdr.msg_namelen =  (socklen_t) servAddrLen;
    }
    else{
        tx_hdr.msg_name = (void*) &routerAddr;
        tx_hdr.msg_namelen =  (socklen_t) routerAddrLen;
    }
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
    #endif

    //seed the rand() function
    srand(1); 

    int replica_0 = 0;
    int replica_1 = 0;
    int replica_2 = 0;

	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
    uint32_t last_optid=0;
    int outoforder_timestamp = 0;
	for(int iter = 0; iter < 3*NUM_REQS; iter++){ // for 3 replicas
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            #ifdef HARDWARE_TIMESTAMPING_ENABLE
            int replica_num = rand()%3;
            if(replica_num == 0){
                // if(is_direct_to_server == 1){
                //     servAddr.sin_addr.s_addr = inet_addr(destIP);
                // }
                // else{
                servAddr.sin_addr.s_addr = inet_addr(destIP);
                //printf("alt_dst_ip:%s\n", destIP);
                Alt.alt_dst_ip = inet_addr(destIP);
                Alt.alt_dst_ip2 = inet_addr(destIP2);
                Alt.alt_dst_ip3 = inet_addr(destIP3);
                //}
                replica_0++;
            }
            else if(replica_num == 1){
                // if(is_direct_to_server == 1){
                //     servAddr.sin_addr.s_addr = inet_addr(destIP2);
                // }
                // else{
                servAddr.sin_addr.s_addr = inet_addr(destIP2);
                //printf("alt_dst_ip:%s\n", destIP2);
                Alt.alt_dst_ip = inet_addr(destIP2);
                Alt.alt_dst_ip2 = inet_addr(destIP3);
                Alt.alt_dst_ip3 = inet_addr(destIP); 
                //}
                replica_1++;               
            }
            else{
                //if(is_direct_to_server == 1){
                //     servAddr.sin_addr.s_addr = inet_addr(destIP3);
                // }
                // else{
                servAddr.sin_addr.s_addr = inet_addr(destIP3);
                //printf("alt_dst_ip:%s\n", destIP3);
                Alt.alt_dst_ip = inet_addr(destIP3);
                Alt.alt_dst_ip2 = inet_addr(destIP);
                Alt.alt_dst_ip3 = inet_addr(destIP2);
                //}
                replica_2++;
            }
            //tx_iovec.iov_base = (void*) &Alt;
            //tx_hdr.msg_iov = &tx_iovec;

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
        
        ssize_t recv_bytes = 0;

        while(recv_bytes < sizeof(alt_header)){
            #ifdef HARDWARE_TIMESTAMPING_ENABLE
            numBytes = recvmsg(send_sock, &rx_hdr, 0);
            #else
            numBytes = recvfrom(send_sock, (void*) &recv_alt, sizeof(recv_alt), 0, (struct sockaddr *) &servAddr, (socklen_t*) &servAddrLen);            
            #endif            

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
        if(print_tx_counter)
            printf("\n");
        // //hardware_timestamp_array[send_info.optid].send_ts = send_info.time;

        //solve the reordering issue with tx_timestamps here
        if(send_info.optid <= last_optid && send_info.optid!=0){
            printf("out-of-order based on optid\n");
            printf("send_info.optid:%u, last_optid:%u\n",send_info.optid,last_optid);
            outoforder_timestamp++;
        }
        else{
            last_optid = send_info.optid;
        }

        // Handle udp_get_rx_timestamp failures
        // we can make it a while loop to get the proper rx_timestamp
        int print_rx_counter = 0;
        while(udp_get_rx_timestamp(&rx_hdr, &recv_info) < 0){
            if(print_rx_counter){
                printf("+");
            }
            else{
                printf("extracting rx timestamp fails");   
                print_rx_counter++;         
            }
        }
        if(print_rx_counter)
            printf("\n");

        #endif  
        //hardware_timestamp_array[send_info.optid].recv_ts = recv_info.time;
        //system_timestamp_array[send_info.optid].send_ts= ts1;
        //system_timestamp_array[send_info.optid].recv_ts= ts2;


        //print_timespec_ns(&send_info.time, "NIC tx_timestamp");
        //print_timespec_ns(&recv_info.time, "NIC rx_timestamp");
        //print_timespec_ns(&ts1, "Sys tx_timestamp");
        //print_timespec_ns(&ts2, "Sys rx_timestamp");

        //uint64_t recv_stack_overhead = clock_gettime_diff_ns(&ts_info.time, &ts2);
        //printf("recv_stack_overhead:%" PRIu64 "\n", recv_stack_overhead);
        #ifdef HARDWARE_TIMESTAMPING_ENABLE
        uint64_t network_rtt = clock_gettime_diff_ns(&recv_info.time, &send_info.time);
        uint64_t req_rtt = clock_gettime_diff_ns(&ts1, &ts2);
        if(network_rtt > req_rtt){
            fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
            printf("\n--------------\n network_rtt > req rtt \n--------------\n");
            printf("req rtt:%" PRIu64 "\n", req_rtt);
            printf("network_rtt:%" PRIu64 "\n", network_rtt);
        }
        else{
            fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
        } 
        #else
        uint64_t req_rtt = clock_gettime_diff_ns(&ts1, &ts2);
        fprintf(fp, "%" PRIu64 "\n", req_rtt);
        #endif           
	}

    printf("replica 0:%d,", replica_0);
    printf("replica 1:%d,", replica_1);
    printf("replica 2:%d\n", replica_2);

    /*for(int index = 0; index < NUM_REQS; index++){
        uint64_t network_rtt = clock_gettime_diff_ns(&hardware_timestamp_array[index].recv_ts, &hardware_timestamp_array[index].send_ts);
        uint64_t req_rtt     = clock_gettime_diff_ns(&system_timestamp_array[index].recv_ts, &system_timestamp_array[index].send_ts);
        fprintf(fp, "%" PRIu64 ",%" PRIu64 "\n", req_rtt, network_rtt);
    }*/
    #ifdef HARDWARE_TIMESTAMPING_ENABLE
    printf("out-of-order timestamps:%d\n", outoforder_timestamp);
    disable_nic_timestamping(interface_name);
    #endif    

	close(send_sock);
    fclose(fp);

  	exit(0);
}
