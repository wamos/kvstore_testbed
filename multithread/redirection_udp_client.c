#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/tcp.h>

#include "nanosleep.h"
#include "epoll_state.h"
#include "dist_gen.h"

#include "alt_header.h"
#include "map_containers.h"
#include "aws_config.h"
//#include "multi_dest_protocol.h"

#define OPEN_LOOP_ENABLE 1
#define MAX_NUM_REQ 100*1000
#define MAX_NUM_TEST 200*1000
#define MAX_NUM_SAMPLE 100*1000 //100*1000
//#define AWS_HASHTABLE 1

typedef struct {
    uint32_t tid;
    int fd;
    struct sockaddr_in server_addr;
    struct alt_header temp_send_buffer;
    struct alt_header temp_recv_buffer;
} udp_baseline_connection;

typedef struct {
    //per thread state
    uint64_t* arrival_pattern;
    epollState epstate;
    uint32_t tid;
    uint32_t conn_perthread;
    uint32_t sent_req;
    uint32_t completed_req;
    int64_t send_bytes;
    int64_t recv_bytes;
    //each pseudo-connection has it own addr to sendto()
    udp_baseline_connection* conn_list;
} udp_thread_state; // per thread stats

pthread_barrier_t all_threads_ready;

//static __thread tcp_connection* conn_list;
static struct sockaddr_in senderAddr;
int senderAddrLen;
int is_direct_to_server;
int is_random_selection;
struct timespec ts_start;
int closed_loop_done;

char* recv_ip_addr;
char* recv_ip_addr2;
char* recv_ip_addr3;
char* send_ip_addr;

void* fake_mainloop(void *arg){
    int num = *(int*) arg;
    printf("fake mainloop:%d\n", num);
    return NULL;
}

//open-loop connections sendto/recvfrom for UDP
void* openloop_multiple_connections(void *arg){
    printf("thread_openloop\n");
    udp_thread_state* state = (udp_thread_state*) arg;
    udp_baseline_connection* conn_list = state->conn_list;
    int once = 0;
    int max_retries = 20;

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(state->tid, &cpuset);

	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    pthread_barrier_wait(&all_threads_ready);

    //int send_flag = MSG_DONTWAIT;
    //int recv_flag = MSG_DONTWAIT;
    struct timespec ts1, ts2, ts3, ts4, ts5, ts6;

    uint32_t send_conn_id = 0;
    //uint32_t complete_req_id = 0;
    uint64_t interval, current_time;
    uint64_t next_send = clock_gettime_us(&ts1);

    ssize_t numBytes = 0;
    int servAddrLen = sizeof(conn_list[0].server_addr);
    while(state->completed_req <  MAX_NUM_TEST*state->conn_perthread){       
    //while(!closed_loop_done){ // the real one
        current_time = clock_gettime_us(&ts2);
        while( current_time >= next_send && state->sent_req < MAX_NUM_TEST*state->conn_perthread){
        //while( current_time >= next_send && !closed_loop_done){ // the real one
            uint32_t req_id = state->sent_req;
            if(req_id > MAX_NUM_REQ){
                //printf("Having reqs > MAX_NUM_REQ, randomly assign an index\n");
                req_id = rand()%MAX_NUM_REQ;
            }
            interval = (uint64_t) state->arrival_pattern[req_id];
            next_send = current_time + interval;

            conn_list[send_conn_id].temp_send_buffer.service_id = 1;
            conn_list[send_conn_id].temp_send_buffer.request_id = state->tid * MAX_NUM_TEST + req_id;

            // random replica selection for send dest
            if(is_random_selection && !is_direct_to_server){
                int replica_num = rand()%NUM_REPLICA;
                conn_list[send_conn_id].temp_send_buffer.alt_dst_ip = conn_list[send_conn_id].temp_send_buffer.replica_dst_list[replica_num];
                uint32_t tor_ipaddr = map_lookup(conn_list[send_conn_id].temp_send_buffer.alt_dst_ip);
                conn_list[send_conn_id].server_addr.sin_addr.s_addr = tor_ipaddr;
            }

            //printf("conn_list[send_conn_id].pending_event[pending_index]:%p\n", (void*) conn_list[send_conn_id].pending_event[pending_index]);

            ssize_t send_bytes = 0;
            while(send_bytes < sizeof(struct alt_header)){
                numBytes = sendto(conn_list[send_conn_id].fd, (void*) &conn_list[send_conn_id].temp_send_buffer, sizeof(struct alt_header), 0, (struct sockaddr *) &conn_list[send_conn_id].server_addr, (socklen_t) servAddrLen);

                if (numBytes < 0){
                    printf("send() failed\n");
                    exit(1);
                }
                else{
                    send_bytes = send_bytes + numBytes;
                    state->send_bytes += numBytes;
                    //printf("thread id %" PRIu32 "send:%zd on fd:%d\n", state->tid, numBytes, conn_list[conn_index].fd);
                    //printf("send:%zd\n", numBytes);
                }
            }

            state->sent_req++; // update the sent_req counter for the thread
            send_conn_id = (send_conn_id + 1)%state->conn_perthread;

            current_time = clock_gettime_us(&ts2);
        }
        clock_gettime(CLOCK_REALTIME, &ts3);
        
        int num_events = epoll_wait(state->epstate.epoll_fd, state->epstate.events, 10*state->conn_perthread, 0);
        //printf("thread id %" PRIu32 "num_events %d\n", state->tid, num_events);
        for (int i = 0; i < num_events; i++) {
            clock_gettime(CLOCK_REALTIME, &ts4);
            uint32_t conn_index = state->epstate.events[i].data.u32;
            //printf("\nprocess epoll_wait events, conn index %" PRIu32 "\n", conn_index);
            //printf("events fd:%d, conn_list fd:%d\n", state->epstate.events[i].data.fd, conn_list[conn_index].fd);
            int drained_flag = 0;
            int req_perloop_counter = 0;
            while(!drained_flag){
                //printf("thread id %" PRIu32 " not drained\n", state->tid);
                ssize_t recv_byte_perloop = 0;
                int recv_retries = 0;            
                uint8_t timeout_flag = 0;            
                while(recv_byte_perloop < sizeof(struct alt_header)){
                    //int servAddrLen = sizeof(conn_list[conn_index].server_addr);
                    numBytes = recvfrom(conn_list[conn_index].fd, (void*) &conn_list[conn_index].temp_recv_buffer, sizeof(struct alt_header), MSG_DONTWAIT, (struct sockaddr *) &conn_list[conn_index].server_addr, (socklen_t*) &servAddrLen);
                    //numBytes = recvfrom(conn_list[conn_index].fd, (void*) &conn_list[conn_index].timer_event->recv_header, sizeof(alt_header), MSG_DONTWAIT, (struct sockaddr *) &conn_list[conn_index].server_addr, (socklen_t*) &servAddrLen);
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            recv_retries++;   
                            if(recv_retries == max_retries){
                                drained_flag = 1;
                                break;
                            }   
                            continue;
                        }
                        else{
                            printf("thread id %" PRIu32 " recv() failed\n", state->tid);
                            //break;
                            exit(1);
                        }
                    }
                    else if(numBytes==0){
                        printf("thread id %" PRIu32 " recv() 0 bytes\n", state->tid);
                        if(recv_byte_perloop == sizeof(struct alt_header)){
                            break;
                        }
                        else{
                            recv_retries++;
                            printf("recv 0 byte on fd:%d\n",conn_list[conn_index].fd);
                            if(recv_retries == max_retries){
                                drained_flag = 1;
                                break;
                            }
                            else{
                                continue;
                            }
                        }
                    }
                    else{
                        recv_byte_perloop = recv_byte_perloop + numBytes;
                        state->recv_bytes += numBytes;
                        printf("thread id %" PRIu32 "recv:%zd on fd:%d\n", state->tid, numBytes, conn_list[conn_index].fd);
                    }
                }

                if(drained_flag){
                    //printf("thread id %" PRIu32 " is drained\n", state->tid);
                    break;
                }

                state->completed_req+=1;

                if(state->completed_req%10000 == 0){
                    printf("*** thread id %" PRIu32 ", send:%" PRIu32 ", recv:%" PRIu32 "\n", state->tid, state->sent_req ,state->completed_req);
                }

            }         
        }                
    }
    clock_gettime(CLOCK_REALTIME, &ts4);
    uint64_t diff_us = clock_gettime_diff_us(&ts1, &ts4);
    double diff_seconds = (double) diff_us / 1000000.0;
    double send_rate = (double) state->sent_req / diff_seconds;
    double recv_rate = (double) state->completed_req / diff_seconds; 
    printf("*** thread id %" PRIu32 ", send:%" PRIu32 ", recv:%" PRIu32 "\n", state->tid, state->sent_req ,state->completed_req);
    printf("*** thread id %" PRIu32 ", diff_us:%" PRIu64 ", send rate:%lf , recv rate:%lf \n", state->tid, diff_us, send_rate, recv_rate);

    return NULL;
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
    double rate = (argc > 1)? atof(argv[1]): 2000.0;
    is_direct_to_server = (argc > 2) ? atoi(argv[2]) : 1;
    is_random_selection = (argc > 3) ? atoi(argv[3]) : 1;
    uint32_t num_threads_openloop = (uint32_t) (argc > 4) ? atoi(argv[4]): 2; //30;
    in_port_t recv_port_start = (in_port_t) (argc > 5) ? strtoul(argv[5], NULL, 10) : 7000;        
    uint32_t conn_perthread = (uint32_t) (argc > 6) ? atoi(argv[6]): 4;

    recv_ip_addr = (argc > 7) ? argv[7]: "10.0.0.8";
    recv_ip_addr2 = (argc > 8) ? argv[8]: "10.0.0.11";
    recv_ip_addr3 = (argc > 9) ? argv[9]: "10.0.0.2";
    send_ip_addr = (argc > 10) ? argv[10]: "10.0.0.9";
    //char* identify_string = (argc > 11) ? argv[11]: "test";
    //const char filename_prefix[] = "/home/shw328/kvstore/log/";
    //const char filename_prefix_tmp[] = "/tmp/";
    //const char log[] = ".log";

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
        //print_ipaddr("dest_addr", dest_addr);
        //print_ipaddr("tor_addr", tor_addr);
        //uint32_t ret_addr = map_lookup(dest_addr);
        //print_ipaddr("tor_addr", ret_addr);
	}
    free(ip_addr);
	free(nexthop_addr);

    struct timespec ts1, ts2;

    //seed the rand() function
    srand(1); 

    if(is_direct_to_server)
        printf("client sends directly to server!\n");
    else
        printf("client requests are routed through DPDK switches!\n");

    double* poisson_placeholder = (double *)malloc( MAX_NUM_REQ * sizeof(double) );
    uint64_t* poisson_arrival_pattern = (uint64_t *)malloc( MAX_NUM_REQ * sizeof(uint64_t) );

    // generate intervals of poisson inter arrival
    printf("rate:%lf\n", rate);
    GenPoissonArrival(rate, MAX_NUM_REQ, poisson_placeholder);
    for(int n = 0; n < MAX_NUM_REQ; n++) {
        poisson_arrival_pattern[n] = (uint64_t) round(poisson_placeholder[n]);
        //printf("%" PRIu64 ", %.3lf\n", poisson_arrival_pattern[n], poisson_placeholder[n]);
    }
    free(poisson_placeholder);

    struct sockaddr_in* server_addr_array;
    int server_index = 0;
    uint32_t expected_connections = (uint32_t) conn_perthread * num_threads_openloop;
    server_addr_array = (struct sockaddr_in *) malloc( expected_connections * sizeof(struct sockaddr_in) );

    pthread_t *openloop_threads;
    udp_thread_state *openloop_thread_state;
    if(pthread_barrier_init(&all_threads_ready, NULL, num_threads_openloop) == -1){
        perror("pthread_barrier_init fails");
        exit(1);
    }

    #ifdef OPEN_LOOP_ENABLE
    openloop_threads      = (pthread_t *)malloc( num_threads_openloop * sizeof(pthread_t) );
    openloop_thread_state = (udp_thread_state *)malloc( num_threads_openloop * sizeof(udp_thread_state) );

    printf("setting up open-loop connection\n");
    printf("num_threads_openloop:%" PRIu32 "\n", num_threads_openloop);
    //setting up connection for each thread sequentially
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        openloop_thread_state[thread_id].conn_list = (udp_baseline_connection *)malloc( conn_perthread * sizeof(udp_baseline_connection));
        //set up a per-thread epoll fd for multiplexing recv on multiple connections
        if(CreateEpoll( &openloop_thread_state[thread_id].epstate, 1024) == -1){
           perror("per-thread epoll fd failed to be created\n");
        }

        uint32_t event_flag = EPOLLIN | EPOLLET;
        for(uint32_t conn_index = 0; conn_index < conn_perthread; conn_index++){
            openloop_thread_state[thread_id].conn_list[conn_index].fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

            if(openloop_thread_state[thread_id].conn_list[conn_index].fd == -1){
                printf("socket creation fails\n");
                exit(1);
            }

            //bind the sender socket to a certain an address, so it can receive packets from all destinations
            memset(&senderAddr, 0, sizeof(senderAddr));
            senderAddr.sin_addr.s_addr = inet_addr(send_ip_addr);
            senderAddr.sin_family = AF_INET;
            senderAddr.sin_port = htons(recv_port_start);

            if (bind(openloop_thread_state[thread_id].conn_list[conn_index].fd, (struct sockaddr*) &senderAddr, sizeof(struct sockaddr_in)) < 0){
		        perror("bind() failed\n");
                exit(1);
            }

            int flag = 1;
	        if (setsockopt(openloop_thread_state[thread_id].conn_list[conn_index].fd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(int)) == -1) { 
                perror("setsockopt SO_REUSEADDR error\n"); 
                exit(1); 
            } 

            // Set server address:
            // for direct connections, i.e. is_direct_to_server == 1
            // -> servAddr is the actual server address
            // for connection with DPDK switch involved 
            // -> servAddr is the ToR address in front of the server
            struct sockaddr_in servAddr;
            memset(&servAddr, 0, sizeof(servAddr));
            servAddr.sin_family = AF_INET;

            if(is_direct_to_server)
                servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
            else // to the router
                servAddr.sin_addr.s_addr = map_lookup(inet_addr(recv_ip_addr)); //recv_ip_addr is the deafult address
            print_ipaddr("router addr:", servAddr.sin_addr.s_addr);

            servAddr.sin_port = htons(recv_port_start);
            server_addr_array[server_index] = servAddr;
            printf("socket:%d,port:%u\n", openloop_thread_state[thread_id].conn_list[conn_index].fd, recv_port_start);
            server_index++;
            recv_port_start++;

            //Set up openloop_thread_state regarding to alt_header fields
            openloop_thread_state[thread_id].conn_list[conn_index].server_addr = servAddr;
            openloop_thread_state[thread_id].conn_list[conn_index].tid = thread_id*2;

            memset(&openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer, 0, sizeof(struct alt_header));
            memset(&openloop_thread_state[thread_id].conn_list[conn_index].temp_recv_buffer, 0, sizeof(struct alt_header));

            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.msgtype_flags = SINGLE_PKT_REQ_PASSTHROUGH;
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.redirection = 0;  
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.header_size = sizeof(struct alt_header);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.reserved = 0;

            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.feedback_options = 0;
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.request_id = 0;
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.service_id = 1;
                        
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.actual_src_ip = inet_addr(send_ip_addr);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.alt_dst_ip = inet_addr(recv_ip_addr);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.replica_dst_list[0] = inet_addr(recv_ip_addr);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.replica_dst_list[1] = inet_addr(recv_ip_addr2);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.replica_dst_list[2] = inet_addr(recv_ip_addr3);

            //add an event for each connection and assign u32 as the conn index
            AddEpollEventWithData(&openloop_thread_state[thread_id].epstate, openloop_thread_state[thread_id].conn_list[conn_index].fd,
                conn_index, event_flag);
        }

    }
    #else
        printf("no open-loop connections!\n");
    #endif

    clock_gettime(CLOCK_REALTIME, &ts_start);
    #ifdef OPEN_LOOP_ENABLE
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        //[UNTESTED]: timer_wheel
        //if(init_timer_wheel(&openloop_thread_state[thread_id].timer_wheel, 5000) < 0){
        //    printf("timer_wheel init fails\n");
        //    exit(1);
        //}
        //openloop_thread_state[thread_id].timer_wheel.rto_interval = 2500;

        openloop_thread_state[thread_id].arrival_pattern = poisson_arrival_pattern;
        openloop_thread_state[thread_id].tid = thread_id*2;
        openloop_thread_state[thread_id].conn_perthread = conn_perthread;
        openloop_thread_state[thread_id].sent_req = 0;
        openloop_thread_state[thread_id].completed_req = 0;
        openloop_thread_state[thread_id].send_bytes = 0;
        openloop_thread_state[thread_id].recv_bytes = 0;
        clock_gettime(CLOCK_REALTIME, &ts1);
        realnanosleep(25*1000, &ts1, &ts2);  //sleep 25 usecs
        pthread_create(&openloop_threads[thread_id], NULL, openloop_multiple_connections, &openloop_thread_state[thread_id]);
        //pthread_create(&openloop_threads[thread_id], NULL, closedloop_multiple_connections, &openloop_thread_state[thread_id]);        
        //pthread_create(&openloop_threads[i], NULL, fake_mainloop, (void *) &i);
    }
    #endif

    //when do we launch closed-loop threads?
    //     We could start it after the open-loop threads have warmed up the server so we don't observe transient phenomena
    //     like we've seen on an redis server idling more than 500 usecs
    //clock_gettime(CLOCK_REALTIME, &ts1);
    //realnanosleep(1000*1000, &ts1, &ts2);  //sleep 1000 usecs

    #ifdef OPEN_LOOP_ENABLE
    printf("openloop_threads:\n");
    for (uint32_t thread_id = 0;  thread_id < num_threads_openloop; thread_id++){
        pthread_join(openloop_threads[thread_id], NULL);
        //close epoll fd and free its associated event list
        CloseEpoll(&openloop_thread_state[thread_id].epstate);
        printf("thread id %" PRIu32 ":", openloop_thread_state[thread_id].tid);
        printf("sent req:%" PRIu32 ",", openloop_thread_state[thread_id].sent_req);
        printf("completed req:%" PRIu32 ",", openloop_thread_state[thread_id].completed_req);
        printf("send:%" PRId64 ",", openloop_thread_state[thread_id].send_bytes);
        printf("recv:%" PRId64 "\n", openloop_thread_state[thread_id].recv_bytes);
    }

    printf("free openloop_threads\n");
    free(openloop_threads);
    #endif

    #ifdef OPEN_LOOP_ENABLE
    printf("free openloop_thread_state conn list\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        free(openloop_thread_state[thread_id].conn_list);
    }

    printf("free openloop_thread_state\n");
    free(openloop_thread_state);
    #endif

    free(server_addr_array);
    free(poisson_arrival_pattern);

    return 0;
}
