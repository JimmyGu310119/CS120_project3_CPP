#pragma once

// 必须先包含 winsock2
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iostream>
#include <vector>
#include <functional>
#include <JuceHeader.h> 

#include "ipv4_header.h" 

// 抓包回调定义
using SniffCallback = std::function<void(const uint8_t* data, int len)>;

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
        WSACleanup();
    }

    // [新增] 发送函数：直接通过 Raw Socket 把包发给手机
    bool sendPacket(const uint8_t* data, int len) {
        if (len < sizeof(IPv4Header)) return false;

        // 1. 创建临时的发送 Socket
        // IPPROTO_IP 配合 IP_HDRINCL 选项，允许我们自己构造 IP 头
        SOCKET sendSock = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
        if (sendSock == INVALID_SOCKET) {
            DBG("Sniffer Send: Failed to create socket");
            return false;
        }

        // 2. 开启 IP_HDRINCL (IP Header Included)
        // 意思就是：数据包里的前20字节已经是 IP 头了，操作系统不要再给我加一个 IP 头
        BOOL option = TRUE;
        if (setsockopt(sendSock, IPPROTO_IP, IP_HDRINCL, (char*)&option, sizeof(option)) == SOCKET_ERROR) {
            DBG("Sniffer Send: Failed to set IP_HDRINCL");
            closesocket(sendSock);
            return false;
        }

        // 3. 解析目的 IP (为了填 sendto 的地址)
        IPv4Header* ip = (IPv4Header*)data;
        sockaddr_in dest;
        dest.sin_family = AF_INET;
        dest.sin_port = 0; // Raw Socket 不需要端口
        dest.sin_addr.s_addr = ip->dst_ip; // 直接使用包里的目的 IP (即手机 IP)

        // 4. 发送！
        int sent = sendto(sendSock, (const char*)data, len, 0, (sockaddr*)&dest, sizeof(dest));
        
        bool success = (sent == len);
        if (!success) {
            DBG("Sniffer Send: sendto failed. Error: " + String(WSAGetLastError()));
        }

        closesocket(sendSock);
        return success;
    }

    void run() override {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        sock = socket(AF_INET, SOCK_RAW, IPPROTO_IP);
        if (sock == INVALID_SOCKET) return;

        sockaddr_in sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(0);
        inet_pton(AF_INET, ipStr.c_str(), &sa.sin_addr);

        if (bind(sock, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
            DBG("Sniffer Bind Failed: " + String(ipStr));
            closesocket(sock);
            return;
        }

        DWORD dwValue = 1; 
        ioctlsocket(sock, SIO_RCVALL, &dwValue);

        DBG("Sniffer: STARTING LOOP on " + String(ipStr));

        char buffer[65535];
        while (!threadShouldExit()) {
            int len = recv(sock, buffer, sizeof(buffer), 0);
            if (len > 0) {
                if (len < sizeof(IPv4Header)) continue;
                IPv4Header* ip = (IPv4Header*)buffer;
                
                // 过滤：只关心发给 192.168.1.1 的 ICMP 包
                uint32_t dstIP = ntohl(ip->dst_ip);
                if (dstIP == 0xC0A80101 && ip->protocol == 1) { // 192.168.1.1
                    // DBG(">>> MATCH! Forwarding to Audio...");
                    onPacketReceived((uint8_t*)buffer, len);
                }
            }
        }
    }

private:
    SOCKET sock = INVALID_SOCKET;
    std::string ipStr;
    SniffCallback onPacketReceived;
};