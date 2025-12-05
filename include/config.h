#pragma once

// 定义我们自定义的帧类型
namespace Config {
    constexpr unsigned char UDP = 0x01;  // 既然以前有，保留着
    constexpr unsigned char PING = 0x02; // 请求
    constexpr unsigned char PONG = 0x03; // 响应
    
    // 如果后续要做真正的 IP 转发，我们可以加一个:
    constexpr unsigned char IP_PACKET = 0x04; 
}