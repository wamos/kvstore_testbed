#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "multi_dest_header.h"
#include "multi_dest_protocol.h"
#include "nanosleep.h"
#define ITERS 20

typedef struct {
    uint32_t tid;
    int fd;
    struct sockaddr_in server_addr;
    multi_dest_buffer* buf;
} udp_pseudo_connection;

typedef struct { //per thread state
    uint32_t tid;
    //FILE* output_fptr; // output latency numbers
    int fd; // pseudo-connections share a socket
    //alt_header send_header;
    //alt_header recv_header;
    uint32_t num_req;
    int64_t send_bytes;
    int64_t recv_bytes;
    struct sockaddr_in server_addr;
    rto_timer_event* timer_event;
} udp_latency_state;

int main(int argc, char *argv[]) {
    udp_latency_state* state = (udp_latency_state *)malloc(  1* sizeof(udp_latency_state) );
    //multi_dest_buffer send_buf;
    rto_timer_event timer_event;
    simple_timer_wheel timer_wheel;
    alt_header recv_header[10];
    struct timespec ts1, ts2, ts3, ts4, ts5, ts6, ts7, ts8;

    struct sockaddr_in server_addr; 
    char* recv_ip_addr = argv[1]; 
    in_port_t recv_port_start = (argc > 1) ? atoi(argv[2]) : 7000;      
    //char* recv_ip_addr = "10.0.0.8";
    char* recv_ip_addr2 = "10.0.0.9";

    memset(&server_addr, 0, sizeof(server_addr));            // Zero out structure
	server_addr.sin_family = AF_INET;                        // IPv4 address family
	server_addr.sin_addr.s_addr = inet_addr(recv_ip_addr);   // an incoming interface
	server_addr.sin_port = htons(recv_port_start);           // Local port
    int servAddrLen = sizeof(server_addr);

    int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (send_sock < 0){
		printf("socket() failed\n");
        exit(1);
    }

    //if( init_multi_dest_buf(&send_buf, 50) < 0){
    //    printf("buf_init fails\n");
    //    exit(1);
    //}

    state->fd  = send_sock;
    state->timer_event = &timer_event;
    state->server_addr = server_addr;

    if(init_timer_wheel(&timer_wheel, 500) < 0){
        printf("timer_wheel init fails\n");
        exit(1);
    }
    timer_wheel.rto_interval = 100;
    
    ssize_t numBytes = 0;
	for(uint32_t iter = 0; iter < ITERS; iter++){
		//api ref: ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        clock_gettime(CLOCK_REALTIME, &ts1);

        ssize_t send_bytes = 0;
        //rto_timer_event* timer_event = acquire_multi_dest_header(&state->buf);
        //timer_event->fd = send_sock;
        while(send_bytes < sizeof(alt_header)){
            state->timer_event->send_header.request_id = iter;
            numBytes = sendto(state->fd, (void*) &state->timer_event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) servAddrLen);

            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else{
                send_bytes = send_bytes + numBytes;
                printf("------------------\n");
                printf("send:%zd, reqid:%" PRIu32 "\n", numBytes, state->timer_event->send_header.request_id);
            }
        }
        clock_gettime(CLOCK_REALTIME, &ts2);        

        size_t recv_bytes = 0;
        uint8_t timeout_flag = 0;
        clock_gettime(CLOCK_REALTIME, &ts5);        
        while(recv_bytes < sizeof(alt_header)){
            numBytes = recvfrom(state->fd, (void*) &state->timer_event->recv_header, sizeof(alt_header), MSG_DONTWAIT, (struct sockaddr *) &state->server_addr, (socklen_t*) &servAddrLen);

            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    printf("recv EAGAIN\n");

                    clock_gettime(CLOCK_REALTIME, &ts6);
                    uint32_t diff_us = (uint32_t) clock_gettime_diff_us(&ts6, &ts5);
                    //printf("diff_us:%" PRIu32 ",rto:%" PRIu32 "\n", diff_us, timer_wheel.rto_interval);
                    if(diff_us > 100){
                        printf("RTO!\n");
                        timeout_flag = 1;
                        break;
                    }

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
                printf("recv:%zd, reqid:%" PRIu32 "\n", numBytes, state->timer_event->send_header.request_id);
            } 
        }
        clock_gettime(CLOCK_REALTIME, &ts3);

        if(timeout_flag){
            uint8_t retransmission_done = 0;            
            while(retransmission_done == 0){                
                send_bytes = 0;
                clock_gettime(CLOCK_REALTIME, &ts7);
                while(send_bytes < sizeof(alt_header)){
                    numBytes = sendto(state->fd, (void*) &state->timer_event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) servAddrLen);

                    if (numBytes < 0){
                        printf("send() failed\n");
                        exit(1);
                    }
                    else{
                        send_bytes = send_bytes + numBytes;
                        printf("------------------\n");
                        printf("send:%zd, reqid:%" PRIu32 "\n", numBytes, state->timer_event->send_header.request_id);
                    }
                }

                recv_bytes = 0;
                while(recv_bytes < sizeof(alt_header)){
                    clock_gettime(CLOCK_REALTIME, &ts8);
                    uint64_t diff_us = clock_gettime_diff_us(&ts8, &ts7);
                    if(diff_us > timer_wheel.rto_interval){
                        retransmission_done = 0;
                        break;
                    }
                    numBytes = recvfrom(state->fd, (void*) &state->timer_event->recv_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t*) &servAddrLen);

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
                        printf("recv:%zd, reqid:%" PRIu32 "\n", numBytes, state->timer_event->send_header.request_id);
                    } 
                }
                retransmission_done = 1;
            }
        } 
  
        clock_gettime(CLOCK_REALTIME, &ts4);
        //printf("timer_wheel[processed_index].tick %" PRIu64 "\n", timer_wheel.wheel[timer_wheel.processed_index].tick ); 

        if(ts1.tv_sec == ts4.tv_sec){
            //fprintf(fp, "%" PRIu64 "\n", ts4.tv_nsec - ts1.tv_nsec); 
            printf("latency:%" PRIu64 " ns\n\n", ts4.tv_nsec - ts1.tv_nsec); 
        }
        else{ 
            uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
            uint64_t ts4_nsec = ts4.tv_nsec + 1000000000*ts4.tv_sec;  
            //fprintf(fp, "%" PRIu64 "\n", ts4_nsec - ts1_nsec);
            printf("latency:%" PRIu64 " ns\n\n", ts4_nsec - ts1_nsec);
        } 
    }

    //free_multi_dest_buf(&send_buf);

    return 0;
}