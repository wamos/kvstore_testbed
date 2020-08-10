#ifndef MULTI_DEST_PROTOCOL_H
#define MULTI_DEST_PROTOCOL_H

#define _GNU_SOURCE
#include "multi_dest_header.h"
#include "nanosleep.h"

static const uint32_t BUFSIZE = 1024*1024;

// for each thread, it's not shared among threads
struct multi_dest_buffer{
    uint8_t isfull;    
    uint8_t out_of_order_recv;
    uint8_t* out_of_order_map;  
    uint32_t buf_size;
    alt_header* send_header; // dynamic alloc-ed buffer in case the size grows too large
    uint32_t ack_head;  // an index to lowest continous and avialable req index
    uint32_t ack_tail; // an index to the more recent sent req index
};
typedef struct multi_dest_buffer multi_dest_buffer;

int init_multi_dest_buf_init(multi_dest_buffer* buf, uint32_t size);
alt_header* acquire_multi_dest_buf(multi_dest_buffer* buf);
int reclaim_multi_dest_buf(multi_dest_buffer* buf, alt_header* recv_header);
void free_multi_dest_buf(multi_dest_buffer* buf);

struct rto_timer_event{ 
    int fd;
    uint32_t received_index; 
    // if received_index == tm_wheel->wheel_tick_size, it means not received yet
    struct rto_timer_event* next_event;
    struct timespec send_time;    
    //uint32_t rto_interval; 
    // [NOPE] if rto_interval=0, then this timer is dis-alarmed        
    // rto_interval should be placed into thread-state data structure    
    //struct rto_timer_event* last_event;
    //alt_header* send_header; // a ptr to the sent req, req id is included here!
};
typedef struct rto_timer_event rto_timer_event;

// int init_rto_timer_event(rto_timer_event* event, int fd, uint32_t rto_interval,
//     alt_header* send_header, rto_timer_event* next_event);

// // for each thread, it's not shared among threads
// struct timer_event_pool { 
//     rto_timer_event* timer_pool;
//     uint8_t* usage_map;
//     uint8_t isfullyused;
//     uint8_t out_of_order_usage;
//     uint32_t pool_size; 
//     uint32_t used_head;
//     uint32_t used_tail;
// }; 
// typedef struct timer_event_pool timer_event_pool;

// int init_timer_pool(timer_event_pool* pool, uint32_t pool_size);
// rto_timer_event* acquire_timer_from_pool(timer_event_pool* pool);
// int reclaim_timer_to_pool(timer_event_pool* pool, rto_timer_event* retired_timer);

struct timer_wheel_slot { 
  uint64_t tick; // this number will grow out of the size of the timer wheel
  rto_timer_event* event_head;  
};
typedef struct timer_wheel_slot timer_wheel_slot;

// for each thread, it's not shared among threads
struct simple_timer_wheel{
    uint32_t wheel_tick_size;  
    struct timespec last_access_time;
    uint32_t current_index; //
    timer_wheel_slot* wheel; // we need an array of timer_wheel_slot
};
typedef struct simple_timer_wheel simple_timer_wheel;

int init_timer_wheel(simple_timer_wheel* tm_wheel, int wheel_size);
void schedule_event_timer_wheel(simple_timer_wheel* tm_wheel, rto_timer_event* event, uint64_t rto_interval);
//int process_event_timer_wheel(simple_timer_wheel* tm_wheel, rto_timer_event* event);
void process_event_timer_wheel(simple_timer_wheel* tm_wheel, timer_event_pool* pool, uint64_t rto_interval);
// TODO: can we advance ticks without having a function for this?
inline void update_ticks_timer_wheel(simple_timer_wheel* tm_wheel){
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    uint64_t advance_index = clock_gettime_diff_us(&tm_wheel->last_access_time, &now);
    //printf("update_ticks_timer_wheel, advance_index:%" PRIu64 "\n", advance_index);
    tm_wheel->current_index = tm_wheel->current_index + (uint32_t) advance_index;
}


#endif //MULTI_DEST_PROTOCOL_H