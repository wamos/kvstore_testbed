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
#include "nanosleep.h"
#include "alt_header.h"
#include "map_containers.h"
#include "aws_config.h"

//#define ITERS 500000
#define ITERS 100000

int UDPSocketSetup(int servSock, struct sockaddr_in servAddr){
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

static inline void
print_ipaddr(const char* string, uint32_t ip_addr){
	uint32_t ipaddr = ip_addr;
	uint8_t src_addr[4];
	src_addr[3] = (uint8_t) (ipaddr >> 24) & 0xff;
	src_addr[2] = (uint8_t) (ipaddr >> 16) & 0xff;
	src_addr[1] = (uint8_t) (ipaddr >> 8) & 0xff;
	src_addr[0] = (uint8_t) ipaddr & 0xff;
	printf("%s:%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n", string,
			src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
}

static inline void
dump_ipaddr(FILE* fp, uint32_t ip_addr){
	uint32_t ipaddr = ip_addr;
	uint8_t src_addr[4];
	src_addr[3] = (uint8_t) (ipaddr >> 24) & 0xff;
	src_addr[2] = (uint8_t) (ipaddr >> 16) & 0xff;
	src_addr[1] = (uint8_t) (ipaddr >> 8) & 0xff;
	src_addr[0] = (uint8_t) ipaddr & 0xff;
	fprintf(fp, "%%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n", fp,
			src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
}

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];
    char* routerIP; //= argv[2];   // 1st arg: BESS IP address (dotted quad)
    char* destIP  = (char*) malloc(10); //"10.0.0.1"; //= argv[3];     // 2nd arg: alt dest ip addr;
	char* destIP2 = (char*) malloc(10); //"10.0.0.2"; //= argv[4];
	char* destIP3 = (char*) malloc(10); //"10.0.0.3"; //= argv[5];
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 7000;
    int is_direct_to_server =  0; //(argc > 3) ? atoi(argv[3]) : 1;
    int is_replica_selection =  (argc > 3) ? atoi(argv[3]) : 1;
    int is_random_selection = (argc > 4) ? atoi(argv[4]) : 1;
    char* expname = (argc > 5) ? argv[5] : "dpdk_tor_test";
    const char filename_prefix[] = "/home/ec2-user/efs/multi-tor-evalution/log/";
    const char log[] = ".log";
    const char redirect[] = ".rdct";
    const char all[] = ".all";

    char logfilename[100];
    snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, log);
    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;
    FILE* fp = fopen(logfilename,"w+");
    if(fp == NULL){
       printf("fail to open the file\n");
       exit(1);
    }

    snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, redirect);
    FILE* fp_redir = fopen(logfilename,"w+");
    if(fp_redir == NULL){
       printf("fail to open the file\n");
       exit(1);
    }

    snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, all);
    FILE* fp_all = fopen(logfilename,"w+");
    if(fp_all == NULL){
       printf("fail to open the file\n");
       exit(1);
    }

    char *ip_addr = (char*) malloc(20);
	char *nexthop_addr = (char*) malloc(20);
    int num_entries;

	//#ifndef AWS_HASHTABLE
	//FILE* read_fp = fopen("/home/shw328/multi-tor-evalution/onearm_lb/test-pmd/routing_table_local.txt", "r");
	//#else
    FILE* read_fp = fopen("/home/ec2-user/efs/multi-tor-evalution/config/routing_table_aws.txt", "r");
	//#endif
    if(read_fp == NULL){
        printf("fp is NULL\n");
        exit(1);
    }

	fscanf(read_fp, "%d\n", &num_entries);
	printf("routing table: num_entries %d\n", num_entries);
	for(int i = 0; i < num_entries; i++){
		fscanf(read_fp, "%s %s\n", ip_addr, nexthop_addr);
		// local-ip -> ToR ip
		uint32_t dest_addr = inet_addr(ip_addr);
		uint32_t tor_addr  = inet_addr(nexthop_addr);
        map_insert(dest_addr,tor_addr);
        print_ipaddr("dest_addr", dest_addr);
        print_ipaddr("tor_addr", tor_addr);
        uint32_t ret_addr = map_lookup(dest_addr);
        //print_ipaddr("tor_addr", ret_addr);
	}
    free(ip_addr);
	free(nexthop_addr);

    int service = 0 , num_replica = 0;
    read_fp = fopen("/home/ec2-user/efs/multi-tor-evalution/config/replica_service_ip_aws.txt", "r");
	//#endif
    if(read_fp == NULL){
        printf("fp is NULL\n");
        exit(1);
    }
    fscanf(read_fp, "%d\n", &num_entries);
    printf("replica_service_ip_aws: num_lines %d\n", num_entries);
    fscanf(read_fp, "%d %d %s %s %s\n", &num_replica, &service, destIP, destIP2, destIP3);

    fclose(read_fp);

    int listen_sock;
    if ((listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
	clntAddr.sin_family = AF_INET;                // IPv4 address family
	clntAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface
	clntAddr.sin_port = htons(recvPort);          // Local port
    int clntAddrLen = sizeof(clntAddr);

    UDPSocketSetup(listen_sock, clntAddr);

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
    printf("routerAddr lookup\n");
    routerAddr.sin_addr.s_addr = map_lookup(inet_addr(destIP)); //inet_addr(routerIP); // an incoming interface
    int routerAddrLen = sizeof(routerAddr);

    struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(destIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);

    //our alt header
    struct alt_header Alt;    
    if(is_replica_selection)
        Alt.msgtype_flags =   SINGLE_PKT_REQ;
    else
        Alt.msgtype_flags =   SINGLE_PKT_REQ_PASSTHROUGH;

    Alt.redirection = 0;
    
    Alt.feedback_options = 0;
    Alt.service_id = service;
    Alt.request_id = 0;
    
    Alt.alt_dst_ip  = inet_addr(destIP);
    //Alt.actual_src_ip = inet_addr(recvIP);    
    Alt.replica_dst_list[0] = inet_addr(destIP);
	Alt.replica_dst_list[1] = inet_addr(destIP2);
	Alt.replica_dst_list[2] = inet_addr(destIP3);

    print_ipaddr("replica_dst_list[0]:", Alt.replica_dst_list[0]);
    print_ipaddr("replica_dst_list[1]:", Alt.replica_dst_list[1]);
    print_ipaddr("replica_dst_list[2]:", Alt.replica_dst_list[2]);
    printf("sizeif Alt: %ld\n", sizeof(Alt));
    printf("is_direct_to_server: %d\n", is_direct_to_server);
    struct alt_header recv_alt;
	//clock_gettime(CLOCK_REALTIME, &starttime_spec);
    ssize_t numBytes = 0;
	for(int iter = 0; iter < ITERS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(struct alt_header)){
            if(is_random_selection){
                int replica_num = rand()%NUM_REPLICA;
                if(replica_num == 0){
                    Alt.alt_dst_ip  = inet_addr(destIP);
                    routerAddr.sin_addr.s_addr = map_lookup(inet_addr(destIP));
                }
                else if(replica_num == 1){
                    Alt.alt_dst_ip  = inet_addr(destIP2);
                    routerAddr.sin_addr.s_addr = map_lookup(inet_addr(destIP2));
                }
                else{
                    Alt.alt_dst_ip  = inet_addr(destIP3);
                    routerAddr.sin_addr.s_addr = map_lookup(inet_addr(destIP3));
                }
            }

            if(is_direct_to_server)
                numBytes = sendto(listen_sock, (void*) &Alt, sizeof(struct alt_header), 0, (struct sockaddr *) &servAddr, (socklen_t) servAddrLen); 
            else{
            	numBytes = sendto(listen_sock, (void*) &Alt, sizeof(struct alt_header), 0, (struct sockaddr *) &routerAddr, (socklen_t) routerAddrLen);
            }

            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else{
                send_bytes = send_bytes + numBytes;
                //printf("send:%zd\n", numBytes);
            }
        }
	//routerAddr.sin_port = routerAddr.sin_port + 1;
        Alt.request_id = Alt.request_id + 1;
        
        ssize_t recv_bytes = 0;
        while(recv_bytes < sizeof(struct alt_header)){
            //numBytes = recvfrom(listen_sock, (void*) &recv_alt, sizeof(recv_alt), 0, (struct sockaddr *) &routerAddr, (socklen_t*) &routerAddrLen);
            numBytes = recvfrom(listen_sock, (void*) &recv_alt, sizeof(struct alt_header), 0, (struct sockaddr *) &servAddr, (socklen_t*) &servAddrLen);

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
        if(ts1.tv_sec == ts2.tv_sec){            
            printf("%" PRIu8 ",%" PRIu64 "\n", recv_alt.redirection, ts2.tv_nsec - ts1.tv_nsec);
            if(recv_alt.redirection > 0){
                fprintf(fp_redir, "%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec);
                //dump_ipaddr(fp_redir, recv_alt.actual_src_ip);
            }
            else
                fprintf(fp, "%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec);
            
            fprintf(fp_all, "%" PRIu8 ",%" PRIu64 "\n", recv_alt.redirection, ts2.tv_nsec - ts1.tv_nsec);
            //dump_ipaddr(fp_all, recv_alt.actual_src_ip);
            //printf("%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec);
        }
        else{ 
            uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
            uint64_t ts2_nsec = ts2.tv_nsec + 1000000000*ts2.tv_sec;                                
            printf("%" PRIu8 ",%" PRIu64 "\n", recv_alt.redirection, ts2_nsec - ts1_nsec);
            if(recv_alt.redirection > 0){
                fprintf(fp_redir, "%" PRIu64 "\n", ts2_nsec - ts1_nsec);
                //dump_ipaddr(fp_redir, recv_alt.actual_src_ip);
            }
            else
                fprintf(fp, "%" PRIu64 "\n", ts2_nsec - ts1_nsec);

            fprintf(fp_all, "%" PRIu8 ",%" PRIu64 "\n", recv_alt.redirection, ts2_nsec - ts1_nsec);
            //dump_ipaddr(fp_all, recv_alt.actual_src_ip);
            //fprintf(fp, "%" PRIu64 "\n", ts2.tv_nsec - ts1.tv_nsec);
            //printf("%" PRIu64 "\n", ts2_nsec - ts1_nsec);
        }

        // clock_gettime(CLOCK_REALTIME, &ts1);
	    // sleep_ts1=ts1;
	    // realnanosleep(500*1000, &sleep_ts1, &sleep_ts2); // 500 us between requests
	}

	close(send_sock);
    fclose(fp);
    fclose(fp_redir);
    fclose(fp_all);

  	exit(0);
}
