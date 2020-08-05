#ifndef MULTI_DEST_PROTOCOL_H
#define MULTI_DEST_PROTOCOL_H

#define _GNU_SOURCE

static const uint32_t BUFSIZE = 1024*1024;

// for each thread, it's not shared among threads
typedef struct {
    uint8_t isfull;    
    uint8_t out_of_order_recv;
    uint8_t* out_of_order_map;  
    uint32_t buf_size;
    alt_header* send_header; // dynamic alloc-ed buffer in case the size grows too large
    uint32_t ack_head;  // an index to lowest continous and avialable req index
    uint32_t ack_tail; // an index to the more recent sent req index
} multi_dest_buffer;

int multi_dest_buf_init(multi_dest_buffer* buf, uint32_t size){
    buf->buf_size = size;
    buf->ack_head = 0;
    buf->ack_tail = 0;
    buf->isfull = 0;
    buf->out_of_order_recv = 0; 
    buf->send_header = (alt_header*)malloc( size * sizeof(alt_header) );
    buf->out_of_order_map     = (uint8_t*)malloc( size * sizeof(uint8_t) ); 
    memset(buf->out_of_order_map, 0, size * sizeof(uint8_t));
    memset(buf->send_header, 0, size * sizeof(alt_header));

    if(buf->send_header == NULL)
        return -1;

    return 0;
}

inline alt_header* multi_dest_buf_acquire(multi_dest_buffer* buf){
    if(buf->isfull == 0){
        alt_header* header = &buf->send_header[buf->ack_head];
        buf->ack_head = (buf->ack_head+1)%buf->buf_size;
        if(buf->ack_head == buf->ack_tail){
          buf->isfull = 1;  
        }
        return header;
    }
    else{
        return NULL;
    }
}

int multi_dest_buf_reclaim(multi_dest_buffer* buf, alt_header* recv_header){
    if( buf->ack_tail == buf->ack_head && buf->isfull == 0){
        printf("can't pop empty buffer!\n");
        return -1;
    }
    else{
        buf->isfull = 0;
        if(recv_header->request_id == buf->send_header[buf->ack_tail].request_id){
            if(!buf->out_of_order_recv)
                buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
            else{ //there are out_of_order_recv reqs
                buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
                // catch up til the point that the next not-yet-received req, e.g. 
                // req 1, 2, 3, 4, 5
                // last recv req 2 and req 4, recv req 1 now
                // buf->ack_tail will catch up to req 2 
                while(buf->out_of_order_map[buf->ack_tail] > 0){               
                    buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
                    buf->out_of_order_recv--;
                }
            }
            return 0;
        }
        else{
            // later req arrives eariler:
            // recv_header->request_id > buf->send_header[ack_tail].request_id
            uint32_t index_diff = recv_header->request_id - buf->send_header[buf->ack_tail].request_id;
            printf("out of order recv\n");
            printf("recv_header->request_id:%" PRIu16 ",%" PRIu16"\n", recv_header->request_id, buf->send_header[buf->ack_tail].request_id);
            if(index_diff > 0){
                uint32_t index = (buf->ack_tail+index_diff)%buf->buf_size;
                buf->out_of_order_map[index] = 1;
                buf->out_of_order_recv++;
            }
            return 1;
        }       
    }        
}

// typedef struct { 
//     int fd;
//     uint16_t rto_interval; // if rto_interval=0, then this timer is dis-alarmed
//     int pool_index;    
//     alt_header* send_header; // a ptr to the sent req, req id is included here!
//     rto_timer_event* next_event;
//     rto_timer_event* last_event;
//     //uint32_t scheduled_slot;
// } rto_timer_event;

// // for each threads, it's not shared among threads
// typedef struct { 
//     rto_timer_event* timer_pool;
//     uint8_t* usage_map;
//     uint32_t avail_head;
//     uint32_t avail_tail;
// } timer_event_pool;

// typedef struct { 
//   uint64_t tick; // this number will grow out of the size of the timer wheel
//   rto_timer_event* event_head;  
// } timer_wheel_slot;

// // for each threads, it's not shared among threads
// typedef struct {
//     uint32_t wheel_tick_size;  
//     uint32_t current_index; //
//     timer_wheel_slot* wheel; // we need an array of timer_wheel_slot
// } simple_timer_wheel;

// int init_timer_pool(timer_event_pool* pool, int pool_size){
//     pool->avail_head = 0;
//     pool->avail_tail = 0;
//     pool->timer_pool = (rto_timer_event*)malloc( pool_size * sizeof(rto_timer_event) );
//     if(pool->timer_pool == NULL)
//         return -1;

//     return 0;
// }

// int init_timer_wheel(simple_timer_wheel* tm_wheel, int wheel_size){
//     tm_wheel->wheel = (timer_wheel_slot*)malloc( wheel_size * sizeof(timer_wheel_slot) );

//     if(tm_wheel->wheel == NULL)
//         return -1;

//     tm_wheel->current_index = 0;
//     tm_wheel->wheel_tick_size = wheel_size;
//     for(uint32_t tick = 0; tick < wheel_size; tick++){
//         tm_wheel->wheel[tick].tick = tick;
//         tm_wheel->wheel[tick].event_head = NULL;
//     }

//     return 0;
// }


#endif //MULTI_DEST_PROTOCOL_H