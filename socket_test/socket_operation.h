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

#define ITERS 5000

// typedef struct __attribute__((__packed__)) {
//   uint16_t service_id;    // Type of Service.
//   uint32_t request_id;    // Request identifier.
//   //uint16_t packet_id;     // Packet identifier.
//   uint16_t options;       // Options (could be request length etc.).
//   in_addr_t alt_dst_ip1;
//   in_addr_t alt_dst_ip2;
//   in_addr_t alt_dst_ip3;
// } alt_header;

#define FIRST_FLAG 0x20
#define LAST_FLAG 0x10

// total 24 bytes
struct alt_header {
  // 1 + 1 + 2 + 4 = 8 bytes
  uint8_t  msgtype_flags;
  uint8_t  redirection;
  uint8_t  header_size;  	 	
  uint8_t  reserved;

  uint16_t feedback_options;	
  uint16_t service_id;    // Type of Service.
  uint32_t request_id;    // Request identifier.
  
  // 12 bytes
  uint32_t alt_dst_ip;
  uint32_t alt_dst_ip2;
  uint32_t alt_dst_ip3;
} __attribute__((__packed__)); // or use __rte_packed
typedef struct alt_header alt_header;

// msgtype_flags field details: suspended for now
// -> bit 0,1: unused now
// -> bit 2,3: FIRST_FLAG and LAST_FLAG for multi-packet req
// -> bit 4-7: msg_type listed in the enum below

enum {
  SINGLE_PKT_REQ = 0,
  SINGLE_PKT_RESP_PIGGYBACK,
  SINGLE_PKT_RESP_PASSTHROUGH,  
  HOST_FEEDBACK_MSG,
  SWITCH_FEEDBACK_MSG,
  MULTI_PKT_REQ,
  MULTI_PKT_RESP_PIGGYBACK,
  MULTI_PKT_RESP_PASSTHROUGH,  
  CONTROL_MSG,
};

// static inline void set_alt_header_isfirst(struct alt_header *h){
// 	h->msgtype_flags = h->msgtype_flags | FIRST_FLAG;
// }

// static inline void set_alt_header_islast(struct alt_header *h){
// 	h->msgtype_flags = h->msgtype_flags | LAST_FLAG;
// }

// static inline uint8_t get_alt_header_isfirst(struct alt_header *h){
// 	return (h->msgtype_flags & FIRST_FLAG) >> 6;
// }

// static inline uint8_t get_alt_header_islast(struct alt_header *h){
// 	return (h->msgtype_flags & LAST_FLAG) >> 5;
// }

static inline void set_alt_header_msgtype(struct alt_header *h, uint8_t value){
  //value = value & 0x0F; // leave the lower 4-bit
  //h->msgtype_flags = h->msgtype_flags | value;
  h->msgtype_flags = value;
}

static inline uint8_t get_alt_header_msgtype(struct alt_header *h){
	//return (h->msgtype_flags & 0x0F) >> 4;
  return h->msgtype_flags;
}



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
