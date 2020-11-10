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

#include "multi_dest_header.h"
#include "multi_dest_protocol.h"

#define OPEN_LOOP_ENABLE 1
#define MAX_NUM_REQ 100*1000
#define MAX_NUM_TEST 200*1000
#define MAX_NUM_SAMPLE 100*1000 //100*1000

// typedef struct __attribute__((__packed__)) {
//   uint16_t service_id;    // Type of Service.
//   uint16_t request_id;    // Request identifier.
//   uint16_t packet_id;     // Packet identifier.
//   uint16_t options;       // Options (could be request length etc.).
//   in_addr_t alt_dst_ip;
//   in_addr_t alt_dst_ip2;
// } alt_header;

typedef struct {
    uint32_t tid;
    int fd;
    struct sockaddr_in server_addr;
    alt_header temp_send_buffer;
    alt_header temp_recv_buffer;
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
    //[UNTESTED]: send_buffer and timer_wheel
    multi_dest_buffer buffer;
    simple_timer_wheel timer_wheel;    
} udp_thread_state; // per thread stats

typedef struct {
    //per thread state
    uint32_t tid;
    FILE* output_fptr; // output latency numbers
    int fd; // pseudo-connections share a socket
    alt_header send_header;
    alt_header recv_header;
    uint32_t num_req;
    int64_t send_bytes;
    int64_t recv_bytes;
    struct sockaddr_in server_addr;
} udp_latency_state;

pthread_barrier_t all_threads_ready;

//static __thread tcp_connection* conn_list;
static struct sockaddr_in routerAddr;
int routerAddrLen;
int is_direct_to_server;
int is_random_selection;
struct timespec ts_start;
int closed_loop_done;

char* recv_ip_addr;
char* recv_ip_addr2;
char* recv_ip_addr3;
char* router_ip_addr;

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

            //rto_timer_event* event = acquire_multi_dest_header(&conn_list[send_conn_id].buffer);
            // if(event == NULL){
            //     break;
            // }
            conn_list[send_conn_id].temp_send_buffer.service_id = 1;
            conn_list[send_conn_id].temp_send_buffer.request_id = state->tid * MAX_NUM_TEST + req_id;

            // random replica selection for send dest
            if(is_random_selection){
                int replica_num = rand()%3;
                if(replica_num == 0){
                    conn_list[send_conn_id].server_addr.sin_addr.s_addr = conn_list[send_conn_id].temp_send_buffer.alt_dst_ip;
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip = inet_addr(recv_ip_addr);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip2 = inet_addr(recv_ip_addr2);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip3 = inet_addr(recv_ip_addr3);
                }
                else if(replica_num == 1){
                    conn_list[send_conn_id].server_addr.sin_addr.s_addr = conn_list[send_conn_id].temp_send_buffer.alt_dst_ip2;
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip = inet_addr(recv_ip_addr2);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip2 = inet_addr(recv_ip_addr3);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip3 = inet_addr(recv_ip_addr);
                }
                else{
                    conn_list[send_conn_id].server_addr.sin_addr.s_addr = conn_list[send_conn_id].temp_send_buffer.alt_dst_ip3;
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip = inet_addr(recv_ip_addr3);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip2 = inet_addr(recv_ip_addr);
                    conn_list[send_conn_id].temp_send_buffer.alt_dst_ip3 = inet_addr(recv_ip_addr2);
                }
            }

            //printf("conn_list[send_conn_id].pending_event[pending_index]:%p\n", (void*) conn_list[send_conn_id].pending_event[pending_index]);

            ssize_t send_bytes = 0;
            while(send_bytes < sizeof(alt_header)){
                if(is_direct_to_server){
                    numBytes = sendto(conn_list[send_conn_id].fd, (void*) &conn_list[send_conn_id].temp_send_buffer, sizeof(alt_header), 0, (struct sockaddr *) &conn_list[send_conn_id].server_addr, (socklen_t) servAddrLen);
                }
                else{
                    routerAddr.sin_port = conn_list[send_conn_id].server_addr.sin_port;
                    numBytes = sendto(conn_list[send_conn_id].fd, (void*) &conn_list[send_conn_id].temp_send_buffer, sizeof(alt_header), 0, (struct sockaddr *) &routerAddr, (socklen_t) routerAddrLen);
                }

                if (numBytes < 0){
                    printf("send() failed\n");
                    exit(1);
                }
                else{
                    send_bytes = send_bytes + numBytes;
                    state->send_bytes += numBytes;
                    //printf("send:%zd\n", numBytes);
                }
            }
            //if(send_conn_id == 3)
            //printf("send_conn_id:%" PRIu32 "\n", send_conn_id);;

            // event->received_tick = UINTMAX_MAX;          
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
                while(recv_byte_perloop < sizeof(alt_header)){
                    //int servAddrLen = sizeof(conn_list[conn_index].server_addr);
                    numBytes = recvfrom(conn_list[conn_index].fd, (void*) &conn_list[conn_index].temp_recv_buffer, sizeof(alt_header), MSG_DONTWAIT, (struct sockaddr *) &conn_list[conn_index].server_addr, (socklen_t*) &servAddrLen);
                    //numBytes = recvfrom(conn_list[conn_index].fd, (void*) &conn_list[conn_index].timer_event->recv_header, sizeof(alt_header), MSG_DONTWAIT, (struct sockaddr *) &conn_list[conn_index].server_addr, (socklen_t*) &servAddrLen);
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                        //printf("thread id %" PRIu32 "EAGAIN\n", state->tid);
                            // clock_gettime(CLOCK_REALTIME, &ts5);
                            // uint64_t diff_us = clock_gettime_diff_us(&ts5, &ts4);
                            // if(diff_us > state->timer_wheel.rto_interval){
                            //     printf("RTO! in recv loop\n");
                            //     timeout_flag = 1;
                            //     break;
                            // }
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
                        if(recv_byte_perloop == sizeof(alt_header)){
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
                        //printf("thread id %" PRIu32 "recv:%zd on fd:%d\n", state->tid, numBytes, conn_list[conn_index].fd);
                    }
                }

                if(drained_flag){
                    //printf("thread id %" PRIu32 " is drained\n", state->tid);
                    break;
                }

                //[TEMP] comment this out when it's fixed
                //reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, tail_event, state->conn_perthread, state->tid);
                state->completed_req+=1;

                //printf("recvfrom fd %d, temp recv request_id:%" PRIu32 ", event send request_id:%" PRIu32 "\n", 
                    //conn_list[conn_index].fd, conn_list[conn_index].buffer.temp_recv_buffer.request_id, tail_event->recv_header.request_id);

                // if(!timeout_flag){
                //     //printf("recvfrom fd %d request_id:%" PRIu32 ",", conn_list[conn_index].fd, conn_list[conn_index].pending_event[pending_index]->recv_header.request_id);
                //     //printf("thread id %" PRIu32 ",fd %" PRIu32 ",recv %zd\n", state->tid, conn_list[conn_index].fd, numBytes);
                //printf("thread id %" PRIu32 ", send:%" PRIu32 ", recv:%" PRIu32 "\n", state->tid, state->sent_req ,state->completed_req);

                if(state->completed_req%10000 == 0){
                    printf("*** thread id %" PRIu32 ", send:%" PRIu32 ", recv:%" PRIu32 "\n", state->tid, state->sent_req ,state->completed_req);
                }

                //     state->completed_req+=1;
                // }
                // else{
                //     printf("timeout request_id:%" PRIu32 ",", conn_list[conn_index].pending_event[pending_index]->recv_header.request_id);
                //     //printf("timeout\n");
                // } 
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

void* closedloop_latency_measurement(void *arg){
    printf("closedloop_latency_measurement\n");
    udp_latency_state* state = (udp_latency_state*) arg;
    struct timespec ts1, ts2;
    uint32_t counter_replica0 = 0;
    uint32_t counter_replica1 = 0;
    uint32_t counter_replica2 = 0;

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(state->tid, &cpuset);

	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    int send_flag = MSG_DONTWAIT;
    int recv_flag = MSG_DONTWAIT;
    int servAddrLen = sizeof(state->server_addr);
    for(int i = 0; i < MAX_NUM_SAMPLE; i++){
        ssize_t numBytes;

        // random replica selection for send dest
        if(is_random_selection){
            int replica_num = rand()%3;
            if(replica_num == 0){
                state->server_addr.sin_addr.s_addr = state->send_header.alt_dst_ip;
                counter_replica0++;
            }
            else if(replica_num == 1){
                state->server_addr.sin_addr.s_addr = state->send_header.alt_dst_ip2;
                counter_replica1++;
            }
            else{
                state->server_addr.sin_addr.s_addr = state->send_header.alt_dst_ip3;
                counter_replica2++;
            }
        }

        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_byte_perloop = 0;
     
        while(send_byte_perloop < sizeof(alt_header)){
            numBytes = sendto(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) servAddrLen);

            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    continue;
                }
                else{
                    printf("thread id %" PRIu32 "send() failed\n", state->tid);
                    exit(1);
                }
            }else{
                send_byte_perloop = send_byte_perloop + numBytes;
                state->send_bytes += numBytes;
                //printf("closed-loop thread id %" PRIu32 ",send:%zd, iter:%d\n", state->tid, numBytes, i);
            }
        }

        //printf("closed-loop thread id %" PRIu32 "before recv()\n", state->tid);
        ssize_t recv_byte_perloop = 0;
        while(recv_byte_perloop < sizeof(alt_header)){
            numBytes = recvfrom(state->fd, (void*) &state->recv_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t*) &servAddrLen);
            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    continue;
                }
                else{
                    printf("thread id %" PRIu32 "recv() failed\n", state->tid);
                    exit(1);
                }
            }else{
                recv_byte_perloop = recv_byte_perloop + numBytes;
                state->recv_bytes += numBytes;
                //printf("recv:%zd\n", numBytes);
            }
        }

        clock_gettime(CLOCK_REALTIME, &ts2);
        //printf("closed-loop thread id %" PRIu32 ", send:%" PRId64 ", recv:%" PRId64 "\n", state->tid, state->send_bytes ,state->recv_bytes);
        printf("closed-loop thread id %" PRIu32 ", completed:%" PRIu32 "\n", state->tid, state->num_req);
        uint64_t duration = clock_gettime_diff_ns(&ts1,&ts2);
        fprintf(state->output_fptr,"%" PRIu64 "\n", duration);
        state->num_req+=1;
        // if(state->num_req%100 == 0){
        //     printf("*** thread id %" PRIu32 ", completed:%" PRIu32 "\n", state->tid, state->num_req);
        // }
    }

    fflush(state->output_fptr);

    printf("replica 0 counter:%" PRIu32 "\n", counter_replica0);
    printf("replica 1 counter:%" PRIu32 "\n", counter_replica1);
    printf("replica 2 counter:%" PRIu32 "\n", counter_replica2);
    printf("thread id %" PRIu32 " closing up connection\n", state->tid);
    close(state->fd);

    closed_loop_done = 1;

    return NULL;
}

int main(int argc, char *argv[]) {    
    double rate = (argc > 1)? atof(argv[1]): 2000.0;
    is_direct_to_server = (argc > 2) ? atoi(argv[2]) : 1;
    is_random_selection = (argc > 3) ? atoi(argv[3]) : 1;
    uint32_t num_threads_openloop = (uint32_t) (argc > 4) ? atoi(argv[4]): 30;
    uint32_t num_threads_closedloop = (uint32_t) (argc > 5) ? atoi(argv[5]): 1;
    char* identify_string = (argc > 6) ? argv[6]: "test";

    recv_ip_addr = (argc > 7) ? argv[7]: "10.0.0.5";     // 2nd arg: alt dest ip addr;
    recv_ip_addr2 = (argc > 8) ? argv[8]: "10.0.0.8";
    recv_ip_addr3 = (argc > 9) ? argv[9]: "10.0.0.9";
    router_ip_addr = (argc > 10) ? argv[10]: "10.0.0.18"; // server IP address (dotted quad)

    in_port_t recv_port_start = (in_port_t) (argc > 12) ? strtoul(argv[12], NULL, 10) : 7000;        
    uint32_t conn_perthread = (uint32_t) (argc > 13) ? atoi(argv[13]): 4;

    const char filename_prefix[] = "/home/shw328/kvstore/log/";
    const char filename_prefix_tmp[] = "/tmp/";
    const char log[] = ".log";
    struct timespec ts1, ts2;

    //seed the rand() function
    srand(1); 

    if(is_direct_to_server)
        printf("client sends directly to server!\n");
    else
        printf("client requests are routed through BESS!\n");

    // memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
  	// servAddr.sin_family = AF_INET;          // IPv4 address family
    // servAddr.sin_port = htons(recvPort);    // Server port
    // servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface

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
    uint32_t expected_connections = (uint32_t) conn_perthread * num_threads_openloop + num_threads_closedloop;
    server_addr_array = (struct sockaddr_in *) malloc( expected_connections * sizeof(struct sockaddr_in) );

    pthread_t *openloop_threads;
    udp_thread_state *openloop_thread_state;

    pthread_t *closedloop_threads;
    udp_latency_state *closedloop_thread_state;

    if(pthread_barrier_init(&all_threads_ready, NULL, num_threads_openloop) == -1){
        perror("pthread_barrier_init fails");
        exit(1);
    }

    closedloop_threads = (pthread_t *)malloc( num_threads_closedloop * sizeof(pthread_t) );
    closedloop_thread_state = (udp_latency_state *)malloc( num_threads_closedloop * sizeof(udp_latency_state) );
    int server_index = 0;

    #ifdef OPEN_LOOP_ENABLE
    openloop_threads     = (pthread_t *)malloc( num_threads_openloop * sizeof(pthread_t) );
    openloop_thread_state = (udp_thread_state *)malloc( num_threads_openloop * sizeof(udp_thread_state) );

    printf("setting up open-loop connection\n");
    //setting up connection for each thread sequentially
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        openloop_thread_state[thread_id].conn_list = (udp_baseline_connection *)malloc( conn_perthread * sizeof(udp_baseline_connection));
        //set up a per-thread epoll fd for multiplexing recv on multiple connections
        if(CreateEpoll( &openloop_thread_state[thread_id].epstate, 1024) == -1){
           perror("per-thread epoll fd failed to be created\n");
        }

        uint32_t event_flag = EPOLLIN | EPOLLET;
        //int conn_fd;
        for(uint32_t conn_index = 0; conn_index < conn_perthread; conn_index++){
            openloop_thread_state[thread_id].conn_list[conn_index].fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if(openloop_thread_state[thread_id].conn_list[conn_index].fd == -1){
                printf("socket creation fails\n");
                exit(1);
            }

            struct sockaddr_in servAddr;
            memset(&servAddr, 0, sizeof(servAddr));
            servAddr.sin_family = AF_INET;

            if(is_direct_to_server)
                servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
            else // to the router
                servAddr.sin_addr.s_addr = inet_addr(router_ip_addr);

            servAddr.sin_port = htons(recv_port_start);
            server_addr_array[server_index] = servAddr;
            printf("socket:%d,port:%u\n", openloop_thread_state[thread_id].conn_list[conn_index].fd, recv_port_start);
            server_index++;
            recv_port_start++;

            openloop_thread_state[thread_id].conn_list[conn_index].server_addr = servAddr;
            openloop_thread_state[thread_id].conn_list[conn_index].tid = thread_id*2;
            //openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer = 0;
            memset(&openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer, 0, sizeof(alt_header));
            memset(&openloop_thread_state[thread_id].conn_list[conn_index].temp_recv_buffer, 0, sizeof(alt_header));
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.service_id = 1; //11;
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.alt_dst_ip = inet_addr(recv_ip_addr);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.alt_dst_ip2 = inet_addr(recv_ip_addr2);
            openloop_thread_state[thread_id].conn_list[conn_index].temp_send_buffer.alt_dst_ip3 = inet_addr(recv_ip_addr3);

            //add an event for each connection and assign u32 as the conn index
            AddEpollEventWithData(&openloop_thread_state[thread_id].epstate, openloop_thread_state[thread_id].conn_list[conn_index].fd,
                conn_index, event_flag);
        }

    }
    #else
        printf("no open-loop connections, closed-loop only\n");
    #endif

    //router address setup
	memset(&routerAddr, 0, sizeof(routerAddr)); // Zero out structure
  	routerAddr.sin_family = AF_INET;          // IPv4 address family
    routerAddr.sin_port = htons(recv_port_start);    // Server port
    routerAddr.sin_addr.s_addr = inet_addr(router_ip_addr); // an incoming interface
    routerAddrLen = sizeof(routerAddr);

    //closed-loop connection and thread setup
    printf("setting up closed-loop connection\n");
    //setting up 1 connection for each thread sequentially
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        // Closed-loop only
        closedloop_thread_state[thread_id].tid = thread_id*2; 
        // with open-loop
        //closedloop_thread_state[thread_id].tid = (num_threads_openloop + thread_id)*2; 
        //scale to two NUMA nodes //(num_threads_openloop + thread_id)*2;
        closedloop_thread_state[thread_id].fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        printf("thread id %" PRIu32 ", conn_fd:%d\n", closedloop_thread_state[thread_id].tid, closedloop_thread_state[thread_id].fd);

        memset(&closedloop_thread_state[thread_id].recv_header, 0, sizeof(alt_header));
        memset(&closedloop_thread_state[thread_id].send_header, 0, sizeof(alt_header));

        closedloop_thread_state[thread_id].send_header.service_id = 1;
        closedloop_thread_state[thread_id].send_header.request_id = 0;
        closedloop_thread_state[thread_id].send_header.options = 0;
        closedloop_thread_state[thread_id].send_header.alt_dst_ip = inet_addr(recv_ip_addr);
        closedloop_thread_state[thread_id].send_header.alt_dst_ip2 = inet_addr(recv_ip_addr2);
        closedloop_thread_state[thread_id].send_header.alt_dst_ip3 = inet_addr(recv_ip_addr3);

        closedloop_thread_state[thread_id].num_req = 0;
        closedloop_thread_state[thread_id].send_bytes = 0;
        closedloop_thread_state[thread_id].recv_bytes = 0;

        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;

        servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);

        if(is_direct_to_server)
            servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
        else // to the router
            servAddr.sin_addr.s_addr = inet_addr(router_ip_addr);

        servAddr.sin_port = htons(recv_port_start);
        server_addr_array[server_index] = servAddr;
        closedloop_thread_state[thread_id].server_addr = servAddr;
        printf("socket:%d,port:%u\n",  closedloop_thread_state[thread_id].fd, recv_port_start);
    }

    //closed-loop file setup
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        char logfilename[100];
        // for different number of threads
        // char thread_str[10];
        // snprintf(thread_str, sizeof(char)* 10, "%u", thread_id);
        // snprintf(logfilename, sizeof(filename_prefix) + sizeof(argv[3]) + sizeof(argv[5]) + sizeof(thread_str) +
        //     sizeof(identify_string) + sizeof(log) + 15, "%s%s_%sthd_%sconn_thd%s%s", filename_prefix, identify_string, argv[3], argv[5],
        //     thread_str, log);

        snprintf(logfilename, sizeof(filename_prefix) + sizeof(argv[4]) +  sizeof(argv[1]) + sizeof(identify_string) +
            sizeof(log) + 15, "%s%s_%s_%sthd%s", filename_prefix, identify_string, argv[1], argv[4], log);

        // e.g. openloop_2thd4conn_thd0_test1.log
        // open-loop setup with 2 open-loop threads and each has 4 connections
        // measured from closed-loop thread 0
        // marked as "test1" for this particular run
        printf("closed-loop thread %u logs to file %s\n", thread_id, logfilename);
        closedloop_thread_state[thread_id].output_fptr = fopen(logfilename,"w+");
    }

    clock_gettime(CLOCK_REALTIME, &ts_start);
    #ifdef OPEN_LOOP_ENABLE
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        //[UNTESTED]: timer_wheel
        if(init_timer_wheel(&openloop_thread_state[thread_id].timer_wheel, 5000) < 0){
            printf("timer_wheel init fails\n");
            exit(1);
        }
        openloop_thread_state[thread_id].timer_wheel.rto_interval = 2500;

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

    closed_loop_done = 0;
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        // [TEMP] we don't start closedloop_threads.
        pthread_create(&closedloop_threads[thread_id], NULL, closedloop_latency_measurement, &closedloop_thread_state[thread_id]);
    }

    printf("closedloop_thread:\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        // [TEMP] we don't join closedloop_threads.
        pthread_join(closedloop_threads[thread_id], NULL);
        printf("thread id %" PRIu32 ":", closedloop_thread_state[thread_id].tid);
        printf("req:%" PRIu32 ",", closedloop_thread_state[thread_id].num_req);
        printf("send:%" PRId64 ",", closedloop_thread_state[thread_id].send_bytes);
        printf("recv:%" PRId64 "\n", closedloop_thread_state[thread_id].recv_bytes);
    }

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
    printf("free closedloop_threads\n");
    free(closedloop_threads);

    printf("free closedloop_thread_state conn\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        fclose(closedloop_thread_state[thread_id].output_fptr);
    }

    #ifdef OPEN_LOOP_ENABLE
    printf("free openloop_thread_state conn list\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        free(openloop_thread_state[thread_id].conn_list);
    }

    printf("free openloop_thread_state\n");
    free(openloop_thread_state);
    #endif

    free(server_addr_array);
    free(closedloop_thread_state);
    free(poisson_arrival_pattern);

    return 0;
}
