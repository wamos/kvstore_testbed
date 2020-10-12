#gcc -O2 tcp_server.c -o tcp_server -lrt
#gcc -O2 tcp_client.c -o tcp_client -lrt
gcc -O2 udp_server.c -o udp_server -lrt #-pthread
gcc -O2 udp_client.c -o udp_client -lrt

gcc -O2 -pthread udp_server_probetest.c -o udp_server_probetest -lrt
