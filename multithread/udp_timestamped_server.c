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
#include "timestamping.h"
#include "epoll_state.h"
#include "multi_dest_header.h"

//#define HARDWARE_TIMESTAMPING_ENABLE 1
static const int MAXPENDING = 20; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;
int closed_loop_done;
int queued_events;
uint64_t pkt_counter;

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
    char* expname = (argc > 4) ? argv[4]: "timestamp_server";
    //double rate = (argc > 5)? atof(argv[5]): 10000.0;
    struct timespec ts1, ts2, sleep_ts1, sleep_ts2, ts3, ts4;
    char* clientIP = "172.31.34.51"; //argv[1];
    closed_loop_done = 0;
    queued_events = 0;

    #ifdef HARDWARE_TIMESTAMPING_ENABLE
    char* interface_name = "eth1";
    /*if(enable_nic_timestamping(interface_name) < 0){
        printf("NIC timestamping can't be enabled\n");
    }*/
    #endif

    ssize_t numBytesRcvd;
    ssize_t numBytesSend;
    int conn_count = 1;
    int setsize = 10240;
    int* fd_array = (int *) malloc(1024 * sizeof(int) );
    int fd_tail = 0; //int fd_head = 0;
    
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
    struct sockaddr_in* server_addr_array;
    server_addr_array = (struct sockaddr_in *) malloc( expected_connections * sizeof(struct sockaddr_in) );

    for(int server_index = 0; server_index < expected_connections; server_index++){
        printf("socket creation,");
        if ((udp_socket_array[server_index] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		    perror("socket() failed\n");
            exit(1);
        }

        if(server_index == 0){
            if(sock_enable_timestamping(udp_socket_array[server_index]) < 0){
                printf("socket %d timestamping can't be enabled\n", udp_socket_array[server_index]); 
            }
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

    //pthread_create(feedback_thread, NULL, feedback_mainloop, fbk_state);


    //struct sockaddr_in clntAddr; // Client address
    //memset(&clntAddr, 0, sizeof(clntAddr));
    //int clntAddrLen = sizeof(clntAddr);

    struct timespec send_ts, recv_ts;
    uint32_t opt_id = 0;
    struct timestamp_info send_info = {send_ts, opt_id};
    struct timestamp_info recv_info = {recv_ts, opt_id};

    struct sockaddr_in clntAddr;                  // Local address
    memset(&clntAddr, 0, sizeof(clntAddr));       // Zero out structure
    //clntAddr.sin_family = AF_INET;                // IPv4 address family
    //clntAddr.sin_addr.s_addr = inet_addr(clientIP); // an incoming interface
    //clntAddr.sin_port = htons(recv_port_start);          // Local port
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
    int max_retries = 5;
    pkt_counter=0;
    uint64_t log_counter=0;
    int once = 0;    

    char logfilename[100];
    const char log[] = ".log";
    const char filename_prefix[] = "/tmp/";

    snprintf(logfilename, sizeof(filename_prefix) + sizeof(log) + 30, "%s%s%s", filename_prefix, expname, log);
    //FILE* output_fptr = fopen(logfilename, "w+");
    //if(output_fptr == NULL){
    //    printf("FILE* isn't good\n");
    //    fclose(output_fptr);
    //    exit(0);
    //}
    //fprintf(output_fptr, "file opened!\n");
    //fflush(output_fptr);

    #ifdef HARDWARE_TIMESTAMPING_ENABLE	
    struct iovec tx_iovec;
    tx_iovec.iov_base = (void*) &alt_send_header;
    tx_iovec.iov_len = sizeof(alt_header);

    struct msghdr tx_hdr = {0};
    tx_hdr.msg_name = (void*) &clntAddr;
    tx_hdr.msg_namelen =  (socklen_t) clntAddrLen;
    tx_hdr.msg_iov = &tx_iovec;
    tx_hdr.msg_iovlen = 1;

    char recv_control[CONTROL_LEN] = {0};
    struct iovec rx_iovec;
    rx_iovec.iov_base = (void*) &alt_recv_header;
    rx_iovec.iov_len = sizeof(alt_header);
    
    struct msghdr rx_hdr = {0};
    rx_hdr.msg_name = (void*) &clntAddr;
    rx_hdr.msg_namelen =  (socklen_t) clntAddrLen;
    rx_hdr.msg_iov = &rx_iovec;
    rx_hdr.msg_iovlen = 1;
    rx_hdr.msg_control = recv_control;
	rx_hdr.msg_controllen = CONTROL_LEN;
   #endif

    uint32_t last_optid = 0;
    int outoforder_timestamp = 0;
    uint32_t req_counter = 0;
    while(1){
    //while(!closed_loop_done){
        //printf("epoll_wait: waiting for connections\n");
        //printf("accepted connections: %d\n", accept_connections);
        int num_events = epoll_wait(epstate.epoll_fd, epstate.events, setsize, -1);
        clock_gettime(CLOCK_REALTIME, &ts1);
        if(num_events == -1){
            perror("epoll_wait");
            exit(1);
        }

        //int client_fd = 0;    
        if (num_events == 1) {
            for (int j = 0; j < num_events; j++) {
                struct epoll_event *e = epstate.events+j;

                ssize_t numBytes = 0;
                ssize_t recv_byte_perloop = 0;
                ssize_t send_byte_perloop = 0;
                int recv_retries = 0; 
                int send_retries = 0;                    
                while(recv_byte_perloop < sizeof(alt_header)){
		    #ifdef HARDWARE_TIMESTAMPING_ENABLE
            	    numBytes = recvmsg(e->data.fd, &rx_hdr, 0);
            	    #else
		    numBytes = recvfrom(e->data.fd, (void*)&alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
            	    #endif 

                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){                            
                               continue; 
                        }
                        else{
                            printf("recvfrom failed on fd:%d\n", e->data.fd); 
                            send_byte_perloop = 20;
                            break;
                        }
                    }
                    else if (numBytes == 0){ //&& recv_byte_perloop == 20){
                        if(recv_byte_perloop == 20){
                            break;
                        }
                        else{
                            recv_retries++;
                            printf("recv 0 byte on fd:%d\n", e->data.fd);
                            if(recv_retries == max_retries){
                                send_byte_perloop = 20; //force it not entering send-loop
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
	            #ifdef HARDWARE_TIMESTAMPING_ENABLE
                    numBytes= sendmsg(e->data.fd, &tx_hdr, 0);
		    #else
		    numBytes = sendto(e->data.fd, (void*) &alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr)); 
		    #endif
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
                        else{
                            printf("send 0 byte on fd:%d\n", e->data.fd);
                            /*send_retries++;
                            printf("send 0 byte on fd:%d\n", e->data.fd);
                            if(send_retries == max_retries){
                                break;
                            }
                            else{
                                continue;
                            }*/
                        }
                    }
                    else{
                        send_byte_perloop = send_byte_perloop + numBytes;
                        total_send_bytes = total_send_bytes + numBytes;
                        //printf("send:%zd on fd %d\n", numBytes, e->data.fd);                
                    }
                }
            }
            clock_gettime(CLOCK_REALTIME, &ts2);

            //clock_gettime(CLOCK_REALTIME, &ts3);

	    #ifdef HARDWARE_TIMESTAMPING_ENABLE
            // Handle udp_get_rx_timestamp failures
            // we can make it a while loop to get the proper rx_timestamp
            int print_rx_counter = 0;
            while(udp_get_rx_timestamp(&rx_hdr, &recv_info) < 0){
                continue;
                if(print_rx_counter){
                     printf("+");
                }
                else{
                     printf("extracting rx timestamp fails");   
                     print_rx_counter++;         
                }
            }
            if(print_rx_counter)
                 printf("\n");     

            int print_tx_counter = 0;            
            while(udp_get_tx_timestamp(udp_socket_array[0], &send_info) < 0){
                continue;
               	if(print_tx_counter){
                     printf("+");
                }
                else{
                     printf("extracting tx timestamp fails at req %u", req_counter);
                     print_tx_counter++;
                }
            }
            if(print_tx_counter)
                printf("\n");

	    uint64_t app_latency = clock_gettime_diff_ns(&ts1, &ts2);
            uint64_t host_latency = clock_gettime_diff_ns(&recv_info.time, &send_info.time);
	     if(host_latency < app_latency){
                //fprintf(output_fptr, "%" PRIu64 ",%" PRIu64 "\n", host_latency, app_latency);
                printf("\n--------------\n host_latency < app_latency \n--------------\n");
                printf("app_latency:%" PRIu64 "\n", app_latency);
                printf("host_latency:%" PRIu64 "\n", host_latency);
            }
            else{
                fprintf(output_fptr, "%" PRIu64 ",%" PRIu64 "\n", host_latency, app_latency);
            }
            #endif

            //uint64_t app_latency2 = clock_gettime_diff_ns(&ts1, &ts2);
	    //printf("app_latency:%" PRIu64 "\n", app_latency2);
            //fprintf(output_fptr, "%" PRIu64 "\n", app_latency2);

            //clock_gettime(CLOCK_REALTIME, &ts4); 
            //uint64_t log_latency = clock_gettime_diff_ns(&ts3, &ts4);
            //printf("log_latency:%" PRIu64 "\n", log_latency);
        }
        else if(num_events == 0){
            printf("no event!\n");
        }
        else{
            printf("not closed-loop right!\n");
        }
        req_counter++;        
        //fflush(output_fptr);
    }    
    //pthread_join(*feedback_thread, NULL);

	printf("closing sockets then\n");
    for(int server_index = 0; server_index < expected_connections; server_index++){
        close(udp_socket_array[server_index]);
    }
    free(server_addr_array);
    free(udp_socket_array);
    free(fd_array);
}
