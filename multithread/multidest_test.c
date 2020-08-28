#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "multi_dest_header.h"
#include "multi_dest_protocol.h"

ssize_t sendto_sim(int fd, void* header, ssize_t size, int flag, struct sockaddr_in server_addr){
    return size;
}

ssize_t recvfrom_sim(int fd, void* header, ssize_t size, int flag, struct sockaddr_in server_addr){
    return size;
}

int main(int argc, char *argv[]) {
    multi_dest_buffer send_buf;
    alt_header recv_header[10];

    struct sockaddr_in server_addr;        
    char* recv_ip_addr = "10.0.0.8";
    char* recv_ip_addr2 = "10.0.0.9";
    in_port_t recv_port_start = 7000;
    server_addr.sin_port = htons(recv_port_start);
    server_addr.sin_addr.s_addr =inet_addr(recv_ip_addr);
    int fd = 4;

    if( init_multi_dest_buf(&send_buf, 10) < 0){
        printf("buf_init fail\n");
        exit(1);
    }

    // re-ordering 2, 3, 4, 5 -> 5, 2, 3 ,4
    // recv_header[0].request_id = 0;
    // recv_header[1].request_id = 1;
    // recv_header[2].request_id = 5;
    // recv_header[3].request_id = 2;
    // recv_header[4].request_id = 3;
    // recv_header[5].request_id = 4;
    // recv_header[6].request_id = 6;
    // recv_header[7].request_id = 7;
    // recv_header[8].request_id = 8;
    // recv_header[9].request_id = 9;

    // id beyond [0,9]
    recv_header[0].request_id = 10;
    recv_header[1].request_id = 11;
    recv_header[2].request_id = 12;
    recv_header[3].request_id = 14;
    recv_header[4].request_id = 13;
    recv_header[5].request_id = 15;
    recv_header[6].request_id = 16;
    recv_header[7].request_id = 17;
    recv_header[8].request_id = 18;
    recv_header[9].request_id = 19;


    //send/recv emulation
    // for(int i = 0; i < 10; i++){
    //     alt_header* header = acquire_multi_dest_buf(&send_buf);
    //     header->service_id = 1;
    //     header->request_id = i+10;
    //     header->packet_id  = 1;
    //     header->options    = 1;
    //     header->alt_dst_ip = inet_addr(recv_ip_addr);
    //     header->alt_dst_ip2 = inet_addr(recv_ip_addr2);
    //     ssize_t sent_byte = sendto_sim(fd, (void*) header, sizeof(header), 0, server_addr);
    //     ssize_t recv_byte = recvfrom_sim(fd, (void*) &recv_header[i], sizeof(alt_header), 0, server_addr);        
    //     //we inspect the status of multi_dest_buf now
    //     printf("before reclaim, req id:%d\n", i);
    //     printf("tail:%" PRIu32 ",head:%" PRIu32 "\n", send_buf.ack_tail, send_buf.ack_head);
    //     int ret_val = reclaim_multi_dest_buf(&send_buf, &recv_header[i]);
    //     printf("after  reclaim, req id:%d\n", i);
    //     printf("tail:%" PRIu32 ",head:%" PRIu32 "\n", send_buf.ack_tail, send_buf.ack_head);
    // }

    //test full buffer
    // for(int i = 0; i < 10+2; i++){
    //     alt_header* header = acquire_multi_dest_buf(&send_buf);
    //     if(header != NULL){
    //         header->service_id = 1;
    //         header->request_id = i+10;
    //         header->packet_id  = 1;
    //         header->options    = 1;
    //         header->alt_dst_ip = inet_addr(recv_ip_addr);
    //         header->alt_dst_ip2 = inet_addr(recv_ip_addr2);
    //     }
    // }

    //test empty buffer
    // alt_header* header = acquire_multi_dest_buf(&send_buf);
    // header->service_id = 1;
    // header->request_id = 10;
    // header->packet_id  = 1;
    // header->options    = 1;
    // header->alt_dst_ip = inet_addr(recv_ip_addr);
    // header->alt_dst_ip2 = inet_addr(recv_ip_addr2);
    // int ret_val = reclaim_multi_dest_buf(&send_buf, &recv_header[0]);
    // ret_val = reclaim_multi_dest_buf(&send_buf, &recv_header[1]);



    free_multi_dest_buf(&send_buf);

    return 0;
}