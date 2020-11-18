#include "map_containers.h"
#include <stdio.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


static inline void
print_ipaddr(const char* string, uint32_t ip_addr){
	uint32_t ipaddr = ip_addr;
	uint8_t src_addr[4];
	src_addr[3] = (uint8_t) (ipaddr >> 24) & 0xff;
	src_addr[2] = (uint8_t) (ipaddr >> 16) & 0xff;
	src_addr[1] = (uint8_t) (ipaddr >> 8) & 0xff;
	src_addr[0] = (uint8_t) ipaddr & 0xff;
	printf("%s:%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n", string,
			src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
}

int main() {
    char* dest_addr_str = "10.0.0.2";
    char* tor_addr_str  = "10.0.0.18";
    char* test_str  = "255.255.255.255";
    uint32_t dest_addr = inet_addr(dest_addr_str);
    uint32_t tor_addr = inet_addr(tor_addr_str);
    uint32_t test_addr = inet_addr(test_str);
    print_ipaddr("dest_addr", dest_addr);
    print_ipaddr("tor_addr", tor_addr);
    print_ipaddr("test_addr", test_addr);

    map_insert(dest_addr,tor_addr);
    uint32_t ret_addr = map_lookup(dest_addr);
    print_ipaddr("ret_addr", ret_addr);
    ret_addr = map_lookup(test_addr);

    return 0;
}