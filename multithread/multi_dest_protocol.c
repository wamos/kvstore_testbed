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

#include "multi_dest_protocol.h"

int init_multi_dest_buf(multi_dest_buffer* buf, uint32_t size){
    buf->buf_size = size;
    buf->ack_head = 0;
    buf->ack_tail = 0;
    buf->isfull = 0;
    buf->out_of_order_recv = 0;     
    buf->send_timer  = (rto_timer_event*)malloc( size * sizeof(rto_timer_event) );
    buf->out_of_order_map  = (uint8_t*)malloc( size * sizeof(int8_t) ); 
    // un-used: -1, sent: 0 
    // for recv-ed buf, val > 0, recv in-order: 1, recv out-of-order: 2
    memset(buf->out_of_order_map, -1, size * sizeof(int8_t));    
    memset(buf->send_timer, 0, size * sizeof(rto_timer_event));
    //buf->send_header = (alt_header*)malloc( size * sizeof(alt_header) );
    //memset(buf->send_header, 0, size * sizeof(alt_header));

    if(buf->send_timer == NULL)
        return -1;

    return 0;
}

rto_timer_event* acquire_multi_dest_header(multi_dest_buffer* buf){
    if(buf->isfull == 0){
        rto_timer_event* ret_timer  = &buf->send_timer[buf->ack_head];
        buf->out_of_order_map[buf->ack_head] = 0;
        buf->ack_head = (buf->ack_head+1)%buf->buf_size;
        if(buf->ack_head == buf->ack_tail){
          buf->isfull = 1;  
        }
        return ret_timer;
    }
    else{
        printf("buf is full:%" PRIu8 "\n", buf->isfull);
        return NULL;
    }
}

int reclaim_multi_dest_buf2(multi_dest_buffer* buf, rto_timer_event* event, uint32_t num_conn, uint32_t thread_id){
    if( buf->ack_tail == buf->ack_head && buf->isfull == 0){
        printf("can't pop empty buffer!\n");
        return -1;
    }
    else{
        buf->isfull = 0;
        uint32_t ack_request_id = buf->send_timer[buf->ack_tail].send_header.request_id;
        //printf("thread id:%" PRIu32 "recv request_id:%" PRIu32 "send request_id:%" PRIu32 ",ack_tail request_id:%" PRIu32",", thread_id, event->recv_header.request_id, event->send_header.request_id, ack_request_id);

        if(event->recv_header.request_id == ack_request_id){ // event->recv_header.request_id == event->send_header.request_id){            
            if(buf->out_of_order_recv == 0){
                //printf("perfect_reclaiming\n");
                buf->out_of_order_map[buf->ack_tail] = 1;
                buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
            }
            else if(buf->out_of_order_recv < 0){
                printf("incorrect numbers of out_of_order_recv\n");  
            }
            else{
                //printf("out_of_order_map[ack_tail+0]:%" PRIu8 "\n", buf->out_of_order_map[buf->ack_tail]);                                
                buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
                printf("out_of_order_map[ack_tail+1]:%" PRIu8 "\n", buf->out_of_order_map[buf->ack_tail]);
                printf("out_of_order_map[ack_tail+2]:%" PRIu8 "\n", buf->out_of_order_map[buf->ack_tail+1]);
                printf("out_of_order_map[ack_tail+3]:%" PRIu8 "\n", buf->out_of_order_map[buf->ack_tail+2]);    

                while(buf->out_of_order_map[buf->ack_tail] > 1){   // == 2             
                    buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
                    buf->out_of_order_recv--;
                }
                printf("reclaiming_with_out_of_order_recv, out_of_order_recv:%" PRId32 "\n", buf->out_of_order_recv);
            }
        }
        else if(event->recv_header.request_id > ack_request_id){ // event->recv_header.request_id == event->send_header.request_id){
            // early-arrived requestse 
            // because request_id are assigned in send-loop and round-robin on each pseudo-connection

            // uint32_t index_diff = (event->recv_header.request_id - ack_request_id)/num_conn; 
            // uint32_t index = (buf->ack_tail + index_diff)%buf->buf_size;                         
            // buf->out_of_order_map[index] = 2;
            // buf->out_of_order_recv++;

            buf->out_of_order_map[buf->ack_tail] = 1;
            buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;

            //printf("in_front_of_tail requests, out_of_order_recv:%" PRId32 "\n", buf->out_of_order_recv);
        }
        else if(event->recv_header.request_id < ack_request_id){ // event->recv_header.request_id == event->send_header.request_id){ 
            // late-arrived requests -> drop them
            //buf->out_of_order_map[buf->ack_tail] = 3;
            //printf("behind_tail requests\n");
        }
        else{
            printf("unknown error\n");
            // this shouldn't happen I guess?
        }

    }
    return 0;
}

void free_multi_dest_buf(multi_dest_buffer* buf){
    if(buf->send_timer != NULL)
        free(buf->send_timer);
    
    if(buf->out_of_order_map != NULL)
        free(buf->out_of_order_map);
}

int init_timer_wheel(simple_timer_wheel* tm_wheel, uint32_t wheel_size){
    tm_wheel->wheel = (timer_wheel_slot*)malloc( wheel_size * sizeof(timer_wheel_slot) );

    if(tm_wheel->wheel == NULL)
        return -1;

    tm_wheel->current_tick = 0;
    tm_wheel->processed_index = 0;
    //clock_gettime(CLOCK_REALTIME, &tm_wheel->last_access_time);
    tm_wheel->wheel_tick_size = wheel_size;
    for(uint32_t tick = 0; tick < wheel_size; tick++){
        tm_wheel->wheel[tick].tick = tick;
        tm_wheel->wheel[tick].event_head = NULL;
    }

    return 0;
}

void processing_closedloop_timer_wheel(simple_timer_wheel* tm_wheel){
    // use monotonically incresing ticks instead of wrap-around index 
    // e.g. received_index = 485, schedule_index/current_index = 20
    // use index can cause this to be time-out
    // however, schedule_index/current_index 20 here should have tick = 520
    // schedule_tick/current_tick 520 > received index/tick 485
    // so there shouldn't be a timeout if we use ticks    
    rto_timer_event* event = tm_wheel->wheel[tm_wheel->processed_index].event_head;

    if(event->received_tick <= tm_wheel->wheel[tm_wheel->processed_index].tick){
        printf("recv tick:%" PRIu64 ",", event->received_tick);
        printf("schd tick:%" PRIu64 ",", tm_wheel->wheel[tm_wheel->processed_index].tick);
        printf("event finished within rto_interval\n");
        printf("recv request_id:%" PRIu32 "\n", event->recv_header.request_id);
        // clear the timer slot before return        
    }
    //else case: timeout requests
        // we don't do anything here          
    tm_wheel->wheel[tm_wheel->processed_index].event_head = NULL;
}

void processing_openloop_timer_wheel(simple_timer_wheel* tm_wheel, udp_pseudo_connection* conn_list, uint32_t num_conn, uint32_t thread_id){
    // use monotonically incresing ticks instead of wrap-around index 
    // e.g. received_index = 485, schedule_index/current_index = 20
    // use index can cause this to be time-out
    // however, schedule_index/current_index 20 here should have tick = 520
    // schedule_tick/current_tick 520 > received index/tick 485
    // so there shouldn't be a timeout if we use ticks    
    rto_timer_event* event = tm_wheel->wheel[tm_wheel->processed_index].event_head;
    uint32_t conn_index = event->conn_index;

    //printf("recv tick:%" PRIu64 ", schd tick:%" PRIu64 ", conn_index:%" PRIu32 ",", event->received_tick, tm_wheel->wheel[tm_wheel->processed_index].tick, conn_index);

    //printf("schd tick:%" PRIu64 ",", tm_wheel->wheel[tm_wheel->processed_index].tick);

    if(event->received_tick < UINTMAX_MAX){
        // 1. proceess event_head
        if(event->received_tick <= tm_wheel->wheel[tm_wheel->processed_index].tick){
            //printf("event finished within rto_interval\n");
            if(event->recv_header.request_id != event->send_header.request_id){
                printf("unmatched id! in processing_openloop_timer_wheel,");
                printf("thread id:%" PRIu32 ",recv request_id:%" PRIu32 ",send request_id:%" PRIu32 "\n", thread_id, event->recv_header.request_id, event->send_header.request_id);
                //conn_list[event->conn_index].pending_req--;
                //conn_list[event->conn_index].rto_buffer[conn_list[event->conn_index].rto_counter] = event;
                //conn_list[event->conn_index].rto_counter++;  
            }
            else{
                //conn_list[event->conn_index].pending_req--;
                reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, event, num_conn, thread_id);  
            }
        }
        else{ // [TEMP] for testing 
            //printf("timeout in processing_openloop_timer_wheel,");
            //printf("thread id:%" PRIu32 ",recv request_id:%" PRIu32 ",send request_id:%" PRIu32 "\n", thread_id, event->recv_header.request_id, event->send_header.request_id);
            reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, event, num_conn, thread_id);
            //conn_list[event->conn_index].pending_req--;
            //conn_list[event->conn_index].rto_buffer[conn_list[event->conn_index].rto_counter] = event;
            //conn_list[event->conn_index].rto_counter++;          
        }
    }
    else{
        // [TEMP] to run the whole program!
        event->recv_header.request_id = event->send_header.request_id;
        //printf("no_recv_then_RTO, thread id:%" PRIu32 ",recv request_id:%" PRIu32 ",send request_id:%" PRIu32 "\n", thread_id, event->recv_header.request_id, event->send_header.request_id);
        reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, event, num_conn, thread_id);
        //conn_list[event->conn_index].rto_buffer[conn_list[event->conn_index].rto_counter] = event;
        //conn_list[event->conn_index].rto_counter++; 
    }
    //else case: timeout requests
        // we don't do anything here, 
        // their value in buf->out_of_order_map will be 0 representing sent
        // but not received yet

    while(event->next_event !=  NULL){ //walk the linked-list of timers
        event = event->next_event;
        // the if code copied here!
        if(event->received_tick < UINTMAX_MAX){
            reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, event, num_conn, thread_id);
        }
        else{
            event->recv_header.request_id = event->send_header.request_id;
            reclaim_multi_dest_buf2(&conn_list[conn_index].buffer, event, num_conn, thread_id);
            //conn_list[event->conn_index].rto_buffer[conn_list[event->conn_index].rto_counter] = event;
            //conn_list[event->conn_index].rto_counter++;
        }
    }

    //     printf("recv tick:%" PRIu64 ",", event->received_tick);
    //     printf("schd tick:%" PRIu64 ",", tm_wheel->wheel[tm_wheel->processed_index].tick);

    //     if(event->received_tick <= tm_wheel->wheel[tm_wheel->processed_index].tick){
    //         conn_list[event->conn_index].pending_req--;
    //         reclaim_multi_dest_buf(buf, event, thread_id);
    //     }
    //     else{
    //         printf("RTO! in processing_openloop_timer_wheel,");
    //         printf("thread_id%" PRIu32 ", sent request_id:%" PRIu32 "\n", thread_id, event->send_header.request_id);
    //         conn_list[event->conn_index].pending_req--;
    //         buf->rto_buffer[buf->rto_counter] = event;
    //         buf->rto_counter++;
    //     }
    // }

    // clear the timer slot before return  
    tm_wheel->wheel[tm_wheel->processed_index].event_head = NULL;
}

// TODO: rewrite the whole function
// 1. take the scheduled_index calculation out of the function
// 2. we only want to contain pointer update operation in this fucntion
void schedule_event_timer_wheel(simple_timer_wheel* tm_wheel, rto_timer_event* event, uint32_t scheduled_index){
    //uint32_t scheduled_index = 0; 
    // if(rto_interval >= tm_wheel->wheel_tick_size){ // schedule for the max possible interval
    //     scheduled_index = (tm_wheel->current_index + tm_wheel->wheel_tick_size - 1) % tm_wheel->wheel_tick_size;
    // }
    // else{ // rto_interval < wheel_tick_size
    //     scheduled_index = (tm_wheel->current_index + rto_interval) % tm_wheel->wheel_tick_size;
    // }
    // printf("scheduled_index:%" PRIu32 "\n", scheduled_index);

    if(tm_wheel->wheel[scheduled_index].event_head == NULL){
        //printf("event_head == NULL\n");
        tm_wheel->wheel[scheduled_index].event_head = event;
        //printf("event_head__: %p\n", tm_wheel->wheel[scheduled_index].event_head);
        //printf("event_insert: %p\n", event);
    }
    else{
        // pre: slot.event_head->previous_head
        // post: slot.event_head->event->previous_head
        //rto_timer_event* previous_head = tm_wheel->wheel[scheduled_index].event_head;         
        //event->next_event = previous_head;
        printf("event_head != NULL\n");
        event->next_event = tm_wheel->wheel[scheduled_index].event_head;
        tm_wheel->wheel[scheduled_index].event_head = event;
    }
    // printf("rto_timer_event:%p\n", event);
    // printf("tm_wheel->wheel[scheduled_index].event_head:%p\n", tm_wheel->wheel[scheduled_index].event_head);
}


// int reclaim_multi_dest_buf(multi_dest_buffer* buf, rto_timer_event* event, uint32_t thread_id){
//     if( buf->ack_tail == buf->ack_head && buf->isfull == 0){
//         printf("can't pop empty buffer!\n");
//         return -1;
//     }
//     else{
//         buf->isfull = 0;
//         //printf("buf->ack_tail:%" PRIu32 "\n", buf->ack_tail);
//         uint32_t ack_request_id = buf->send_timer[buf->ack_tail].send_header.request_id;

//         //printf("thread id:%" PRIu32 ",recv request_id:%" PRIu32 ",send request_id:%" PRIu32 ",ack_tail request_id:%" PRIu32",", thread_id, event->recv_header.request_id, event->send_header.request_id, ack_request_id);

//         //printf("sent_request_id:%" PRIu32 "\n", sent_request_id);
//         if(event->recv_header.request_id == ack_request_id){
//             if(!buf->out_of_order_recv){
//                 printf("perfect_reclaiming\n");
//                 buf->out_of_order_map[buf->ack_tail] = 1;
//                 buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
//             }
//             else{ //there are out_of_order_recv reqs
//                 printf("reclaiming_with_out_of_order_recv\n");
//                 buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
//                 // catch up til the point that the next not-yet-received req, e.g. 
//                 // req 1, 2, 3, 4, 5
//                 // last recv req 2 and req 4, recv req 1 now
//                 // buf->ack_tail will catch up to req 2 
//                 while(buf->out_of_order_map[buf->ack_tail] > 1){               
//                     buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
//                     buf->out_of_order_recv--;
//                 }
//             }
//             return 0;
//         }
//         else{
//             // TODO: duplicated response? 
//             // -> yes, e.g. recv req 0 again after req 64
//             // DONE: later req arrives eariler
//             // recv_header->request_id > buf->send_header[ack_tail].request_id
//             int32_t index_diff = (int32_t)event->recv_header.request_id - (int32_t) ack_request_id;            
//             if(index_diff > 0){
//                 printf("out_of_order_recv, early arrived\n");
//                 uint32_t index = (buf->ack_tail + (uint32_t) index_diff)%buf->buf_size;
//                 buf->out_of_order_map[index] = 2;
//                 buf->out_of_order_recv++;
//                 //buf->ack_tail = (buf->ack_tail + 1)%buf->buf_size;
//             }
//             else{ //index_diff < 0
//                 printf("out_of_order_recv, late arrived\n");
//             }            
//             return 1;
//         }       
//     }        
// }


// void process_preslot_timer_wheel(simple_timer_wheel* tm_wheel, multi_dest_buffer* buf){
//     // we've pre-checked that proceess event_head current_slot.event_head != NULL
//     // timer_wheel_slot current_slot = tm_wheel->wheel[tm_wheel->processed_index];    
//     // printf("current_slot.event_head %p\n", current_slot.event_head);
//     // rto_timer_event* event = current_slot.event_head;
//     rto_timer_event* event = tm_wheel->wheel[tm_wheel->processed_index].event_head;

//     // use monotonically incresing ticks instead of wrap-around index 
//     // e.g. received_index = 485, schedule_index/current_index = 20
//     // use index can cause this to be time-out
//     // however, schedule_index/current_index 20 here should have tick = 520
//     // schedule_tick/current_tick 520 > received index/tick 485
//     // so there shouldn't be a timeout if we use ticks

//     // TODO: make 1. and 2. a for-loop
//     // 1. proceess event_head
//     printf("recv tick:%" PRIu64 ",", event->received_tick);
//     printf("schd tick:%" PRIu64 ",", tm_wheel->wheel[tm_wheel->processed_index].tick);
//     if(event->received_tick > tm_wheel->wheel[tm_wheel->processed_index].tick){
//     //if(event->received_index == tm_wheel->wheel_tick_size){//> tm_wheel->current_index){
//         //printf("retransmit!\n");
//         printf("event finished after rto_interval\n");
//         reclaim_multi_dest_buf(buf, &event->recv_header);   
//         // let's retransmit!, set up server addr
//         struct sockaddr_in serv_addr;
//         serv_addr.sin_addr.s_addr = event->send_header.alt_dst_ip; 
//         serv_addr.sin_port = event->send_header.dst_port;
//         int serv_addr_len = sizeof(serv_addr);                
//         // TODO: send-loop here
//         ssize_t send_bytes = 0;
//         while(send_bytes < sizeof(alt_header)){
//             ssize_t numBytes = sendto(event->fd, (void*) &event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &server_addr, (socklen_t) serv_addr_len);

//             if (numBytes < 0){
//                 printf("send() failed\n");
//                 exit(1);
//             }
//             else{
//                 send_bytes = send_bytes + numBytes;
//                 printf("re-send:%zd, reqid:%" PRIu32 "\n", numBytes, event->send_header.request_id);
//             }
//         }  
//         // TODO: schedule retransmission!  
//         uint32_t scheduled_index = (tm_wheel->current_tick + tm_wheel->rto_interval)%tm_wheel->wheel_tick_size;
//         schedule_event_timer_wheel(tm_wheel, event, scheduled_index);
//     }
//     else{
//         printf("event finished within rto_interval\n");
//         printf("recv request_id:%" PRIu32 "\n", event->recv_header.request_id);
//         reclaim_multi_dest_buf(buf, &event->recv_header);
//         //tm_wheel->wheel[tm_wheel->processed_index].event_head = NULL;
//         // TODO: update per thread completed_req?
//     } 
    
//     // 2. proceess event_head->next_event
//     // while(event->next_event !=  NULL){ //walk the linked-list of timers
//     //     event = event->next_event; // actually walk to the next event

//     //     //received_index == tm_wheel->wheel_tick_size, it means not received yet
//     //     //if(event->received_index > tm_wheel->current_index){
//     //     if(event->received_index == tm_wheel->wheel_tick_size){//> tm_wheel->current_index){
//     //         printf("retransmit!\n");
//     //         //let's retransmit!
//     //         struct sockaddr_in serv_addr;
//     //         serv_addr.sin_addr.s_addr = event->send_header.alt_dst_ip; 
//     //         serv_addr.sin_port = event->send_header.dst_port;
//     //         int serv_addr_len = sizeof(serv_addr);
//     //         //numBytes = sendto(event->fd, (void*) event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &serv_addr, (socklen_t) serv_addr_len);            
//     //         ssize_t send_bytes = 0;
//     //         printf("event finished after rto_interval\n");     
//     //         uint32_t scheduled_index = (tm_wheel->current_index + tm_wheel->rto_interval)%tm_wheel->wheel_tick_size;  
//     //         schedule_event_timer_wheel(tm_wheel, event, scheduled_index);
//     //     }
//     //     else{
//     //         printf("event finished within rto_interval\n");
//     //         printf("recv request_id:%" PRIu32 "\n", event->recv_header.request_id);
//     //         reclaim_multi_dest_buf(buf, &event->recv_header);
//     //         // TODO: update per thread completed_req?
//     //     }                            
//     // }

//     // clear the timer slot before return  
//     tm_wheel->wheel[tm_wheel->processed_index].event_head = NULL;
// }


// int init_timer_pool(timer_event_pool* pool, uint32_t pool_size){
//     pool->isfullyused = 0;
//     pool->pool_size = pool_size;
//     pool->used_head = 0;
//     pool->used_tail = 0;
//     pool->out_of_order_usage = 0;
//     pool->timer_pool = (rto_timer_event*)malloc( pool_size * sizeof(rto_timer_event) );
//     if(pool->timer_pool == NULL)
//         return -1;

//     pool->usage_map = (uint8_t*)malloc( pool_size * sizeof(uint8_t) );
//     if(pool->usage_map == NULL)
//         return -1;    

//     memset(pool->timer_pool, 0, pool_size * sizeof(rto_timer_event));
//     memset(pool->usage_map, 0, pool_size * sizeof(uint8_t));

//     return 0;
// }

// rto_timer_event* acquire_timer_from_pool(timer_event_pool* pool){
//     if(pool->isfullyused == 0){
//         rto_timer_event* timer = &pool->timer_pool[pool->used_head];
//         timer->pool_index = pool->used_head;
//         pool->used_head = (pool->used_head+1)%pool->pool_size;
//         if(pool->used_head == pool->used_tail){
//             pool->isfullyused = 1;
//         }
//         return timer;
//     }
//     else{
//        printf("all timers in pool are used :%" PRIu8 "\n", pool->isfullyused);
//        return NULL; 
//     }

// }

// int reclaim_timer_to_pool(timer_event_pool* pool, rto_timer_event* retired_timer){
//     if(pool->used_head == pool->used_tail && pool->isfullyused == 0){
//         printf("all timers have returned, where does this timer come from?\n");
//         return -1;
//     }
//     else{
//         pool->isfullyused = 0;
//         if(retired_timer->pool_index == pool->used_tail){ //in-order return to pool
//             if(!pool->out_of_order_usage){
//                 pool->used_tail = (pool->used_tail + 1)%pool->pool_size;
//             }
//             else{ // in order, but need to clear out-of-order timers
//                 pool->used_tail = (pool->used_tail + 1)%pool->pool_size;
//                 while(pool->usage_map[pool->used_tail] > 0){               
//                     pool->used_tail = (pool->used_tail + 1)%pool->pool_size;
//                     pool->out_of_order_usage--;
//                 }
//             }
//             return 0;
//         }
//         else{ // out-of-order
//             pool->out_of_order_usage++;
//             pool->usage_map[retired_timer->pool_index] = 1;
//             return 1;
//         }   
//     } 

// }

// int init_rto_timer_event(rto_timer_event* event, int fd, uint32_t rto_interval,
//     alt_header* send_header, rto_timer_event* next_event){

//     event->fd = fd;
//     event->send_header = send_header;
//     event->next_event = next_event;

//     return 0;
// }

// process events from timer wheel and return expired ones to timer_event_pool
// [NEED TO OPTIMIZE]: take 1 us in average and up to 3 us to process 1 timer event!

// TODO: rewrite the whole function
// 1. trevarse the wheel outside this function
// 2. this function handles per slot timer-event processing
// 3. don't include re-transmision here?
// void process_event_timer_wheel(simple_timer_wheel* tm_wheel, timer_event_pool* pool, uint64_t rto_interval){
//     //to know how many slots we need to keep up in the following while loop
//     struct timespec now;
//     clock_gettime(CLOCK_REALTIME, &now);
//     int64_t advance_index = (int64_t) clock_gettime_diff_us(&tm_wheel->last_access_time, &now);
//     //uint64_t ts1_nsec = (uint64_t) tm_wheel->last_access_time.tv_nsec + 1000000000 * (uint64_t) tm_wheel->last_access_time.tv_sec;
//     //printf("last_access_time:%" PRIu64 "\n", ts1_nsec);
//     printf("Now_index:%" PRIu64 "\n", tm_wheel->current_index + (uint64_t) advance_index);
//     //printf("advance_index1:%" PRId64 "\n", advance_index);

//     while(advance_index > 0){ //walk the wheel ticks until now, advance_index= now - last_access_time in us
//         timer_wheel_slot current_slot = tm_wheel->wheel[tm_wheel->current_index];
//         //printf("current_index:%" PRIu32 "\n", tm_wheel->current_index);
//         //printf("current_slot.event_head %p", current_slot.event_head);
//         if(current_slot.event_head != NULL){ // there is at least one event in this slot
//             printf("\ncurrent_slot.event_head %p\n", current_slot.event_head);
//             rto_timer_event* event = current_slot.event_head;
//             int once = 1;
//             while(event->next_event !=  NULL || once ){ //walk the linked-list of timers
//                 if(!once)
//                     event = event->next_event;
//                 // TODO: does this "req_duration" even make sense?
//                 //uint64_t req_duration = clock_gettime_diff_us(&now, &event->send_time);
//                 //printf("req_duration:%" PRIu64 "\n", req_duration);
//                 //if(rto_interval < req_duration){
//                 if(event->received_index > tm_wheel->current_index){
//                     printf("retransmit!\n");
//                     //let's retransmit!
//                     struct sockaddr_in serv_addr;
//                     serv_addr.sin_addr.s_addr = event->send_header->alt_dst_ip; 
//                     serv_addr.sin_port = event->send_header->dst_port;
//                     int serv_addr_len = sizeof(serv_addr);
//                     //numBytes = sendto(event->fd, (void*) event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &serv_addr, (socklen_t) serv_addr_len);            
//                     ssize_t send_bytes = 0;
//                     printf("event finished after rto_interval\n");
//                     // while(send_bytes < sizeof(alt_header)){
//                     //         ssize_t numBytes = sendto(event->fd, (void*) event->send_header, sizeof(alt_header), 0, (struct sockaddr *) &serv_addr, (socklen_t) serv_addr_len);
//                     //     if (numBytes < 0){
//                     //         printf("send() failed\n");
//                     //         exit(1);
//                     //     }
//                     //     else{
//                     //         send_bytes = send_bytes + numBytes;
//                     //         //printf("send:%zd\n", numBytes);
//                     //     }
//                     // }
//                     //TODO: setup RTO again, now RTO is pre-set and fixed
//                     //event->rto_interval = event->rto_interval;
//                     //an update 99-percentile RTT can be used for a dynamic RTO value?             

//                     //schedule_event_timer_wheel(tm_wheel, event, scheduled_index);
//                 }
//                 else{
//                     printf("event finished within rto_interval\n");
//                     // return the timer back to its pool
//                     if( reclaim_timer_to_pool(pool, event) < 0){
//                         printf("timer recliamation errors\n");
//                     }
//                     // TODO: update per thread completed_req?
//                 }                            
//                 once = 0;
//             }
//         }
//         else{
//            printf("-"); 
//         }
//         advance_index--;
//         tm_wheel->current_index = (tm_wheel->current_index + 1) % tm_wheel->wheel_tick_size;
//     }
//     printf("\nadvance_index2:%" PRIu64 "\n", advance_index);
// }