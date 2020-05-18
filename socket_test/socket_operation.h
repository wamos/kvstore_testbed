#include <stdint.h>
#include <string.h>
#include <sys/select.h>
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
