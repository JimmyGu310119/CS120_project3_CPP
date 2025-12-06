#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <sstream>
#include <functional>
#include <cstring> // memcpy

// 包含标准头，防止报错
#include "ipv4_header.h"
#include "icmp_header.h"

// 帧结构常量
using LENType = unsigned char;
using TYPEType = unsigned char;
using IPType = unsigned int;
using PORTType = unsigned short;

constexpr int LENGTH_OF_ONE_BIT = 2;
constexpr int MTU = 1500;
constexpr int LENGTH_PREAMBLE = 3;
constexpr int LENGTH_CRC = 4;
// 计算 Header 长度用于校验
constexpr int FRAME_HEADER_LEN = sizeof(LENType) + sizeof(TYPEType) + sizeof(IPType) + sizeof(PORTType);
constexpr int MAX_LENGTH_BODY = MTU - LENGTH_PREAMBLE - FRAME_HEADER_LEN - LENGTH_CRC;

const std::string preamble{0x55, 0x55, 0x54};

// 函数声明
IPType Str2IPType(const std::string &ip);
std::string IPType2Str(IPType ip);
unsigned int crc32(const char *src, size_t srcSize);

// 辅助模板
template<class T>
[[nodiscard]] std::string inString(T object) {
    return std::string((const char *) &object, sizeof(T));
}

// === 核心帧结构 ===
class FrameType {
public:
    LENType len = 0;
    TYPEType type = 0;
    IPType ip = 0;
    PORTType port = 0;
    std::string body; // 存放负载数据

    FrameType() = default;

    FrameType(TYPEType nType, IPType nIp, PORTType nPort, std::string nBody) 
        : len((LENType)nBody.size()), type(nType), ip(nIp), port(nPort), body(std::move(nBody)) {}

    // 序列化：将帧头和内容拼成字符串，用于计算 CRC 和发送
    [[nodiscard]] std::string wholeString() const { 
        return inString(len) + inString(type) + inString(ip) + inString(port) + body; 
    }

    // 计算 CRC32
    [[nodiscard]] unsigned int crc() const {
        auto str = wholeString();
        return crc32(str.c_str(), str.size());
    }
};

// 计时器
using std::chrono::steady_clock;
class MyTimer {
public:
    std::chrono::time_point<steady_clock> start;
    MyTimer() : start(steady_clock::now()) {}
    void restart() { start = steady_clock::now(); }
    [[nodiscard]] double duration() const {
        auto now = steady_clock::now();
        return std::chrono::duration<double>(now - start).count();
    }
};

// 回调函数定义
using ProcessorType = std::function<void(FrameType &)>;