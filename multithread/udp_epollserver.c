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
#include "nanosleep.h"
#include "epoll_state.h"

static const int MAXPENDING = 20; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;

typedef struct __attribute__((__packed__)) {
  uint16_t service_id;    // Type of Service.
  uint16_t request_id;    // Request identifier.
  uint16_t packet_id;     // Packet identifier.
  uint16_t options;       // Options (could be request length etc.).
  in_addr_t alt_dst_ip;
} alt_header;

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

int isFoundInArray(int* fd_array, uint32_t array_length, int fd){
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
    uint32_t expected_connections = (argc > 3) ? atoi(argv[3])*4+1: 20; // pseudo-connection for UDP
    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

    ssize_t numBytesRcvd;
    ssize_t numBytesSend;
    int conn_count = 1;
    int setsize = 10240;
    int fd_array[1024];
    uint32_t fd_tail = 0; //int fd_head = 0;
    memset(&fd_array, -1, sizeof(fd_array));
    
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
        recv_port_start++; //ecv_port_start + 1;

        if(UDPSocketSetup(udp_socket_array[server_index], servAddr) == -1){
            perror("UDPSocketSetup error\n"); 
            exit(1);
        }
    }
    
    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));
    int clntAddrLen = sizeof(clntAddr);

    alt_header alt_recv_header;
    memset(&alt_recv_header, 0, sizeof(alt_header));
    alt_header alt_send_header;
    memset(&alt_send_header, 0, sizeof(alt_header));

    alt_send_header.service_id = 1;
    alt_send_header.request_id = 0;
    alt_send_header.packet_id = 0;
    alt_send_header.options = 10;    

    for(int server_index = 0; server_index < expected_connections; server_index++){
        if(AddEpollEvent(&epstate, udp_socket_array[server_index], event_flag)== -1){
            perror("listen_sock cannot add EPOLLIN | EPOLLET\n");
            exit(1);
        }
    }

    int total_events = 0;
    int accept_connections = 0;
    int max_retries = 5;

    while(1){
        //printf("epoll_wait: waiting for connections\n");
        //printf("accepted connections: %d\n", accept_connections);
        int num_events = epoll_wait(epstate.epoll_fd, epstate.events, setsize, -1);
        if(num_events == -1){
            perror("epoll_wait");
            exit(1);
        }

        if (num_events > 0) {
            //printf("num_events:%d\n", num_events);
            for (int j = 0; j < num_events; j++) {
                struct epoll_event *e = epstate.events+j;
                //printf("epoll_event->data.fd:%d\n", e->data.fd);
                if( e->events == EPOLLHUP){
                    perror("\n-------\nevent:EPOLLHUP\n--------\n");
                    continue;
                }
                else if( e->events == EPOLLERR){
                    perror("\n-------\nevent:EPOLLERR\n--------\n");
                    continue;
                }
                else if( e->events == EPOLLRDHUP){
                    perror("\n-------\nevent:EPOLLERR\n--------\n");
                    continue;
                }

                // if e->data.fd not in fd_array
                //    add it to fd_array;
                //    print a new connection;
                //    accept_connections++;
                // if e->data.fd is in udp_socket_array
                //    do processing of whatever 

                if (isFoundInArray( &fd_array , fd_tail, e->data.fd) == -1){          
                    printf("Accept connections\n");
                    printf("incoming_sock_fd:%d\n", e->data.fd);
                    fd_array[fd_tail] = e->data.fd;
                    fd_tail = fd_tail + 1;
                    accept_connections++;
                    printf("accept fds: %d, expected fds: %d\n", accept_connections, expected_connections);
                }

                int sock_index = isFoundInArray(udp_socket_array, expected_connections, e->data.fd);

                if(sock_index == -1){
                    printf("unexpected fd caught by epoll!\n");
                    exit(1);
                }

                ssize_t numBytes = 0;
                ssize_t recv_byte_perloop = 0;
                ssize_t send_byte_perloop = 0;
                int recv_retries = 0; 
                int send_retries = 0;                    
                while(recv_byte_perloop < 20){
                    numBytes = recvfrom(udp_socket_array[sock_index], (void*)&alt_recv_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, (socklen_t *) &clntAddrLen);
                    //numBytes = recv(e->data.fd, recv_buffer, 20, MSG_DONTWAIT);
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            continue;
                        }
                        else{
                            printf("recv() failed on fd:%d\n", e->data.fd); 
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
                        //printf("recv:%zd on fd %d\n", numBytes, e->data.fd);
                    }
                    }

                    //clock_gettime(CLOCK_REALTIME, &ts1);
                    //sleep_ts1=ts1;
                    //realnanosleep(10*1000, &sleep_ts1, &sleep_ts2); // processing time 10 us

                while(send_byte_perloop < 20){
                    //numBytes = send(e->data.fd, send_buffer, 20, MSG_DONTWAIT);
                    alt_send_header.alt_dst_ip = server_addr_array[sock_index].sin_addr.s_addr;
                    ssize_t numBytes = sendto(udp_socket_array[sock_index], (void*) &alt_send_header, sizeof(alt_header), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
                    if (numBytes < 0){
                        if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                            continue;
                        }
                        else{
                            printf("send() failed on fd:%d\n", e->data.fd); 
                            //exit(1);
                            break;
                        }
                    }
                    else if (numBytes == 0 ){
                        if(send_byte_perloop == 20){
                            break;
                        }
                        else{
                            send_retries++;
                            printf("send 0 byte on fd:%d\n", e->data.fd);
                            if(send_retries == max_retries){
                                break;
                            }
                            else{
                                continue;
                            }
                        }
                    }
                    else{
                        send_byte_perloop = send_byte_perloop + numBytes;
                        //printf("send:%zd on fd %d\n", numBytes, e->data.fd);                
                    }
                }
            }
        }
    }

	printf("closing sockets then\n");
    for(int server_index = 0; server_index < expected_connections; server_index++){
        close(udp_socket_array[server_index]);
    }
    free(server_addr_array);
    free(udp_socket_array);
}