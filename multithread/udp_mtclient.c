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

//#define OPEN_LOOP_ENABLE 1
#define MAX_NUM_REQ 10 //100*1000
#define MAX_NUM_SAMPLE 100*1000
//#define MAX_PENDING_REQ 20

typedef struct __attribute__((__packed__)) {
  uint16_t service_id;    // Type of Service.
  uint16_t request_id;    // Request identifier.
  uint16_t packet_id;     // Packet identifier.
  uint16_t options;       // Options (could be request length etc.).
  in_addr_t alt_dst_ip;
} alt_header;

typedef struct {
    //per thread state
    uint64_t* arrival_pattern;
    epollState epstate;
    uint32_t tid;
    uint32_t conn_perthread;
    uint32_t num_req;    
    int64_t send_bytes;
    int64_t recv_bytes;    
    int fd; // pseudo-connections share a socket
    alt_header send_header;
    alt_header recv_header;
    //each pseudo-connection has it own addr to sendto()
    struct sockaddr_in* server_addr_list;    
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
struct timespec ts_start;
int closed_loop_done;

// int SetTCPNoDelay(int sock, int flag){
//     if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1){
//         //perror("setsockopt TCP_NODELAY error\n");
//         return -1;
//     }
//     return 0;
// }

// void SetSocketLinger(int sock, int flag){
//     struct linger linger;
//     /* Close with RST not FIN */
//     linger.l_onoff = 1;
//     linger.l_linger = 0;
//     if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (void *)&linger, sizeof(linger))) {
//         perror("setsockopt SO_LINGER error");
//         exit(1);
//     }
// }

void* fake_mainloop(void *arg){
    int num = *(int*) arg;
    printf("fake mainloop:%d\n", num);
    return NULL;
}

// void* openloop_multiple_connections(void *arg){
//     printf("thread_openloop\n");    
//     thread_state* state = (thread_state*) arg;
//     tcp_connection* conn_list = state->conn_list;
//     int once = 0;

//     cpu_set_t cpuset;
//     pthread_t thread = pthread_self();

// 	CPU_ZERO(&cpuset);
// 	CPU_SET(state->tid, &cpuset);

// 	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
//         printf("pthread_setaffinity_np fails\n");
//     }

//     pthread_barrier_wait(&all_threads_ready);

//     //open loop send determined by poisson arrival 
//     int send_flag = MSG_DONTWAIT;
//     int recv_flag = MSG_DONTWAIT;
//     struct timespec ts1, ts2, ts3, ts4;

//     uint32_t send_conn_id = 0;
//     uint32_t complete_req_id = 0;
//     uint64_t interval, current_time;
//     uint64_t next_send = clock_gettime_us(&ts1);

//     ssize_t numBytes = 0;
//     while(!closed_loop_done){
//         current_time = clock_gettime_us(&ts2);
//         while( current_time >= next_send && !closed_loop_done){

//             uint32_t req_id = conn_list[send_conn_id].sent_req;
//             if(req_id > MAX_NUM_REQ){
//                 //printf("Having reqs > MAX_NUM_REQ, randomly assign an index\n");
//                 req_id = rand()%MAX_NUM_REQ;
//             }
//             interval = (uint64_t) state->arrival_pattern[req_id];
//             next_send = current_time + interval;

//             ssize_t send_byte_perloop = 0;
//             while(send_byte_perloop < 20){
//                 numBytes = send(conn_list[send_conn_id].fd, conn_list[send_conn_id].sendbuffer, 20, send_flag);
//                 if (numBytes < 0){
//                     if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
//                         continue;
//                     }
//                     else{ 
//                         printf("thread id %" PRIu32 "conn id %" PRIu32 "send() failed\n", state->tid, send_conn_id);
//                         exit(1);
//                     }
//                 }else{
//                     send_byte_perloop = send_byte_perloop + numBytes;
//                     state->send_bytes += numBytes;                    
//                 }
//             }
//             //printf("thread id %" PRIu32 ",conn id %" PRIu32 ",send %zd\n", state->tid, send_conn_id, numBytes);

//             //update indices
//             conn_list[send_conn_id].pending_req++;
//             conn_list[send_conn_id].sent_req++;
//             state->num_req++;            
//             send_conn_id = (send_conn_id + 1)%state->conn_perthread;

            
//             // updated current_time for the next loop
//             current_time = clock_gettime_us(&ts2); 

//             //printf("post send thread id %" PRIu32 " conn id %" PRIu32 "\ncurrent:%" PRIu64 ", interval:%" PRIu64 ", next_send:%" PRIu64 "\n", 
//             //    state->tid, send_conn_id, current_time, interval, next_send);
//         }

//         if(state->send_bytes == state->conn_perthread*MAX_NUM_REQ*20 && once == 0 ){
//             clock_gettime(CLOCK_REALTIME, &ts3);
//             uint64_t milliseconds = clock_gettime_diff_us(&ts_start,&ts3)/1000;
//             printf("total milliseconds:%" PRId64 "\n", milliseconds);
//             printf("send done on thread id%" PRIu32 "\n", state->tid);
//             once = 1;
//         }
//         //open loop recv
//         //epoll_wait()
//         //foreach event in events
//         //  identify conn index
//         //  recv on the correct socket

//         //printf("-");
//         int num_events = epoll_wait(state->epstate.epoll_fd, state->epstate.events, 10*state->conn_perthread, 0);
// 		for (int i = 0; i < num_events; i++) {
// 			uint32_t conn_index = state->epstate.events[i].data.u32;
//             //printf("\nprocess epoll_wait events, conn index %" PRIu32 "\n", conn_index);
//             //printf("events fd:%d, conn_list fd:%d\n", state->epstate.events[i].data.fd, conn_list[conn_index].fd);
//             ssize_t recv_byte_perloop = 0;
//             while(recv_byte_perloop < 20){
//                 //numBytes = recv(state->epstate.events[i].data.fd, state->recvbuffer, 20, recv_flag);                                        
//                 numBytes = recv(conn_list[conn_index].fd, conn_list[conn_index].recvbuffer, 20, recv_flag);                                        
//                 if (numBytes < 0){
//                     if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
//                         continue;
//                     }
//                     else{ 
//                         printf("thread id %" PRIu32 " recv() failed\n", state->tid);  
//                         //break;
//                         exit(1);
//                     }
//                 }else{
//                     recv_byte_perloop = recv_byte_perloop + numBytes;
//                     state->recv_bytes += numBytes;
//                     //printf("recv:%zd\n", numBytes);
//                 }                
//             }
//             //printf("thread id %" PRIu32 ",conn id %" PRIu32 ",recv %zd\n", state->tid, conn_index, numBytes);
//             //printf("*** thread id %" PRIu32 ", send:%" PRId64 ", recv:%" PRId64 "\n", state->tid, state->send_bytes ,state->recv_bytes);        
//             conn_list[conn_index].pending_req--;
//             state->num_req+=1;
//         }

//         //printf("*** thread id %" PRIu32 ", send:%" PRId64 ", recv:%" PRId64 "\n", state->tid, state->send_bytes ,state->recv_bytes);        
//     }
    
//     printf("thread id %" PRIu32 " is closing up connection\n", state->tid);
//     for(int conn_index = 0; conn_index < state->conn_perthread; conn_index++){  
//         if(conn_list[conn_index].is_open)
//             close(conn_list[conn_index].fd);
//     }
    
//     return NULL;
// }

void* closedloop_latency_measurement(void *arg){
    printf("closedloop_latency_measurement\n");    
    udp_latency_state* state = (udp_latency_state*) arg;
    struct timespec ts1, ts2;

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
        clock_gettime(CLOCK_REALTIME, &ts1);
        ssize_t send_byte_perloop = 0;
        
        while(send_byte_perloop < 20){
            if(is_direct_to_server)                
                numBytes = sendto(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) servAddrLen); 
            else
                numBytes = sendto(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &routerAddr, (socklen_t) routerAddrLen);

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
                //printf("thread id %" PRIu32 ",send:%zd, iter:%d\n", stats->tid, numBytes, i);
            }
        }

        //printf("thread id %" PRIu32 "before recv()\n", stats->tid);                               
        ssize_t recv_byte_perloop = 0;
        while(recv_byte_perloop < 20){
            numBytes = recvfrom(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t*) &servAddrLen);
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
        uint64_t duration = clock_gettime_diff_ns(&ts1,&ts2);
        fprintf(state->output_fptr,"%" PRIu64 "\n", duration);
        state->num_req+=1;
    }

    fflush(state->output_fptr);

    printf("thread id %" PRIu32 " closing up connection\n", state->tid);
    close(state->fd);

    closed_loop_done = 1;

    return NULL;
}

int main(int argc, char *argv[]) {
	char* router_ip_addr = (argc > 1) ? argv[1]: "10.0.0.18"; // server IP address (dotted quad)
    char* recv_ip_addr = (argc > 2) ? argv[2]: "10.0.0.8";     // 2nd arg: alt dest ip addr;
    in_port_t recv_port_start = (in_port_t) (argc > 3) ? strtoul(argv[3], NULL, 10) : 7000;
    uint32_t num_threads_openloop = (uint32_t) (argc > 4) ? atoi(argv[4]): 10;
    uint32_t num_threads_closedloop = (uint32_t) (argc > 5) ? atoi(argv[5]): 1;
    uint32_t conn_perthread = (uint32_t) (argc > 6) ? atoi(argv[6]): 4;
    char* identify_string = (argc > 6) ? argv[6]: "test";
    double rate = (argc > 7)? atof(argv[7]): 10000.0;
    is_direct_to_server = (argc > 8) ? atoi(argv[8]) : 1;
    const char filename_prefix[] = "/home/shw328/kvstore/log/";
    const char log[] = ".log";
    struct timespec ts1, ts2;
    
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
        // this allocation is not NUMA-waware!
        openloop_thread_state[thread_id].server_addr_list = (struct sockaddr_in *)malloc( conn_perthread * sizeof(struct sockaddr_in));
        int conn_fd;
        //set up a per-thread epoll fd for multiplexing recv on multiple connections
        if(CreateEpoll( &openloop_thread_state[thread_id].epstate, 1024) == -1){
           perror("per-thread epoll fd failed to be created\n");
        }
        uint32_t event_flag = EPOLLIN | EPOLLET;
        conn_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        openloop_thread_state[thread_id].fd = conn_fd;
        openloop_thread_state[thread_id].tid = thread_id;
        openloop_thread_state[thread_id].send_bytes = 0;
        openloop_thread_state[thread_id].recv_bytes = 0;
        openloop_thread_state[thread_id].num_req = 0;
        openloop_thread_state[thread_id].conn_perthread = conn_perthread;
        openloop_thread_state[thread_id].arrival_pattern = poisson_arrival_pattern;
        //TODO: init send/recv alt header
        memset(&openloop_thread_state[thread_id].recv_header, 0, sizeof(alt_header));
        memset(&openloop_thread_state[thread_id].send_header, 0, sizeof(alt_header));
        
        for(uint32_t conn_index = 0; conn_index < conn_perthread; conn_index++){                    
            printf("thread id %" PRIu32 ", conn_fd:%d\n", thread_id, conn_fd);

            struct sockaddr_in servAddr;
            memset(&servAddr, 0, sizeof(servAddr));
            servAddr.sin_family = AF_INET; 
            servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
            servAddr.sin_port = htons(recv_port_start); 
            server_addr_array[server_index] = servAddr;                        
            openloop_thread_state[thread_id].server_addr_list[conn_index] = servAddr;
            recv_port_start = recv_port_start + 1;
            server_index = server_index + 1;

            //add an event for each connection and assign u32 as the conn index
            //AddEpollEventWithData(&openloop_thread_state[thread_id].epstate, conn_fd, conn_index, event_flag);
            // not overwhlem connect operations
            clock_gettime(CLOCK_REALTIME, &ts1);
            realnanosleep(100*1000, &ts1, &ts2);  //sleep 100 usecs 
        }
    }
    #else
        printf("no open-loop connections, closed-loop only\n");
    #endif

    // router address setup
	memset(&routerAddr, 0, sizeof(routerAddr)); // Zero out structure
  	routerAddr.sin_family = AF_INET;          // IPv4 address family
    routerAddr.sin_port = htons(recv_port_start);    // Server port
    routerAddr.sin_addr.s_addr = inet_addr(router_ip_addr); // an incoming interface
    routerAddrLen = sizeof(routerAddr);
    
    //closed-loop connection and thread setup
    printf("setting up closed-loop connection\n");
    //setting up 1 connection for each thread sequentially 
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
        closedloop_thread_state[thread_id].tid = num_threads_openloop + thread_id;
        closedloop_thread_state[thread_id].fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        printf("thread id %" PRIu32 ", conn_fd:%d\n", closedloop_thread_state[thread_id].tid, closedloop_thread_state[thread_id].fd);

        memset(&closedloop_thread_state[thread_id].recv_header, 0, sizeof(alt_header));
        memset(&closedloop_thread_state[thread_id].send_header, 0, sizeof(alt_header));

        closedloop_thread_state[thread_id].num_req = 0;
        closedloop_thread_state[thread_id].send_bytes = 0;
        closedloop_thread_state[thread_id].recv_bytes = 0;

        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET; 
        servAddr.sin_addr.s_addr = inet_addr(recv_ip_addr);
        servAddr.sin_port = htons(recv_port_start); 
        server_addr_array[server_index] = servAddr;                        
        closedloop_thread_state[thread_id].server_addr = servAddr;
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

        snprintf(logfilename, sizeof(filename_prefix) + sizeof(argv[3]) + sizeof(argv[5]) +  sizeof(argv[7]) +
            sizeof(identify_string) + sizeof(log) + 15, "%s%s_%s_%sthd_%sconn%s", filename_prefix, identify_string, 
            argv[7], argv[3], argv[5], log);

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
        //pthread_create(&openloop_threads[thread_id], NULL, closedloop_multiple_connections, &openloop_thread_state[thread_id]);
        pthread_create(&openloop_threads[thread_id], NULL, openloop_multiple_connections, &openloop_thread_state[thread_id]);
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
        pthread_create(&closedloop_threads[thread_id], NULL, closedloop_latency_measurement, &closedloop_thread_state[thread_id]);
    }
    
    printf("closedloop_thread:\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_closedloop; thread_id++){
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
        printf("req:%" PRIu32 ",", openloop_thread_state[thread_id].num_req);
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
        free(openloop_thread_state[thread_id].server_addr_list);
    }

    printf("free openloop_thread_state\n");
    free(openloop_thread_state);
    #endif

    free(closedloop_thread_state);
    free(poisson_arrival_pattern);

    return 0;
}