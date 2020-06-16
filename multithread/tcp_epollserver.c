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

/*int recv_socket_setup(int servSock, struct sockaddr_in servAddr, struct sockaddr_in clntAddr){
    int clntSock;
    // Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
		perror("bind() failed\n");
        exit(1);
    }
    
	// Mark the socket so it will listen for incoming connections
	if (listen(servSock, MAXPENDING) < 0){
		perror("listen() failed\n");
        exit(1); 
    }

	int flag = 1;
	if (setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag,
	  sizeof(int)) == -1) { 
        perror("setsockopt SO_REUSEADDR error\n"); 
        exit(1); 
    }

    int val = 1;
    if (setsockopt(servSock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1){
        perror("setsockopt TCP_NODELAY error\n");
    }

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
}*/

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

int SetTCPNoDelay(int servSock, int flag){
    if (setsockopt(servSock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1){
        //perror("setsockopt TCP_NODELAY error\n");
        return -1;
    }
    return 0;
}


int TCPSocketListen(int servSock, struct sockaddr_in servAddr){

    // Bind to the local address
	if (bind(servSock, (struct sockaddr*) &servAddr, sizeof(servAddr)) < 0){
		perror("bind() failed\n");
        return -1;
    }
    
	// Mark the socket so it will listen for incoming connections
	if (listen(servSock, MAXPENDING) < 0){
		perror("listen() failed\n");
        return -1; 
    }

    return 0;
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

void send_socket_setup(int send_sock, struct sockaddr_in servAddr){    
    if (connect(send_sock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
		printf("connect() failed\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {

	char* recvIP = argv[1];     // 1st arg: server IP address (dotted quad)
    in_port_t recvPort = (in_port_t) (argc > 2) ? atoi(argv[2]) : 6379;
    // assume 4 connections per open-loop thread and only 1 closed-loop connection
    int expected_connections = (argc > 3) ? atoi(argv[3])*4+1: 20;  

    char recv_buffer[20];
    char send_buffer[20];

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

	int recv_sock, listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

	// Construct local address structure
	struct sockaddr_in servAddr;                  // Local address
	memset(&servAddr, 0, sizeof(servAddr));       // Zero out structure
	servAddr.sin_family = AF_INET;                // IPv4 address family
	servAddr.sin_addr.s_addr = inet_addr(recvIP); // an incoming interface
	servAddr.sin_port = htons(recvPort);          // Local port
    
    struct sockaddr_in clntAddr; // Client address
    memset(&clntAddr, 0, sizeof(clntAddr));

    if(SetSocketReused(listen_sock, 1) == -1){
        perror("setsockopt SO_REUSEADDR error\n"); 
        exit(1);
    }

    if(SetTCPNoDelay(listen_sock, 1) == -1){
        perror("setsockopt TCP_NODELAY error\n");
        exit(1);
    }

    if(SetSocketNonblocking(listen_sock) == -1){
        perror("fcntl O_NONBLOCK error\n"); 
        exit(1);
    }

    if(TCPSocketListen(listen_sock, servAddr) == -1){
        perror("epoll_socket_listen failed\n");
        exit(1);
    }

    //recv_sock = recv_socket_setup(listen_sock, servAddr, clntAddr);
    //printf("listen_sock:%d, recv_sock:%d\n", listen_sock, recv_sock);
    memset(&recv_buffer, 0, sizeof(recv_buffer));
    //memset(&send_buffer, 0, sizeof(send_buffer));

    ssize_t numBytesRcvd;
    ssize_t numBytesSend;
    int conn_count = 1;
    int setsize = 10240;
    int fd_array[1024];
    int fd_tail = 0; //int fd_head = 0;
    memset(&fd_array, -1, sizeof(fd_array));

    epollState epstate;
    epstate.epoll_fd = -1;
    epstate.events = NULL;

    if(CreateEpoll(&epstate, setsize) == -1){
        printf("epoll_fd:%d\n", epstate.epoll_fd);
        perror("epoll create fails\n");
        exit(1);
    }


    uint32_t event_flag = EPOLLIN | EPOLLET;
    if(AddEpollEvent(&epstate, listen_sock, event_flag)== -1){
        perror("listen_sock cannot add EPOLLIN | EPOLLET\n");
        exit(1);
    }

    int total_events = 0;
    int accept_connections = 0;
    int max_retries = 5;
    //int expected_connections = 40;
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
                // if(e->events == EPOLLIN){
                //     printf("event:EPOLLIN \n");
                // }
                // else if(e->events == EPOLLET){
                //     printf("event:EPOLLET \n");
                // }
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

                if ( e->data.fd == listen_sock){              
                    printf("Accept connections\n");
                    int incoming_sock = TCPSocketAceept(listen_sock);
                    printf("incoming_sock_fd:%d\n", incoming_sock);
                    accept_connections++;
                    // Set incoming sock to non-blocking
                    if( SetSocketNonblocking(incoming_sock) == -1){
                        printf("incoming_sock_fd:%d\n", incoming_sock);
                        perror("fcntl O_NONBLOCK error\n"); 
                        continue;
                    }

                    event_flag = EPOLLOUT | EPOLLIN | EPOLLET | EPOLLRDHUP;
                    if(AddEpollEvent(&epstate, incoming_sock, event_flag) == -1){
                        printf("incoming_sock_fd:%d\n", incoming_sock);
                        perror("epoll_ctl error in AddEpollEvent\n");
                        continue;
                    }
                    fd_array[fd_tail] = incoming_sock;
                    fd_tail = fd_tail + 1;
                }
                else{
                    if(accept_connections < expected_connections){
                        //printf("highest fd: %d\n", fd_array[fd_tail-1]);
                        printf("accept fds: %d, expected fds: %d\n", accept_connections, expected_connections);
                        continue;                    
                    }

                    // if(e->data.fd == fd_array[fd_tail-1]){
                    //     printf("closed-loop e->data.fd:%d\n", e->data.fd);
                    //     if(e->events == EPOLLIN){
                    //         printf("event:EPOLLIN \n");
                    //     }
                    //     else if(e->events == EPOLLET){
                    //         printf("event:EPOLLET \n");
                    //     }
                    //     else if( e->events == EPOLLHUP){
                    //         perror("\n-------\nevent:EPOLLHUP\n--------\n");
                    //         continue;
                    //     }
                    //     else if( e->events == EPOLLERR){
                    //         perror("\n-------\nevent:EPOLLERR\n--------\n");
                    //         continue;
                    //     }
                    //     else if( e->events == EPOLLRDHUP){
                    //         perror("\n-------\nevent:EPOLLERR\n--------\n");
                    //         continue;
                    //     }
                    // }

                    ssize_t numBytes = 0;
                    ssize_t recv_byte_perloop = 0;
                    ssize_t send_byte_perloop = 0;
                    int recv_retries = 0; 
                    int send_retries = 0;                    
                    while(recv_byte_perloop < 20){
                        numBytes = recv(e->data.fd, recv_buffer, 20, MSG_DONTWAIT);
                        if (numBytes < 0){
                            if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                                //send_byte_perloop = 20; // trick the next send-loop to fail
                                //break;
                                continue;
                            }
                            else{
                                printf("recv() failed on fd:%d\n", e->data.fd); 
                                //exit(1);
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
                            //printf("recv:%zd\n", numBytes);
                        }
                    }

                    //clock_gettime(CLOCK_REALTIME, &ts1);
                    //sleep_ts1=ts1;
                    //realnanosleep(10*1000, &sleep_ts1, &sleep_ts2); // processing time 10 us

                    while(send_byte_perloop < 20){
                        numBytes = send(e->data.fd, send_buffer, 20, MSG_DONTWAIT);
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
                        }
                    }
                }
            }
        }
    }

	printf("closing sockets then\n");
    close(listen_sock);
    close(recv_sock);
    //close(send_sock);

}