#include "utils.h"
#include <JuceHeader.h> // 需要 JUCE 的 IPAddress 类

// 手写 CRC32
unsigned int crc32(const char *src, size_t srcSize) {
    unsigned int crc = 0xFFFFFFFF;
    const unsigned char* p = (const unsigned char*)src;
    for (size_t i = 0; i < srcSize; ++i) {
        crc ^= p[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

IPType Str2IPType(const std::string &ip) {
    juce::IPAddress tmp(ip);
    IPType ret = 0;
    for (int i = 0; i < 4; ++i) ret = ret << 8 | tmp.address[i];
    return ret;
}

std::string IPType2Str(IPType ip) {
    juce::IPAddress tmp(ip);
    return tmp.toString().toStdString();
}