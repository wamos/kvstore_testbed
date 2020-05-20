#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// typedef struct socket_data {
//     int fd;
// } socket_data;

// void tcpListen(int fd, char* port, int backlogSize);
// int tcpAccept(int fd, uint64_t timeout, uint64_t socketBufferSize);
// int tcpConnect(int fd, char* address, char* port, uint64_t socketBufferSize, uint64_t retryDelayInMicros, uint64_t maxRetry);
// void tcpClose(int fd);
// uint64_t tcpSend(int fd ,char *buffer, uint64_t buf_size);
// uint64_t tcpReceive(int fd, char *buffer, uint64_t buf_size);

#define ITERS 100*1000

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