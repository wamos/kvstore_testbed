gcc -O0 tcp_mtclient.c -o client -lrt -pthread
gcc -O2 tcp_epollserver.c -o server -lrt
