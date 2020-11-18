#ifndef ALT_HEADER_H
#define ALT_HEADER_H

#define HOST_PER_RACK 2
#define NUM_REPLICA 3

struct alt_header {
  // 1 + 1 + 2 + 4 = 8 bytes
  uint8_t  msgtype_flags;
  uint8_t  redirection;
  uint8_t  header_size; // how long the whole header is, HOST_PER_RACK is not a fixed value
  uint8_t  reserved;

  //4 bytes
  uint16_t feedback_options;	
  uint16_t service_id;    // Type of Service.
  uint32_t request_id;    // Request identifier
  
  // 8 bytes + 12 bytes = 20 bytes
  uint32_t alt_dst_ip; // default destination
  uint32_t actual_src_ip;
  uint32_t replica_dst_list[NUM_REPLICA];
  // stay here for testing older version!
  //uint32_t alt_dst_ip2;
  //uint32_t alt_dst_ip3;  

  //load information appended here!
  uint16_t service_id_list[HOST_PER_RACK];   // 20 bytes
  uint32_t host_ip_list[HOST_PER_RACK];     // 40 bytes
  uint16_t host_queue_depth[HOST_PER_RACK]; // 20 bytes
} __attribute__((__packed__)); // or use __rte_packed

enum {
  SINGLE_PKT_REQ = 0, // default is doing replica selection
  SINGLE_PKT_REQ_PASSTHROUGH,
  SINGLE_PKT_RESP_PIGGYBACK,
  SINGLE_PKT_RESP_PASSTHROUGH,  
  HOST_FEEDBACK_MSG,
  SWITCH_FEEDBACK_MSG,
  MULTI_PKT_REQ,
  MULTI_PKT_RESP_PIGGYBACK,
  MULTI_PKT_RESP_PASSTHROUGH,  
  CONTROL_MSG_ROUTING_UPDATE,  // for updating routing table at runtime
  CONTROL_MSG_SWITCH_ADDITION, // for adding addtional switch and servers at runtime
  CONTROL_MSG_NODE_ADDITION,   // for adding addiotnal node to a switch at runtime
};


#endif //ALT_HEADER_H