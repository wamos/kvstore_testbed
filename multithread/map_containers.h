#ifndef MAP_CONTAINER_H
#define MAP_CONTAINER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void map_insert(uint32_t key, uint32_t value);
uint32_t map_lookup(uint32_t key);
void map_clear();
int map_getsize();

#ifdef __cplusplus
}
#endif

#endif