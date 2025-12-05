#pragma once
#include <cstdint>

#pragma pack(push, 1) // 强制 1 字节对齐，防止编译器加 padding

struct IPv4Header {
    uint8_t ihl : 4;      // Header Length (4 bits)
    uint8_t version : 4;  // Version (4 bits)
    uint8_t tos;          // Type of Service
    uint16_t total_length;// Total Length
    uint16_t id;          // Identification
    uint16_t flags_offset;// Flags (3 bits) + Fragment Offset (13 bits)
    uint8_t ttl;          // Time to Live
    uint8_t protocol;     // Protocol (ICMP = 1)
    uint16_t checksum;    // Header Checksum
    uint32_t src_ip;      // Source IP Address
    uint32_t dst_ip;      // Destination IP Address
};

#pragma pack(pop)