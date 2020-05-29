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

#include <sys/time.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/tcp.h>

#include "nanosleep.h"
#include "epoll_state.h"
#include "dist_gen.h"

#define MAX_NUM_REQ 20
#define MAX_PENDING_REQ 5

typedef struct {
    uint32_t conn_id;
    int fd;    
    uint8_t is_open;
    uint32_t pending_req;
    char sendbuffer[20]; 
    char recvbuffer[20];   
    uint32_t tid;
    uint32_t sent_req; // request are sent
    int64_t send_bytes;
    int64_t recv_bytes;    
} tcp_connection;

typedef struct {
    //per thread state
    uint64_t* arrival_pattern;
    epollState epstate;
    uint32_t tid;
    uint32_t conn_perthread;
    //per thread aggregated state
    uint32_t num_req;    
    int64_t send_bytes;
    int64_t recv_bytes;
    tcp_connection* conn_list;    
} thread_state; // per thread stats

pthread_barrier_t all_threads_ready;

//static __thread tcp_connection* conn_list;
static struct sockaddr_in servAddr;

void* fake_mainloop(void *arg){
    int num = *(int*) arg;
    printf("fake mainloop:%d\n", num);
    return NULL;
}

void* thread_openloop(void *arg){
    printf("thread_openloop\n");    
    thread_state* state = (thread_state*) arg;
    tcp_connection* conn_list = state->conn_list;

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(state->tid, &cpuset);

	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    pthread_barrier_wait(&all_threads_ready);

    //open loop send determined by poisson arrival 
    int send_flag = MSG_DONTWAIT;
    int recv_flag = MSG_DONTWAIT;
    struct timespec ts1, ts2, ts3, ts4;

    uint32_t send_conn_id = 0;
    uint32_t complete_req_id = 0;
    uint64_t interval, current_time;
    uint64_t next_send = clock_gettime_us(&ts1);

    ssize_t numBytes = 0;
    while(state->recv_bytes < state->conn_perthread*(MAX_NUM_REQ)*20 ){        
    //while(state->send_bytes < state->conn_perthread*MAX_NUM_REQ*20){

        current_time = clock_gettime_us(&ts2);
        //printf("thread id %" PRIu32 "next_req_id %" PRIu32 "\n", state->tid, next_req_id);
        // We allow send time drift no more than 5 usecs
        while( current_time >= next_send && state->send_bytes < state->conn_perthread*MAX_NUM_REQ*20){

            uint32_t req_id = conn_list[send_conn_id].sent_req;
            if(req_id > MAX_NUM_REQ){
                printf("Having reqs > MAX_NUM_REQ, randomly assign an index\n");
                req_id = rand()%MAX_NUM_REQ;
            }
            interval = (uint64_t) state->arrival_pattern[req_id];
            next_send = current_time + interval;
            //printf("pre send thread id %" PRIu32 " conn id %" PRIu32 "\ncurrent:%" PRIu64 ", interval:%" PRIu64 ", next_send:%" PRIu64 "\n", 
                //state->tid, send_conn_id, current_time, interval, next_send);

            // max_pending_req is 5 for now. break the loop if we see max_pending_req on this connection
            // TODO: make it configureable
            if(conn_list[send_conn_id].pending_req > MAX_PENDING_REQ){
                printf("pending_req > MAX_PENDING_REQ, break loop\n"); 
                send_conn_id = 0;
                interval = (uint64_t) state->arrival_pattern[req_id];
                next_send = clock_gettime_us(&ts2) + interval;
                break;
            }

            ssize_t send_byte_perloop = 0;
            while(send_byte_perloop < 20){
                numBytes = send(conn_list[send_conn_id].fd, conn_list[send_conn_id].sendbuffer, 20, send_flag);
                if (numBytes < 0){
                    if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                        continue;
                    }
                    else{ 
                        printf("thread id %" PRIu32 "conn id %" PRIu32 "send() failed\n", state->tid, send_conn_id);
                        exit(1);
                    }
                }else{
                    send_byte_perloop = send_byte_perloop + numBytes;
                    state->send_bytes += numBytes;                    
                }
            }
            printf("thread id %" PRIu32 ",conn id %" PRIu32 ",send %zd\n", state->tid, send_conn_id, numBytes);

            //update indices
            conn_list[send_conn_id].pending_req++;
            conn_list[send_conn_id].sent_req++;
            state->num_req++;
            
            send_conn_id = (send_conn_id + 1)%state->conn_perthread;

            // every connection have sent at least once, need to do some recv
            /* next_conn_id = next_conn_id + 1;
            if(next_conn_id == state->conn_perthread){
                next_conn_id = 0;
                interval = (uint64_t) state->arrival_pattern[next_req_id];
                next_send = clock_gettime_us(&ts2) + interval;
                break;
            }*/
            
            // updated current_time for the next loop
            current_time = clock_gettime_us(&ts2); 

            //printf("post send thread id %" PRIu32 " conn id %" PRIu32 "\ncurrent:%" PRIu64 ", interval:%" PRIu64 ", next_send:%" PRIu64 "\n", 
            //    state->tid, send_conn_id, current_time, interval, next_send);
        }

        //open loop recv
        //epoll_wait()
        //foreach event in events
        //  identify conn index
        //  recv on the correct socket

        printf("-");
        int num_events = epoll_wait(state->epstate.epoll_fd, state->epstate.events, state->conn_perthread, 0);
		for (int i = 0; i < num_events; i++) {
			uint32_t conn_index = state->epstate.events[i].data.u32;
            printf("\nprocess epoll_wait events, conn index %" PRIu32 "\n", conn_index);
            printf("events fd:%d, conn_list fd:%d\n", state->epstate.events[i].data.fd, conn_list[conn_index].fd);
            ssize_t recv_byte_perloop = 0;
            while(recv_byte_perloop < 20){
                //numBytes = recv(state->epstate.events[i].data.fd, state->recvbuffer, 20, recv_flag);                                        
                numBytes = recv(conn_list[conn_index].fd, conn_list[conn_index].recvbuffer, 20, recv_flag);                                        
                if (numBytes < 0){
                    if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                        continue;
                    }
                    else{ 
                        printf("thread id %" PRIu32 " recv() failed\n", state->tid);  
                        break;
                        //exit(1);
                    }
                }else{
                    recv_byte_perloop = recv_byte_perloop + numBytes;
                    state->recv_bytes += numBytes;
                    //printf("recv:%zd\n", numBytes);
                }                
            }
            printf("thread id %" PRIu32 ",conn id %" PRIu32 ",recv %zd\n", state->tid, conn_index, numBytes);
            conn_list[conn_index].pending_req--;
            state->num_req+=1;
        }

        printf("*** thread id %" PRIu32 ", send:%" PRId64 ", recv:%" PRId64 "\n", state->tid, state->send_bytes ,state->recv_bytes);        
    }
    

    //TOOO: close connection when join back to the main thread?
    printf("thread id %" PRIu32 " is closing up connection\n", state->tid);
    for(int conn_index = 0; conn_index < state->conn_perthread; conn_index++){  
        if(conn_list[conn_index].is_open)
            close(conn_list[conn_index].fd);
    }
    
    return NULL;
}


void* thread_closedloop(void *arg){
    printf("thread_closedloop\n");    
    thread_state* stats = (thread_state*) arg;
    tcp_connection* conn_list = stats->conn_list;

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(stats->tid, &cpuset);

	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    //pthread_barrier_wait(&all_threads_ready);
    pthread_barrier_wait(&all_threads_ready);

    //do something with the connection
    int send_flag = MSG_DONTWAIT;
    int recv_flag = MSG_DONTWAIT;
    for(int conn_index = 0; conn_index < (int) stats->conn_perthread; conn_index++){  
        for(int i = 0; i < MAX_NUM_REQ; i++){
            ssize_t numBytes;
            if(conn_list[conn_index].is_open){
                ssize_t send_byte_perloop = 0;
                while(send_byte_perloop < 20){
                    numBytes = send(conn_list[conn_index].fd, conn_list[conn_index].sendbuffer, 20, send_flag);
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            continue;
                        }
                        else{ 
                            printf("thread id %" PRIu32 "send() failed\n", stats->tid);
                            exit(1);
                        }
                    }else{
                        send_byte_perloop = send_byte_perloop + numBytes;
                        stats->send_bytes += numBytes;
                        //printf("thread id %" PRIu32 ",send:%zd, iter:%d\n", stats->tid, numBytes, i);
                    }
                }

                //printf("thread id %" PRIu32 "before recv()\n", stats->tid);                               
                ssize_t recv_byte_perloop = 0;
                while(recv_byte_perloop < 20){
                    numBytes = recv(conn_list[conn_index].fd, conn_list[conn_index].recvbuffer, 20, recv_flag);                                        
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            continue;
                        }
                        else{ 
                            printf("thread id %" PRIu32 "recv() failed\n", stats->tid);  
                            exit(1);
                        }
                    }else{
                        recv_byte_perloop = recv_byte_perloop + numBytes;
                        stats->recv_bytes += numBytes;
                        //printf("recv:%zd\n", numBytes);
                    }                    
                }
                stats->num_req+=1;
            }
        }
    }

    //pthread_barrier_wait(&all_threads_ready);

    printf("thread id %" PRIu32 "closing up connection\n", stats->tid);
    for(int conn_index = 0; conn_index < stats->conn_perthread; conn_index++){  
        if(conn_list[conn_index].is_open)
            close(conn_list[conn_index].fd);
    }

    return NULL;
}

int main(int argc, char *argv[]) {

	char* recvIP = (argc > 1) ? argv[1]: "10.0.0.8"; // server IP address (dotted quad)
    in_port_t recvPort = (in_port_t) (argc > 2) ? atoi(argv[2]) : 6379;
    uint32_t num_threads_openloop = (uint32_t) (argc > 3) ? atoi(argv[3]): 10;
    uint32_t num_threads_closeloop = (uint32_t) (argc > 4) ? atoi(argv[4]): 1;
    uint32_t conn_perthread = (uint32_t) (argc > 5) ? atoi(argv[5]): 2;

	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
  	servAddr.sin_family = AF_INET;          // IPv4 address family
    servAddr.sin_port = htons(recvPort);    // Server port
    servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface

    pthread_t *openloop_threads;
    thread_state *openloop_thread_state;
    pthread_t *closedloop_threads;
    thread_state *closedloop_thread_state;
    //pthread_attr_t thread_attr;

    if(pthread_barrier_init(&all_threads_ready, NULL, num_threads_openloop) == -1){
        perror("pthread_barrier_init fails");
        exit(1);
    }

    openloop_threads     = (pthread_t *)malloc( num_threads_openloop * sizeof(pthread_t) );
    openloop_thread_state = (thread_state *)malloc( num_threads_openloop * sizeof(thread_state) );
    double* poisson_placeholder = (double *)malloc( MAX_NUM_REQ * sizeof(double) );
    uint64_t* poisson_arrival_pattern = (uint64_t *)malloc( MAX_NUM_REQ * sizeof(uint64_t) );

    // generate intervals of poisson inter arrival 
    double rate = 10000.0;
    GenPoissonArrival(rate, MAX_NUM_REQ, poisson_placeholder);
    for(int n = 0; n < MAX_NUM_REQ; n++) {
        poisson_arrival_pattern[n] = (uint64_t) round(poisson_placeholder[n]);
        printf("%" PRIu64 ", %.3lf\n", poisson_arrival_pattern[n], poisson_placeholder[n]);
    }
    free(poisson_placeholder);

    closedloop_threads = (pthread_t *)malloc( num_threads_closeloop * sizeof(pthread_t) );
    closedloop_thread_state = (thread_state *)malloc( num_threads_closeloop * sizeof(thread_state) );

    printf("setting up connection\n");
    //setting up connection for each thread sequentially 
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        // this allocation is not NUMA-waware!
        openloop_thread_state[thread_id].conn_list = (tcp_connection *)malloc( conn_perthread * sizeof(tcp_connection));
        int conn_fd;
        //set up a per-thread epoll fd for multiplexing recv on multiple connections
        if(CreateEpoll( &openloop_thread_state[thread_id].epstate, 1024) == -1){
           perror("per-thread epoll fd failed to be created\n");
        }
        uint32_t event_flag = EPOLLIN | EPOLLET;

        for(uint32_t conn_index = 0; conn_index < conn_perthread; conn_index++){        
            conn_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect(conn_fd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
                printf("connect() failed\n");
                openloop_thread_state[thread_id].conn_list[conn_index].is_open = 0;
                continue;
            }

            printf("thread id %" PRIu32 ", conn_fd:%d\n", thread_id, conn_fd);
            openloop_thread_state[thread_id].conn_list[conn_index].fd = conn_fd;
            openloop_thread_state[thread_id].conn_list[conn_index].conn_id = conn_index;
            openloop_thread_state[thread_id].conn_list[conn_index].is_open = 1;
            openloop_thread_state[thread_id].conn_list[conn_index].pending_req = 0;
            openloop_thread_state[thread_id].conn_list[conn_index].sent_req = 0;
            openloop_thread_state[thread_id].conn_list[conn_index].tid = thread_id;            
            memset(&openloop_thread_state[thread_id].conn_list[conn_index].sendbuffer, 1, 20);
            memset(&openloop_thread_state[thread_id].conn_list[conn_index].recvbuffer, 1, 20);

            //add an event for each connection and assign u32 as the conn index
            AddEpollEventWithData(&openloop_thread_state[thread_id].epstate, conn_fd, conn_index, event_flag);
        }
    }

    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        openloop_thread_state[thread_id].tid = thread_id;
        openloop_thread_state[thread_id].conn_perthread = conn_perthread;
        openloop_thread_state[thread_id].arrival_pattern = poisson_arrival_pattern;
        openloop_thread_state[thread_id].num_req = 0;
        openloop_thread_state[thread_id].send_bytes = 0;
        openloop_thread_state[thread_id].recv_bytes = 0;
        //pthread_create(&openloop_threads[thread_id], NULL, thread_closedloop, &openloop_thread_state[thread_id]);
        pthread_create(&openloop_threads[thread_id], NULL, thread_openloop, &openloop_thread_state[thread_id]);
        //pthread_create(&openloop_threads[i], NULL, fake_mainloop, (void *) &i);
    }

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
    free(closedloop_threads);

    printf("free openloop_thread_state conn list\n");
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        free(openloop_thread_state[thread_id].conn_list);
    }

    printf("free openloop_thread_state\n");
    free(openloop_thread_state);
    free(openloop_thread_state);

    free(poisson_arrival_pattern);

    /*char sendbuffer[20];
	memset(&sendbuffer, 1, sizeof(sendbuffer));
    char recvbuffer[20];
    memset(&recvbuffer, 1, sizeof(recvbuffer));

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

  	// Create a reliable, stream socket using TCP
	int send_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

  	// Construct the server address structure
	struct sockaddr_in servAddr;            // Server address
	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
  	servAddr.sin_family = AF_INET;          // IPv4 address family
    servAddr.sin_port = htons(recvPort);    // Server port
    servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface

	if (connect(send_sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		printf("connect() failed\n");
        exit(1);
    }

    //sleep(5);

    for(int i = 0; i < 5; i++){
        ssize_t numBytes = send(send_sock, sendbuffer, 20, 0);
        if (numBytes < 0){
            printf("send() failed\n");
            exit(1);
        }else{
            printf("send:%zd\n", numBytes);
        }

        numBytes = recv(send_sock, recvbuffer, 20, 0);
        if (numBytes < 0){
            printf("recv() failed\n");
            exit(1);
        }else{
            printf("recv:%zd\n", numBytes);
        }
    }


	close(send_sock);*/
    //fclose(fp);

  	exit(0);
}