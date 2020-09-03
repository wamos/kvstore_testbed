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

#include "multi_dest_header.h"
//#include "multi_dest_protocol.h"

static const int MAXPENDING = 20; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;
int closed_loop_done;
int queued_events;
uint64_t pkt_counter;

// typedef struct __attribute__((__packed__)) {
//   uint16_t service_id;    // Type of Service.
//   uint16_t request_id;    // Request identifier.
//   uint16_t packet_id;     // Packet identifier.
//   uint16_t options;       // Options (could be request length etc.).
//   in_addr_t alt_dst_ip;
//   in_addr_t alt_dst_ip2;
// } alt_header;

typedef struct {
    //per thread state
    uint32_t tid;
    uint32_t feedback_period_us;
    uint32_t feedback_counter; 
    // fd related states
    int fd;     
    struct sockaddr_in server_addr; 
    // send/recv buffer
    alt_header send_header;
    alt_header recv_header;     
} feedback_thread_state; // per thread stats

void* feedback_mainloop(void *arg){
    feedback_thread_state* state = (feedback_thread_state*) arg;
    printf("feedback mainloop\n");
    struct timespec ts1, ts2;
    ssize_t numBytes = 0;
    int load_index = 0;
    uint16_t probe_counter=0;

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(state->tid, &cpuset);
	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    int routerAddrLen = sizeof(state->server_addr);
    clock_gettime(CLOCK_REALTIME, &ts1);
    int sec_counter = 0;
    while(!closed_loop_done){    
        //printf("while loop\n");
        //printf("fb: recv_req_count:%d\n", recv_req_count);

        clock_gettime(CLOCK_REALTIME, &ts2);
        uint64_t diff_us;
        if(ts1.tv_sec == ts2.tv_sec){
            diff_us = (uint64_t) (ts2.tv_nsec - ts1.tv_nsec)/1000; 
        }
        else{ 
            uint64_t ts1_nsec = (uint64_t) ts1.tv_nsec + (uint64_t) 1000000000*ts1.tv_sec;
            uint64_t ts2_nsec = (uint64_t) ts2.tv_nsec + (uint64_t) 1000000000*ts2.tv_sec;                    
            diff_us = (ts2_nsec - ts1_nsec)/1000;
        }
        //printf("diff_us: %" PRIu64 "\n");

        //if(queued_events > 50){ // for 09
        if(diff_us > 25){ // for 08
        //if(recv_req_count%10 == 0 && recv_req_count > 0){
            //printf("diff_us: %" PRIu64 "\n");
            //printf("probe packets\n");
            state->send_header.service_id = 10;

            state->send_header.options = (uint16_t) queued_events;  // 09
            //state->send_header.options = (uint16_t) 50;  // 08
            state->send_header.alt_dst_ip = inet_addr("10.0.0.18");
            state->send_header.alt_dst_ip2 = inet_addr("10.0.0.18");
            numBytes = sendto(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) routerAddrLen);
            state->feedback_counter++;
            clock_gettime(CLOCK_REALTIME, &ts1);
        }        
    }
    printf("feedback thread joined\n");

    return NULL;
}

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

int main(int argc, char *argv[]) {

	char* recv_ip_addr = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recv_port_start = (in_port_t) (argc > 2) ? strtoul(argv[2], NULL, 10) : 7000;
    // assume 4 pseudo-connections per open-loop thread and only 1 closed-loop pseudo-connection
    uint32_t expected_connections = (argc > 3) ? atoi(argv[3])*4+1: 1; // pseudo-connection for UDP
    char* identify_string = (argc > 4) ? argv[4]: "test";
    double rate = (argc > 5)? atof(argv[5]): 10000.0;
    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;
    char* routerIP = "10.0.0.18"; //argv[1];
    closed_loop_done = 0;
    queued_events = 0;

    ssize_t numBytesRcvd;
    ssize_t numBytesSend;
    int conn_count = 1;
    int setsize = 10240;
    int* fd_array = (int *) malloc(1024 * sizeof(int) );
    int fd_tail = 0; //int fd_head = 0;
    //memset(&fd_array, -1, sizeof(fd_array));
    
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
    int* ready_fd_array;
    int fd_index_diff; // the fd of these sockets starts at 4, usually so the diff is 4
    fdreq_tracking_array = (int *) malloc(expected_connections * sizeof(int) );
    ready_fd_array = (int *) malloc(expected_connections * sizeof(int) );
    //exit(1);

    struct sockaddr_in* server_addr_array;
    server_addr_array = (struct sockaddr_in *) malloc( expected_connections * sizeof(struct sockaddr_in) );

    for(int server_index = 0; server_index < expected_connections; server_index++){
        //printf("socket creation,");
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
        //printf("socket:%d,port:%u\n", udp_socket_array[server_index], recv_port_start);
        recv_port_start++;        

        if(UDPSocketSetup(udp_socket_array[server_index], servAddr) == -1){
            perror("UDPSocketSetup error\n"); 
            exit(1);
        }
    }
    fd_index_diff = udp_socket_array[0]; 

    pthread_t *feedback_thread;
    feedback_thread_state* fbk_state;
    feedback_thread = (pthread_t *)malloc( 1 * sizeof(pthread_t) );
    fbk_state = (feedback_thread_state *)malloc( 1 * sizeof(feedback_thread_state) );

    // Init feedback_thread_state fbk_state
    fbk_state->tid = 0;
    fbk_state->feedback_period_us = 1000; // 1000 microseconds period for feedback
    fbk_state->feedback_counter = 0;

    if ((fbk_state->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

    struct sockaddr_in routerAddr;                  // Local address
	memset(&routerAddr, 0, sizeof(routerAddr));       // Zero out structure
	routerAddr.sin_family = AF_INET;                // IPv4 address family
	routerAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
	routerAddr.sin_port = htons(recv_port_start);          // Local port
    int routerAddrLen = sizeof(routerAddr);

    fbk_state->server_addr = routerAddr;    
    memset(&fbk_state->recv_header, 0, sizeof(alt_header));
    memset(&fbk_state->send_header, 0, sizeof(alt_header));
    //pthread_create(feedback_thread, NULL, feedback_mainloop, fbk_state);


    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
    int clntAddrLen = sizeof(clntAddr);

    alt_header alt_recv_header;
    memset(&alt_recv_header, 0, sizeof(alt_header));
    alt_header alt_send_header;
    memset(&alt_send_header, 0, sizeof(alt_header));

    alt_send_header.service_id = 1;
    alt_send_header.request_id = 0;
    alt_send_header.options = 10;    

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

    snprintf(logfilename, sizeof(filename_prefix) + sizeof(argv[3]) +  sizeof(argv[5]) + sizeof(identify_string) +
        sizeof(log) + 15, "%s%s_%s_%sthd%s", filename_prefix, identify_string, argv[5], argv[3], log);
    FILE* output_fptr = fopen(logfilename, "w+");

    //TESTING DROP!
    int drop_once_req310 = 0;
    int drop_once_req510 = 0;

    while(1){
    //while(!closed_loop_done){
        //printf("epoll_wait: waiting for connections\n");
        //printf("accepted connections: %d\n", accept_connections);
        int num_events = epoll_wait(epstate.epoll_fd, epstate.events, setsize, -1);
        if(num_events == -1){
            perror("epoll_wait");
            exit(1);
        }

        memset(ready_fd_array, 0, sizeof(int));
        for (int j = 0; j < num_events; j++){
            struct epoll_event *e = epstate.events+j;
            int index = e->data.fd - fd_index_diff;
            ready_fd_array[index] = 1;
        }

        int num_empty_fd = 0;
        int runningRoundRobin = 1;
        while(num_empty_fd < num_events){
            for (int j = 0; j < num_events; j++){
                struct epoll_event *e = epstate.events+j;
                int index = e->data.fd - fd_index_diff;
                //printf("%d,", e->data.fd);
                if(ready_fd_array[index] == 1){
                    ssize_t numBytes = 0;
                    ssize_t recv_byte_perloop = 0;
                    ssize_t send_byte_perloop = 0;
                    int recv_retries = 0; 
                    int send_retries = 0;                    
                    while(recv_byte_perloop < sizeof(alt_header)){
                        //numBytes = recvfrom(udp_socket_array[sock_index], (void*)&alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
                        numBytes = recvfrom(e->data.fd, (void*)&alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
                        //numBytes = recv(e->data.fd, recv_buffer, 20, MSG_DONTWAIT);
                        if (numBytes < 0){
                            if((errno == EAGAIN) || (errno == EWOULDBLOCK)){ 
                                recv_retries++;   
                                if(recv_retries == max_retries){
                                    ready_fd_array[index] = 0;
                                    send_byte_perloop = sizeof(alt_header); //force it not entering send-loop
                                    break;
                                }                        
                                continue; 
                            }
                            else{
                                printf("recvfrom failed on fd:%d\n", e->data.fd); 
                                send_byte_perloop = sizeof(alt_header);
                                break;
                            }
                        }
                        else if (numBytes == 0){ //&& recv_byte_perloop == 20){
                            if(recv_byte_perloop == sizeof(alt_header)){
                                break;
                            }
                            else{
                                recv_retries++;
                                printf("recv 0 byte on fd:%d\n", e->data.fd);
                                if(recv_retries == max_retries){
                                    send_byte_perloop = sizeof(alt_header); //force it not entering send-loop
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
                            //printf("recv:%zd on fd %d\n", numBytes, e->data.fd);
                        }
                    }      

                    //clock_gettime(CLOCK_REALTIME, &ts1);
                    //sleep_ts1=ts1;
                    //realnanosleep(10*1000, &sleep_ts1, &sleep_ts2); // processing time 10 us

                    while(send_byte_perloop < sizeof(alt_header)){
                        //numBytes = send(e->data.fd, send_buffer, 20, MSG_DONTWAIT);
                        //alt_send_header.alt_dst_ip = server_addr_array[0].sin_addr.s_addr;
                        ssize_t numBytes = sendto(e->data.fd, (void*) &alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));                    
                        //ssize_t numBytes = sendto(udp_socket_array[sock_index], (void*) &alt_send_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
                        if (numBytes < 0){
                            if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                                printf("sendto EAGAIN on fd:%d\n", e->data.fd);
                                //fprintf(output_fptr,"sendto EAGAIN on fd:%d\n", e->data.fd);
                                continue;
                            }
                            else{
                                printf("sendto failed on fd:%d\n", e->data.fd); 
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
                    fdreq_tracking_array[e->data.fd - fd_index_diff]+=1;
                }
                else{
                    num_empty_fd++;
                }
            }
        }
        //printf("\n");
        //printf("recv:%zd, send:%zd\n", total_recv_bytes, total_send_bytes);

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
            if(non_zero_fd > 0){
                fprintf(output_fptr, "%d,%d\n", non_zero_fd, sum);
            }
            //fprintf(output_fptr, "\n");
        }
        fflush(output_fptr);
    }
    //fflush(output_fptr);
    //pthread_join(*feedback_thread, NULL);

	printf("closing sockets then\n");
    for(int server_index = 0; server_index < expected_connections; server_index++){
        close(udp_socket_array[server_index]);
    }
    free(server_addr_array);
    free(udp_socket_array);
    free(fd_array);
}