#ifndef MULTI_DEST_HEADER_H
#define MULTI_DEST_HEADER_H

typedef struct __attribute__((__packed__)) {
  uint16_t service_id;    // Type of Service.
  uint32_t request_id;    // Request identifier.
  uint16_t packet_id;     // Packet identifier.
  uint16_t options;       // Options (could be request length etc.).
  in_addr_t alt_dst_ip;
  in_addr_t alt_dst_ip2;
} alt_header;

#endif //MULTI_DEST_HEADER_H