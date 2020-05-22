#ifndef NANOSLEEP_H
#define NANOSLEEP_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

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

#endif //NANOSLEEP_H