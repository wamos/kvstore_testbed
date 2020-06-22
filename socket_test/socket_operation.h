#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/tcp.h>
//#include <netinet/tcp.h>

// typedef struct socket_data {
//     int fd;
// } socket_data;

// void tcpListen(int fd, char* port, int backlogSize);
// int tcpAccept(int fd, uint64_t timeout, uint64_t socketBufferSize);
// int tcpConnect(int fd, char* address, char* port, uint64_t socketBufferSize, uint64_t retryDelayInMicros, uint64_t maxRetry);
// void tcpClose(int fd);
// uint64_t tcpSend(int fd ,char *buffer, uint64_t buf_size);
// uint64_t tcpReceive(int fd, char *buffer, uint64_t buf_size);

#define ITERS 20 //100*1000

typedef struct __attribute__((__packed__)) {
  uint16_t service_id;    // Type of Service.
  uint16_t request_id;    // Request identifier.
  uint16_t packet_id;     // Packet identifier.
  uint16_t options;       // Options (could be request length etc.).
  in_addr_t alt_dst_ip1;
  in_addr_t alt_dst_ip2;
  //in_addr_t alt_dst_ip3;
} alt_header;

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

int SetTCPNoDelay(int sock, int flag){
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1){
        //perror("setsockopt TCP_NODELAY error\n");
        return -1;
    }
    return 0;
}

static inline uint64_t realnanosleep(uint64_t target_latency, struct timespec* ts1, struct timespec* ts2){
    uint64_t accum = 0;
    while(accum < target_latency){
        clock_gettime(CLOCK_REALTIME, ts2);
		if(ts1->tv_sec == ts2->tv_sec){
        	accum = accum + (ts2->tv_nsec - ts1->tv_nsec);
		}
		else{
			uint64_t ts1_nsec = ts2->tv_nsec + 1000000000*ts2->tv_sec;
			uint64_t ts2_nsec = ts2->tv_nsec + 1000000000*ts2->tv_sec;
			accum = accum + (ts2_nsec - ts1_nsec);
    	}
		ts1->tv_nsec = ts2->tv_nsec;
		ts1->tv_sec = ts2->tv_sec;
	}
	return accum;
}
