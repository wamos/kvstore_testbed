#define _GNU_SOURCE
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

typedef struct {
    uint32_t conn_id;
    int fd;    
    uint8_t is_open;
    uint32_t pending_req;
    char sendbuffer[20];
    char recvbuffer[20];

    uint32_t tid;
    uint32_t num_req;
    int64_t send_bytes;
    int64_t recv_bytes;    
} tcp_connection;

typedef struct {
    uint32_t tid;
    uint32_t num_req;
    uint32_t conn_perthread;
    int64_t send_bytes;
    int64_t recv_bytes;
    tcp_connection* conn_list;    
} thread_stats; // per thread stats

pthread_barrier_t all_threads_ready;

//static __thread tcp_connection* conn_list;
static struct sockaddr_in servAddr;

void* fake_mainloop(void *arg){
    int num = *(int*) arg;
    printf("fake mainloop:%d\n", num);
    return NULL;
}

void* thread_mainloop(void *arg){
    printf("thread_mainloop\n");    
    thread_stats* stats = (thread_stats*) arg;
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
    for(int conn_index = 0; conn_index < stats->conn_perthread; conn_index++){  
        for(int i = 0; i < 1000; i++){
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
                    stats->num_req+=1;
                }
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
    in_port_t recvPort = (argc > 2) ? atoi(argv[2]) : 6379;
    unsigned int num_threads_openloop = (argc > 3) ? strtol(argv[3], NULL, 10): 2;
    unsigned int num_threads_closeloop = (argc > 4) ? strtol(argv[4], NULL, 10): 1;
    unsigned int conn_perthread = (argc > 5) ? strtol(argv[5], NULL, 10): 1;

	memset(&servAddr, 0, sizeof(servAddr)); // Zero out structure
  	servAddr.sin_family = AF_INET;          // IPv4 address family
    servAddr.sin_port = htons(recvPort);    // Server port
    servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface

    pthread_t *openloop_threads;
    thread_stats *openloop_thread_stats;
    //pthread_t *closeloop_threads;
    //thread_stats *closeloop_thread_parm;
    pthread_attr_t thread_attr;

    if(pthread_barrier_init(&all_threads_ready, NULL, num_threads_openloop) == -1){
        perror("pthread_barrier_init fails");
        exit(1);
    }

    openloop_threads     = (pthread_t *)malloc( num_threads_openloop * sizeof(pthread_t) );
    openloop_thread_stats = (thread_stats *)malloc( num_threads_openloop * sizeof(thread_stats) );

    //closeloop_threads = (pthread_t *)malloc( num_threads_closeloop * sizeof(pthread_t) );
    //closeloop_thread_parm = (parm *)malloc( num_threads_closeloop * sizeof(parm) );

    printf("setting up connection\n");
    //setting up connection
    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        openloop_thread_stats[thread_id].conn_list = (tcp_connection *)malloc( conn_perthread * sizeof(tcp_connection));
        int conn_fd;
        for(int conn_index = 0; conn_index < conn_perthread; conn_index++){        
            conn_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect(conn_fd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
                printf("connect() failed\n");
                openloop_thread_stats[thread_id].conn_list[conn_index].is_open = 0;
                continue;
            }

            printf("thread id %" PRIu32 ", conn_fd:%d\n", thread_id, conn_fd);
            openloop_thread_stats[thread_id].conn_list[conn_index].fd = conn_fd;
            openloop_thread_stats[thread_id].conn_list[conn_index].conn_id = conn_index;
            openloop_thread_stats[thread_id].conn_list[conn_index].is_open = 1;
            openloop_thread_stats[thread_id].conn_list[conn_index].pending_req = 0;
            openloop_thread_stats[thread_id].conn_list[conn_index].tid = thread_id;
            memset(&openloop_thread_stats[thread_id].conn_list[conn_index].sendbuffer, 1, 20);
            memset(&openloop_thread_stats[thread_id].conn_list[conn_index].recvbuffer, 1, 20);
        }
    }

    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        openloop_thread_stats[thread_id].tid = thread_id;
        openloop_thread_stats[thread_id].conn_perthread = conn_perthread;
        openloop_thread_stats[thread_id].num_req = 0;
        openloop_thread_stats[thread_id].send_bytes = 0;
        openloop_thread_stats[thread_id].recv_bytes = 0;
        pthread_create(&openloop_threads[thread_id], NULL, thread_mainloop, &openloop_thread_stats[thread_id]);
        //pthread_create(&openloop_threads[i], NULL, fake_mainloop, (void *) &i);
    }

    for (uint32_t i=0; i< num_threads_openloop; i++){
        pthread_join(openloop_threads[i], NULL);
        printf("thread id %" PRIu32 ":", openloop_thread_stats[i].tid);
        printf("req:%" PRIu32 ",", openloop_thread_stats[i].num_req);
        printf("send:%" PRId64 ",", openloop_thread_stats[i].send_bytes);
        printf("recv:%" PRId64 "\n", openloop_thread_stats[i].recv_bytes);        
    }

    free(openloop_threads);

    for (uint32_t thread_id = 0; thread_id < num_threads_openloop; thread_id++){
        free(openloop_thread_stats[thread_id].conn_list);
    }
    free(openloop_thread_stats);

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