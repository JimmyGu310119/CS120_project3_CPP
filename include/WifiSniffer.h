#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>
#include <vector>
#include <functional>
#include <JuceHeader.h> // 使用 JUCE 的 Thread

#include "ipv4_header.h" // 复用我们定义的头

// 必须链接 ws2_32.lib，CMake里已经有了

// 抓包回调函数定义：传入原始数据和长度
using SniffCallback = std::function<void(const uint8_t* data, int len)>;

// 定义 SIO_RCVALL 宏 (有些旧 SDK 可能没有)
#ifndef SIO_RCVALL
#define SIO_RCVALL _WSAIOW(IOC_VENDOR,1)
#endif

class WifiSniffer : public juce::Thread {
public:
    WifiSniffer(std::string bindIP, SniffCallback callback) 
        : Thread("WifiSniffer"), ipStr(bindIP), onPacketReceived(callback) {}

    ~WifiSniffer() override {
        signalThreadShouldExit();
        if (sock != INVALID_SOCKET) closesocket(sock);
        waitForThreadToExit(1000);
    }

    void run() override {
        // 1. 初始化 Winsock
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        // 2. 创建原始套接字 (Raw Socket)
        sock = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
        if (sock == INVALID_SOCKET) {
            DBG("Sniffer: Failed to create socket. Admin rights needed?");
            return;
        }

        // 3. 绑定到热点网卡 IP (192.168.137.1)
        sockaddr_in sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(0);
        inet_pton(AF_INET, ipStr.c_str(), &sa.sin_addr);

        if (bind(sock, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
            DBG("Sniffer: Bind failed. Check IP: " + String(ipStr));
            closesocket(sock);
            return;
        }

        // 4. 开启混杂模式 (Promiscuous Mode) - 关键！
        // 这样才能听到发往别处的包
        DWORD dwValue = 1; // 1 = On
        if (ioctlsocket(sock, SIO_RCVALL, &dwValue) == SOCKET_ERROR) {
            DBG("Sniffer: Failed to set promiscuous mode. Admin rights?");
            closesocket(sock);
            return;
        }

        DBG("Sniffer: STARTING LOOP on " + String(ipStr)); // 确认线程跑起来了

        char buffer[65535];
        while (!threadShouldExit()) {
            int len = recv(sock, buffer, sizeof(buffer), 0);
            if (len > 0) {
                if (len < sizeof(IPv4Header)) continue;

                IPv4Header* ip = (IPv4Header*)buffer;
                
                // --- DEBUG LOG START ---
                // 打印所有抓到的 ICMP 包，不管发给谁
                if (ip->protocol == 1) { 
                    uint32_t src = ntohl(ip->src_ip);
                    uint32_t dst = ntohl(ip->dst_ip);
                    
                    String srcStr = String((src >> 24) & 0xFF) + "." + String((src >> 16) & 0xFF) + "." + String((src >> 8) & 0xFF) + "." + String(src & 0xFF);
                    String dstStr = String((dst >> 24) & 0xFF) + "." + String((dst >> 16) & 0xFF) + "." + String((dst >> 8) & 0xFF) + "." + String(dst & 0xFF);
                    
                    // 在 VS Code 输出窗口打印
                    DBG("SNIFFER SEES ICMP: " + srcStr + " -> " + dstStr + " (Len: " + String(len) + ")");
                }
                // --- DEBUG LOG END ---

                // 原始过滤逻辑 (发给 192.168.1.1 的)
                uint32_t dstIP = ntohl(ip->dst_ip);
                if (dstIP == 0xC0A80101 && ip->protocol == 1) { // 192.168.1.1
                    DBG(">>> MATCH! Forwarding packet..."); // 匹配成功日志
                    onPacketReceived((uint8_t*)buffer, len);
                }
            }
            else if (len < 0) {
                // 如果 recv 报错
                // DBG("Sniffer recv failed: " + String(WSAGetLastError()));
            }
        }
        WSACleanup();
    }

private:
    SOCKET sock = INVALID_SOCKET;
    std::string ipStr;
    SniffCallback onPacketReceived;
};