#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct ICMPHeader {
    uint8_t type;       // Type (8 for Request, 0 for Reply)
    uint8_t code;       // Code (Usually 0)
    uint16_t checksum;  // Checksum
    uint16_t id;        // Identifier
    uint16_t sequence;  // Sequence Number
};

#pragma pack(pop)

// 定义常量方便使用
#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8
#define PROTOCOL_ICMP          1