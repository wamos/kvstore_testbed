#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include "practical.h"
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include "socket_operation.h"
#include <errno.h>

static const int MAXPENDING = 5; // Maximum outstanding connection requests
static const int SERVER_BUFSIZE = 1024*16;

void* fake_mainloop(void *arg){
    int num = *(int*) arg;
    printf("fake mainloop:%d\n", num);
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
    int is_direct_to_client = (argc > 3) ? atoi(argv[4]) : 1;

    char recv_buffer[20];
    char send_buffer[20];

    struct timespec ts1, ts2, sleep_ts1, sleep_ts2;

	int send_sock, listen_sock;
	if ((listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
		perror("socket() failed\n");
        exit(1);
    }

    if ((send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
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
    //server_socket_setup(send_sock, routerAddr);

    //memset(&recv_buffer, 0, sizeof(recv_buffer));
    //memset(&send_buffer, 0, sizeof(send_buffer));

    alt_header Alt;
    memset(&Alt, 0, sizeof(alt_header));
    alt_header alt_response;
    memset(&alt_response, 0, sizeof(alt_header));
    ssize_t numBytesRcvd = 0;
    ssize_t numRcvd = 0;
    
    printf("sizeif Alt: %ld\n", sizeof(Alt));
    printf("server started!\n");
	while(numBytesRcvd < sizeof(alt_header)*ITERS) {
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
                /*char clntName[INET_ADDRSTRLEN]; // String to contain client address
                if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntName, sizeof(clntName)) != NULL)
                    printf("Handling client %s/ %d\n", clntName, ntohs(clntAddr.sin_port));
                else
                    printf("Unable to get client address\n");
                */
                // printf("service_id:%" PRIu16 ",", Alt.service_id);
                // printf("request_id:%" PRIu32 ",", Alt.request_id);
                // printf("packet_id:%" PRIu32 ",", Alt.packet_id);
                // printf("options:%" PRIu32 ",", Alt.options);
                //struct in_addr dest_addr;
               	//dest_addr.s_addr = Alt.actual_src_ip;
                //char* dest_ip = inet_ntoa(dest_addr);
                //printf("src_ip:%s\n", dest_ip);
                //printf("req_id:%" PRIu32 "\n", Alt.request_id);

                recv_bytes = recv_bytes +  numBytes;
                numBytesRcvd = numBytesRcvd + numBytes;
                numRcvd++;
                printf("recv:%zd\n", numRcvd);
            }
        }
	
        ssize_t send_bytes = 0;
        while(send_bytes < sizeof(alt_header)){
            //ssize_t numBytes = sendto(send_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            //set_alt_header_msgtype(&alt_response, SINGLE_PKT_RESP_PASSTHROUGH);
            set_alt_header_msgtype(&alt_response, SINGLE_PKT_RESP_PASSTHROUGH);
            alt_response.service_id = Alt.service_id;
            alt_response.request_id = Alt.request_id;
            alt_response.feedback_options = 0;
            alt_response.alt_dst_ip = Alt.actual_src_ip; //clntAddr.sin_addr.s_addr;
            alt_response.actual_src_ip = Alt.actual_src_ip; //clntAddr.sin_addr.s_addr;

            //printf("service_id:%" PRIu16 ",", alt_response.service_id);
            //printf("request_id:%" PRIu32 ",", alt_response.request_id);
            //printf("packet_id:%" PRIu32 ",", alt_response.packet_id);
            //printf("options:%" PRIu32 ",", alt_response.options);
            //struct in_addr dest_addr;
            //dest_addr.s_addr = alt_response.alt_dst_ip;
            //char* dest_ip =inet_ntoa(dest_addr);
            //printf("alt_dst_ip:%s\n", dest_ip);
            routerAddr.sin_port = clntAddr.sin_port;

            ssize_t numBytes;
            if(is_direct_to_client == 0)
                numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &routerAddr, sizeof(routerAddr));            
            else
                numBytes = sendto(listen_sock, (void*) &alt_response, sizeof(alt_response), 0, (struct sockaddr *) &clntAddr, sizeof(clntAddr));

            if (numBytes < 0){
                printf("send() failed\n");
                exit(1);
            }
            else if (numBytes == 0){
                printf("send no bytes\n");
            }
            else{
                send_bytes = send_bytes + numBytes;
                printf("send:%zd\n", numBytes);
            }
        }

	}

	printf("closing sockets then\n");
    close(listen_sock);
    close(send_sock);
    //close(send_sock);

}
