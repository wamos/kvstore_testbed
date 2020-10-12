#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include "socket_operation.h"

int closed_loop_done;
int fake_load_sequence_yeti08[10] = {10, 10, 10, 10, 10, 10, 10, 10, 10};
int fake_load_sequence_yeti09[10] = { 5,  5,  5,  5,  5, 15, 15, 15, 15};
int server_09; 
int server_08;
int recv_req_count;

void* feedback_mainloop(void *arg){
    feedback_thread_state* state = (feedback_thread_state*) arg;
    printf("feedback mainloop\n");
    struct timespec ts1, ts2;
    ssize_t numBytes = 0;
    int load_index = 0;
    int probe_counter = 0;

    // if(server_08)
    //     probe_counter = 0;
    // else if(server_09)
    //     probe_counter = -1;
    // else
    //     printf("WTF\n");

    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(state->tid, &cpuset);
	if(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1){
        printf("pthread_setaffinity_np fails\n");
    }

    int routerAddrLen = sizeof(state->server_addr);
    clock_gettime(CLOCK_REALTIME, &ts1);
    while(!closed_loop_done){    
        //printf("while loop\n");
        //printf("fb: recv_req_count:%d\n", recv_req_count);

        clock_gettime(CLOCK_REALTIME, &ts2);
        uint64_t diff_us;
        if(ts1.tv_sec == ts2.tv_sec){
            diff_us = (ts2.tv_nsec - ts1.tv_nsec)/1000; 
        }
        else{ 
            uint64_t ts1_nsec = ts1.tv_nsec + 1000000000*ts1.tv_sec;
            uint64_t ts2_nsec = ts2.tv_nsec + 1000000000*ts2.tv_sec;                    
            diff_us = (ts2_nsec - ts1_nsec)/1000;
        }

        if(diff_us > state->feedback_period_us){ // 3 seconds
        //if(recv_req_count%10 == 0 && recv_req_count > 0){
            printf("probe packets\n");
            probe_counter+=2;
            //set_alt_header_msgtype(&state->send_header, HOST_FEEDBACK_MSG);
            state->send_header.msgtype_flags = HOST_FEEDBACK_MSG;
            state->send_header.service_id = 1;
            state->send_header.feedback_options = probe_counter; //fake_load_sequence_yeti09[load_index];
            state->send_header.alt_dst_ip  = inet_addr("10.0.0.4");
            state->send_header.alt_dst_ip2 = inet_addr("10.0.0.4");
            state->send_header.alt_dst_ip3 = inet_addr("10.0.0.4");
            numBytes = sendto(state->fd, (void*) &state->send_header, sizeof(alt_header), 0, (struct sockaddr *) &state->server_addr, (socklen_t) routerAddrLen);
            state->feedback_counter++;
            printf("feedback_counter:%" PRIu32 "\n", state->feedback_counter);
            clock_gettime(CLOCK_REALTIME, &ts1);
            //recv_req_count = 0;
        }        
    }

    return NULL;
}

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

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    char* routerIP = argv[2];     // 2nd arg: alt dest ip addr;
    in_port_t recvPort = (argc > 2) ? atoi(argv[3]) : 6379;

    closed_loop_done = 0;
    recv_req_count = 0;
    int len = sizeof(recvIP);

    // if(recvIP[len-1] == '9'){
    //     printf("it's 10.0.0.9\n");
    //     server_09 = 1;
    //     server_08 = 0;
    // }
    // else if (recvIP[len-1] == '8'){
    //     printf("it's 10.0.0.8\n");
    //     server_08 = 1;
    //     server_09 = 0;
    // }
    // else{
    //     printf("nope\n");
    //     server_08 = 0;
    //     server_09 = 0;
    // }

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

	int send_sock, listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

	// Construct local address structure
	struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    int servAddrLen = sizeof(servAddr);

    struct sockaddr_in routerAddr;                  // Local address
	memset(&routerAddr, 0, sizeof(routerAddr));       // Zero out structure
	routerAddr.sin_family = AF_INET;                // IPv4 address family
	routerAddr.sin_addr.s_addr = inet_addr(routerIP); // an incoming interface
	routerAddr.sin_port = htons(recvPort);          // Local port
    int routerAddrLen = sizeof(routerAddr);
    
    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
    int clntAddrLen = sizeof(clntAddr);

    UDPSocketSetup(listen_sock, servAddr);

    pthread_t *feedback_thread;
    feedback_thread_state* fbk_state;
    feedback_thread = (pthread_t *)malloc( 1 * sizeof(pthread_t) );
    fbk_state = (feedback_thread_state *)malloc( 1 * sizeof(feedback_thread_state) );

    // Init feedback_thread_state fbk_state
    fbk_state->tid = 0;
    fbk_state->feedback_period_us = 3*1000*1000; // 1000 microseconds period for feedback
    fbk_state->feedback_counter = 0;

    if ((fbk_state->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

    fbk_state->server_addr = routerAddr;    
    memset(&fbk_state->recv_header, 0, sizeof(alt_header));
    memset(&fbk_state->send_header, 0, sizeof(alt_header));

    //pthread_create(feedback_thread, NULL, feedback_mainloop, fbk_state);
    
    alt_header Alt;
    memset(&Alt, 0, sizeof(alt_header));
    alt_header alt_response;
    memset(&alt_response, 0, sizeof(alt_header));
    ssize_t numBytesRcvd = 0;
    
	//while(numBytesRcvd < sizeof(alt_header)) {
    while(1){
        //printf("enter while loop\n");

        ssize_t recv_bytes = 0;
        while(recv_bytes < sizeof(alt_header)){
            //printf("enter recv loop\n");
            ssize_t numBytes = recvfrom(listen_sock, (void*)&Alt, sizeof(Alt), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
            if (numBytes < 0){
                if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    printf("recv() EAGAIN\n");
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
                // char clntName[INET_ADDRSTRLEN]; // String to contain client address
                // if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntName, sizeof(clntName)) != NULL)
                //     printf("Handling client %s/ %d\n", clntName, ntohs(clntAddr.sin_port));
                // else
                //     printf("Unable to get client address\n");
                
                recv_bytes = recv_bytes +  numBytes;
                numBytesRcvd = numBytesRcvd + numBytes;
                recv_req_count++;
                //printf("recv_req_count:%d\n", recv_req_count);
                //printf("recv:%zd\n", numBytes);
            }
        }

        clock_gettime(CLOCK_REALTIME, &ts1);

        // clock_gettime(CLOCK_REALTIME, &ts1);
        // sleep_ts1=ts1;
        // realnanosleep(500*1000*1000, &sleep_ts1, &sleep_ts2); // 0.5 second

        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            //ssize_t numBytes = sendto(send_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            alt_response.msgtype_flags = SINGLE_PKT_RESP_PASSTHROUGH;
            alt_response.service_id = 1;
            alt_response.request_id = 0;
            alt_response.feedback_options = recv_req_count;
            //alt_response.alt_dst_ip = inet_addr("10.0.0.5");
            alt_response.alt_dst_ip = clntAddr.sin_addr.s_addr;
            routerAddr.sin_port = clntAddr.sin_port; // set to the right port

            // struct in_addr dest_addr;
            // dest_addr.s_addr = alt_response.alt_dst_ip;
            // char* dest_ip =inet_ntoa(dest_addr);
            //printf("alt_dst_ip:%s\n", dest_ip);

            ssize_t numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &routerAddr, sizeof(routerAddr));
            //ssize_t numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else if (numBytes == 0){
                printf("send no bytes\n");
            }
            else{
                send_bytes = send_bytes + numBytes;
                //printf("send:%zd\n", numBytes);
            }
        }

	}
    closed_loop_done = 1;

	printf("closing sockets then\n");
    close(fbk_state->fd);
    close(listen_sock);

    pthread_join(*feedback_thread, NULL);
    free(feedback_thread);
    free(fbk_state);    
    //close(send_sock);

}