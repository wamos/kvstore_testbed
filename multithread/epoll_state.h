#ifndef EPOLL_STATE_H
#define EPOLL_STATE_H

#include <sys/epoll.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct epollState {
    int epoll_fd;
    struct epoll_event *events;
} epollState;

static int CreateEpoll(epollState* state, int setsize){
    state->events = (struct epoll_event *) malloc(sizeof(struct epoll_event)* (uint64_t) setsize);
    if(state->events == NULL){        
        return -1;
    }
    state->epoll_fd = epoll_create(1024);
    if(state->epoll_fd == -1){        
        free(state->events);
        return -1;
    }
    else{
        printf("epoll_fd:%d\n", state->epoll_fd);
    } 
    return 0;
}

static int CloseEpoll(epollState* state){
    if(state != NULL){
        close(state->epoll_fd);
        printf("free epoll state->events\n");
        free(state->events);
        return 0;
    }
    else
        return -1;
}


static int AddEpollEvent(epollState* state, int fd, uint32_t event){
    struct epoll_event ee = {0};
    int op = EPOLL_CTL_ADD;
    ee.data.fd = fd;
    ee.events = event;

    if (epoll_ctl(state->epoll_fd,op,fd,&ee) == -1) 
        return -1;
    
    return 0;
}


static int AddEpollEventWithData(epollState* state, int fd, uint32_t u32, uint32_t event){
    struct epoll_event ee = {0};
    int op = EPOLL_CTL_ADD;
    ee.data.fd = fd;
    ee.events = event;
    ee.data.u32 = u32;

    if (epoll_ctl(state->epoll_fd,op,fd,&ee) == -1) 
        return -1;
    
    return 0;
}


static int RemoveEpollEvent(int fd, epollState* state, uint32_t del_event){
    struct epoll_event ee = {0};
    int op = EPOLL_CTL_MOD;
    ee.data.fd = fd;
    ee.events = del_event;

    if (epoll_ctl(state->epoll_fd,op,fd,&ee) == -1) 
        return -1;
    
    return 0;
}

#endif //EPOLL_STATE_H