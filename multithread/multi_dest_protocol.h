#ifndef MULTI_DEST_PROTOCOL_H
#define MULTI_DEST_PROTOCOL_H

#define _GNU_SOURCE
#include "multi_dest_header.h"
#include "nanosleep.h"

static const uint32_t BUFSIZE = 1024*1024;
#define MAX_RTO_REQ 100
#define MAX_PENDING_REQ 50

struct rto_timer_event{
    //int fd;
    uint32_t conn_index;
    //struct sockaddr_in* server_addr;
    alt_header send_header;
    alt_header recv_header;
    uint64_t received_tick; 
    struct rto_timer_event* next_event;
};
typedef struct rto_timer_event rto_timer_event;

// for each thread, it's not shared among threads
struct multi_dest_buffer{
    uint8_t isfull;    
    uint32_t buf_size;  

    // only used when there is out_of_order_recv
    // -1: not used, 0: sent, 1: recv on ack-tail position, 
    // 2: recv on position further 
    int32_t out_of_order_recv;
    int8_t* out_of_order_map;  

    rto_timer_event* send_timer; // dynamic alloc-ed buffer in case the size grows too large   
    uint32_t ack_head;  // an index to lowest continous and avialable req index
    uint32_t ack_tail; // an index to the more recent sent req index

    alt_header temp_recv_buffer;
    //alt_header* send_header; // dynamic alloc-ed buffer in case the size grows too large
};
typedef struct multi_dest_buffer multi_dest_buffer;

typedef struct {
    uint32_t tid;
    int fd;
    struct sockaddr_in server_addr;
    //[UNTESTED]: timer_event with send_header and recv_header
    // it assumes 1 outstanding sent request -> it doesn't work    
    multi_dest_buffer buffer;

    int32_t rto_counter;
    rto_timer_event* rto_buffer[MAX_RTO_REQ]; 
    //rto_timer_event* pending_event[MAX_PENDING_REQ];
} udp_pseudo_connection;

int init_multi_dest_buf(multi_dest_buffer* buf, uint32_t size);
rto_timer_event* acquire_multi_dest_header(multi_dest_buffer* buf);
int reclaim_multi_dest_buf(multi_dest_buffer* buf, rto_timer_event* event, uint32_t thread_id);
int reclaim_multi_dest_buf2(multi_dest_buffer* buf, rto_timer_event* event, uint32_t num_conn, uint32_t thread_id);
void free_multi_dest_buf(multi_dest_buffer* buf);

struct timer_wheel_slot { 
  uint64_t tick; // this number will grow out of the size of the timer wheel
  rto_timer_event* event_head;  
};
typedef struct timer_wheel_slot timer_wheel_slot;

// for each thread, it's not shared among threads
struct simple_timer_wheel{
    uint32_t wheel_tick_size;  
    //struct timespec last_access_time;
    uint64_t current_tick;     // current_tick%wheel_tick_size = processed_index
    uint32_t processed_index; // where the processing keeps up to
    timer_wheel_slot* wheel; // we need an array of timer_wheel_slot
    uint32_t rto_interval;
};
typedef struct simple_timer_wheel simple_timer_wheel;

int init_timer_wheel(simple_timer_wheel* tm_wheel, uint32_t wheel_size);
void schedule_event_timer_wheel(simple_timer_wheel* tm_wheel, rto_timer_event* event, uint32_t scheduled_index);
void processing_closedloop_timer_wheel(simple_timer_wheel* tm_wheel);
void processing_openloop_timer_wheel(simple_timer_wheel* tm_wheel, udp_pseudo_connection* conn_list, uint32_t num_conn, uint32_t thread_id);
//int process_event_timer_wheel(simple_timer_wheel* tm_wheel, rto_timer_event* event);
//void process_event_timer_wheel(simple_timer_wheel* tm_wheel, timer_event_pool* pool, uint64_t rto_interval);
// TODO: can we advance ticks without having a function for this?
static inline uint32_t update_ticks_timer_wheel(simple_timer_wheel* tm_wheel, uint32_t advance_index){
    // struct timespec now;
    // clock_gettime(CLOCK_REALTIME, &now);
    // uint32_t advance_index = (uint32_t) clock_gettime_diff_us(&tm_wheel->last_access_time, &now);
    // tm_wheel->last_access_time = now;
    //printf("update_ticks_timer_wheel, advance_index:%" PRIu64 "\n", advance_index);

    //updating wheel ticks before moving current_index forward
    printf("processed_index:%" PRIu32 "\n ticks:\n", tm_wheel->processed_index);
    for(uint32_t counter = 0; counter < advance_index; counter++){  
        uint32_t index = (tm_wheel->processed_index + counter)%tm_wheel->wheel_tick_size;      
        tm_wheel->wheel[index].tick = tm_wheel->wheel[index].tick + (uint64_t) tm_wheel->wheel_tick_size;
        printf("%" PRIu64 ",", tm_wheel->wheel[index].tick);
    } 
    printf("\n");

    return advance_index; 
}


#endif //MULTI_DEST_PROTOCOL_H