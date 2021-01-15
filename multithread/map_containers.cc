#include <map>
#include <stdexcept>
#include <iostream> 
#include "map_containers.h"

#ifdef __cplusplus
extern "C" {
#endif
std::map<uint32_t, uint32_t> routing_map;

void map_insert(uint32_t key, uint32_t value) {
    auto kv_pair = std::make_pair(key, value);
    routing_map.insert(kv_pair);
}

uint32_t map_lookup(uint32_t key){
    uint32_t value;
    try {
        value = routing_map.at(key);
        return value;
    }
    catch (const std::out_of_range& oor){
        std::cerr << "map_lookup Out of Range error: " << oor.what() << '\n';
    }
    return 0;
}

void map_clear(){
    routing_map.clear();
}

int map_getsize(){
    return (int) routing_map.size();
}

#ifdef __cplusplus
}
#endif