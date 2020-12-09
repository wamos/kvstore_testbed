#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <pthread.h>
#include "nanosleep.h"
#include "epoll_state.h"
#include "dist_gen.h"
#include "alt_header.h"
#include "map_containers.h"
#include "aws_config.h"
//#define FEEDBACK_TO_BESS_ENABLE 1
//#define GC_DELAY_ENABLE 1
//#define SERVER_ECN_ENABLE 1
//#define AWS_HASHTABLE 1

static const int MAXPENDING = 20; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;
int closed_loop_done;
int queued_events;
uint64_t pkt_counter;

int UDPSocketSetup(int servSock, struct sockaddr_in servAddr){
    int clntSock;
    // Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
		perror("bind() failed\n");
        return -1;
    }
    
	int flag = 1;
	if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag,
	  sizeof(int)) == -1) { 
        perror("setsockopt SO_REUSEADDR error\n"); 
        return -1;
    }

    int flags = fcntl(servSock, F_GETFL, 0);  //clear the flag
    
    flags |= O_NONBLOCK; // set it to O_NONBLOCK
    if(fcntl(servSock, F_SETFL, flags) == -1){
        perror("fcntl O_NONBLOCK error\n"); 
        return -1;  
    }

    return 0;
}

int SetSocketReused(int servSock, int flag){
    if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(int)) == -1) { 
        //perror("setsockopt SO_REUSEADDR error\n"); 
        return -1; 
    }
    return 0;
}

int SetSocketNonblocking(int servSock){
    int flags = fcntl(servSock, F_GETFL, 0);  //clear the flag
    
    flags |= O_NONBLOCK; // set it to O_NONBLOCK
    if(fcntl(servSock, F_SETFL, flags) == -1){
        //perror("fcntl O_NONBLOCK error\n"); 
        return -1; 
    }
    return 0;
}

int isFoundInArray(int* fd_array, int array_length, int fd){
    for(int index = 0; index < array_length; index++){
        if(fd_array[index] == fd)
            return index;
        else
            continue;
    }

    return -1;
}

int TCPSocketAceept(int servSock){
    int clntSock;
    struct sockaddr_in clntAddr;
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

int main(int argc, char *argv[]) {

	char* recv_ip_addr = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recv_port_start = (in_port_t) (argc > 2) ? strtoul(argv[2], NULL, 10) : 7000;
    // assume 4 pseudo-connections per open-loop thread and only 1 closed-loop pseudo-connection
    uint32_t expected_connections = (argc > 3) ? atoi(argv[3])*4+1: 1; // pseudo-connection for UDP
    char* identify_string = (argc > 4) ? argv[4]: "test";
    int is_direct_to_client= (argc > 5)? atoi(argv[5]): 1;
    uint32_t feedback_period = (uint32_t) (argc > 6)? atoi(argv[6]): 100;
    double rate = (argc > 7)? atof(argv[7]): 2000.0;

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2, start_ts, end_ts;
    char* routerIP = "10.0.0.18"; //argv[1];
    closed_loop_done = 0;
    queued_events = 0;

    ssize_t numBytesRcvd;
    ssize_t numBytesSend;
    int conn_count = 1;
    int setsize = 10240;
    
    epollState epstate;
    epstate.epoll_fd = -1;
    epstate.events = NULL;
    uint32_t event_flag = EPOLLIN | EPOLLET;

    if(CreateEpoll(&epstate, setsize) == -1){
        printf("epoll_fd:%d\n", epstate.epoll_fd);
        perror("epoll create fails\n");
        exit(1);
    }

    int* udp_socket_array;
    udp_socket_array = (int *) malloc(expected_connections * sizeof(int) );

    // fdreq_tracking_array tracks the number of reqs in a socket every time a epoll_wait call returns    
    int* fdreq_tracking_array;
    int fd_index_diff; // the fd of these sockets starts at 4, usually so the diff is 4
    fdreq_tracking_array = (int *) malloc(expected_connections * sizeof(int) );

    struct sockaddr_in* server_addr_array;
    server_addr_array = (struct sockaddr_in *) malloc( expected_connections * sizeof(struct sockaddr_in) );

    for(int server_index = 0; server_index < expected_connections; server_index++){
        printf("socket creation,");
        if ((udp_socket_array[server_index] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		    perror("socket() failed\n");
            exit(1);
        }

        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET; 
        servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
        servAddr.sin_port = htons(recv_port_start); 
        server_addr_array[server_index] = servAddr;
        printf("socket:%d,port:%u\n", udp_socket_array[server_index], recv_port_start);
        recv_port_start++;        

        if(UDPSocketSetup(udp_socket_array[server_index], servAddr) == -1){
            perror("UDPSocketSetup error\n"); 
            exit(1);
        }
    }
    fd_index_diff = udp_socket_array[0]; 
    //printf("closed-loop %d\n", udp_socket_array[expected_connections-1]);

    char *ip_addr = (char*) malloc(20);
	char *nexthop_addr = (char*) malloc(20);
    int num_entries;

    #ifndef AWS_HASHTABLE
	FILE* fp = fopen("/home/shw328/multi-tor-evalution/onearm_lb/test-pmd/routing_table_local.txt", "r");
	#else
	FILE* fp = fopen("/home/ec2-user/multi-tor-evalution/onearm_lb/test-pmd/routing_table_aws.txt", "r");
	#endif
    if(fp == NULL){
        printf("fp is NULL\n");
        exit(1);
    }

    fscanf(fp, "%d\n", &num_entries);
	printf("routing table: num_entries %d\n", num_entries);
	for(int i = 0; i < num_entries; i++){
		fscanf(fp, "%s %s\n", ip_addr, nexthop_addr);
		// local-ip -> ToR ip
		uint32_t dest_addr = inet_addr(ip_addr);
		uint32_t tor_addr  = inet_addr(nexthop_addr);
		map_insert(dest_addr,tor_addr);
		print_ipaddr("dest_addr", dest_addr);
		print_ipaddr("tor_addr", tor_addr);
		//uint32_t ret_addr = map_lookup(dest_addr);
		////print_ipaddr("tor_addr", ret_addr);
	}
    	free(ip_addr);
	free(nexthop_addr);

    struct sockaddr_in routerAddr;                  // Local address
	memset(&routerAddr, 0, sizeof(routerAddr));       // Zero out structure
	routerAddr.sin_family = AF_INET;                // IPv4 address family
	routerAddr.sin_addr.s_addr = map_lookup(inet_addr(recv_ip_addr));//inet_addr(routerIP); // an incoming interface
	routerAddr.sin_port = htons(recv_port_start);          // Local port
    int routerAddrLen = sizeof(routerAddr);

    print_ipaddr("routerAddr", routerAddr.sin_addr.s_addr);


    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
    int clntAddrLen = sizeof(clntAddr);

    struct alt_header alt_recv_header;
    memset(&alt_recv_header, 0, sizeof(struct alt_header));
    struct alt_header alt_send_header;
    memset(&alt_send_header, 0, sizeof(struct alt_header));


    for(int server_index = 0; server_index < expected_connections; server_index++){
        if(AddEpollEvent(&epstate, udp_socket_array[server_index], event_flag)== -1){
            perror("listen_sock cannot add EPOLLIN | EPOLLET\n");
            exit(1);
        }
    }

    ssize_t total_recv_bytes = 0;
    ssize_t total_send_bytes = 0;
    int accept_connections = 0;
    int max_retries = 20;
    pkt_counter=0;
    uint64_t log_counter=0;
    int once = 0;    

    char logfilename[100];
    const char log[] = ".qevents";
    const char filename_prefix[] = "/home/shw328/kvstore/log/";

    snprintf(logfilename, sizeof(filename_prefix) + sizeof(argv[3]) +  sizeof(argv[6]) + sizeof(identify_string) +
        sizeof(log) + 15, "%s%s_%s_%sthd%s", filename_prefix, identify_string, argv[6], argv[3], log);
    //FILE* output_fptr = fopen(logfilename, "w+");

    //TESTING DROP!
    int drop_once_req310 = 0;
    int drop_once_req510 = 0;

    uint64_t closedloop_counter = 0;
    uint64_t total_counter = 0;
    
    #ifdef GC_DELAY_ENABLE
    clock_gettime(CLOCK_REALTIME, &gc_ts1);
    #endif
    while(1){
    //while(!closed_loop_done){
        //printf("epoll_wait: waiting for connections\n");
        //printf("accepted connections: %d\n", accept_connections);
        int num_events = epoll_wait(epstate.epoll_fd, epstate.events, setsize, -1);
        if(num_events == -1){
            perror("epoll_wait");
            exit(1);
        }
        queued_events = num_events;

        //if (num_events > 0) {
            //printf("num_events:%d\n", num_events);
            //fprintf(output_fptr,"%d\n", num_events);
            //pkt_counter = pkt_counter + (uint64_t) num_events;
            //fprintf(output_fptr,"%ld\n", pkt_counter);
        //printf("fd:");
        for (int j = 0; j < num_events; j++){
            struct epoll_event *e = epstate.events+j;
            //printf("recv_fd:%d\n", e->data.fd);
            int drained_flag = 0;
            int req_perloop_counter = 0;
            while(!drained_flag){
                ssize_t numBytes = 0;
                ssize_t recv_byte_perloop = 0;
                ssize_t send_byte_perloop = 0;
                int recv_retries = 0; 
                int send_retries = 0;                    
                while(recv_byte_perloop < sizeof(struct alt_header)){
                    //numBytes = recvfrom(udp_socket_array[sock_index], (void*)&alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
                    numBytes = recvfrom(e->data.fd, (void*)&alt_recv_header, sizeof(struct alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
                    //numBytes = recv(e->data.fd, recv_buffer, 20, MSG_DONTWAIT);
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){ 
                            recv_retries++;   
                            if(recv_retries == max_retries){
                                drained_flag = 1;
                                //send_byte_perloop = sizeof(alt_header); //force it not entering send-loop
                                break;
                            }                        
                            continue; 
                        }
                        else{
                            printf("recvfrom failed on fd:%d\n", e->data.fd); 
                            send_byte_perloop = sizeof(struct alt_header);
                            break;
                        }
                    }
                    else if (numBytes == 0){ //&& recv_byte_perloop == 20){
                        if(recv_byte_perloop == sizeof(struct alt_header)){
                            break;
                        }
                        else{
                            recv_retries++;
                            printf("recv 0 byte on fd:%d\n", e->data.fd);
                            if(recv_retries == max_retries){
                                send_byte_perloop = sizeof(struct alt_header); //force it not entering send-loop
                                break;
                            }
                            else{
                                continue;
                            }
                        }
                    }
                    else{
                        recv_byte_perloop = recv_byte_perloop + numBytes;
                        total_recv_bytes = total_recv_bytes + numBytes;
                        printf("recv:%zd on fd %d\n", numBytes, e->data.fd);
                    }
                }                

                if(drained_flag){
                    //if(req_perloop_counter > 1)
                        //printf("fd: %d, drained after %d reqs\n", e->data.fd, req_perloop_counter);
                    break;
                }

                // clock_gettime(CLOCK_REALTIME, &ts1);
                // sleep_ts1=ts1;
                // realnanosleep(1000, &sleep_ts1, &sleep_ts2); // processing time 1 us
		
                while(send_byte_perloop < sizeof(struct alt_header)){
                    if(is_direct_to_client == 1){
                         ssize_t numBytes = sendto(e->data.fd, (void*) &alt_recv_header, sizeof(struct alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
                    }
                    else{
                        alt_recv_header.msgtype_flags = SINGLE_PKT_RESP_PASSTHROUGH;
                        alt_recv_header.service_id = 1;
                        //print_ipaddr("actual_src_ip", alt_recv_header.actual_src_ip);
                        uint32_t src_ip = alt_recv_header.actual_src_ip;                        
                        //printf("clntAddr.sin_port:%" PRIu16 "\n", clntAddr);
                        routerAddr.sin_port = clntAddr.sin_port;
                        alt_recv_header.alt_dst_ip = src_ip;
                        ssize_t numBytes = sendto(e->data.fd, (void*) &alt_recv_header, sizeof(struct alt_header), 0, (struct sockaddr *) &routerAddr, sizeof(routerAddr));
                    }
                    
                    //ssize_t numBytes = sendto(udp_socket_array[sock_index], (void*) &alt_send_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            printf("sendto EAGAIN on fd:%d\n", e->data.fd);
                            //fprintf(output_fptr,"sendto EAGAIN on fd:%d\n", e->data.fd);
                            continue;
                        }
                        else if(errno == EINVAL){
                            printf("sendto EINVAL on fd:%d\n", e->data.fd);
                            // char routerName[INET_ADDRSTRLEN]; // String to contain client address
                            // if (inet_ntop(AF_INET, &routerAddr.sin_addr.s_addr, routerName, sizeof(routerName)) != NULL){
                            //     printf("router addr: %s/ %d\n", routerName, ntohs(routerAddr.sin_port));
                            //     printf("router addrlen: %d\n", routerAddrLen);
                            // }
                            break;
                        }
                        else{
                            printf("sendto failed on fd:%d\n", e->data.fd); 
                            printf("failed errno: %s\n", strerror(errno));
                            //exit(1);
                            break;
                        }
                    }
                    else if (numBytes == 0 ){
                        if(send_byte_perloop == 20){
                            break;
                        }
                    }
                    else{
                        send_byte_perloop = send_byte_perloop + numBytes;
                        total_send_bytes = total_send_bytes + numBytes;
                        //printf("send:%zd on fd %d\n", numBytes, e->data.fd);                
                    }
                }
                total_counter++;
                req_perloop_counter++;
            }
            //TODO: update req-fd counter here
            fdreq_tracking_array[e->data.fd - fd_index_diff] = req_perloop_counter;
            if(e->data.fd == udp_socket_array[expected_connections-1]){
                //printf("closedloop_counter:%" PRIu64 "\n", closedloop_counter);
                closedloop_counter = closedloop_counter + req_perloop_counter;
            }
        }

        if(closedloop_counter == 1){
            clock_gettime(CLOCK_REALTIME, &start_ts);
        }
        //printf("\n");
        //printf("recv:%zd, send:%zd\n", total_recv_bytes, total_send_bytes);

        if(closedloop_counter > 99000 ){
            clock_gettime(CLOCK_REALTIME, &end_ts);
            uint64_t diff_us = clock_gettime_diff_us(&end_ts, &start_ts);
            double diff_seconds = (double) diff_us / 1000000.0;
            double recv_rate = (double) total_counter / diff_seconds; 
            printf("recv req rate: %lf\n", recv_rate);
            closedloop_counter = 0;
            total_counter = 0;
        }

        if(total_counter%10000 == 0 && total_counter > 0){
            printf("total_counter:%" PRIu64 "\n", total_counter);
        }

        //TODO: write req-fd counters to a file
        if(num_events > 0){
            int sum = 0;
            int non_zero_fd = 0;
            for(int index = 0; index < expected_connections; index++){
                //fprintf(output_fptr,"%d,", fdreq_tracking_array[index]);
                sum += fdreq_tracking_array[index];
                if(fdreq_tracking_array[index] > 0)
                   non_zero_fd+=1; 
                fdreq_tracking_array[index] = 0;
            }
            // if(non_zero_fd > 0){
            //     fprintf(output_fptr, "%d,%d\n", non_zero_fd, sum);
            // }
        }
        //fflush(output_fptr);
    }
    //fflush(output_fptr);
    //pthread_join(*feedback_thread, NULL);

	printf("closing sockets then\n");
    for(int server_index = 0; server_index < expected_connections; server_index++){
        close(udp_socket_array[server_index]);
    }
    free(server_addr_array);
    free(udp_socket_array);
    //free(fd_array);
}
