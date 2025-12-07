#pragma once

// [修正] 头文件包含顺序非常重要！
// 1. 先定义 WIN32_LEAN_AND_MEAN 防止 windows.h 干扰
#define WIN32_LEAN_AND_MEAN 

// 2. 必须最先包含 winsock2.h
#include <winsock2.h>
#include <windows.h>

// 3. 然后才是其他头文件
#include "include/config.h"
#include "include/reader.h"
#include "include/writer.h"
#include "include/utils.h"
#include "include/ipv4_header.h"
#include "include/icmp_header.h"
#include "include/Tap.h" // Tap.h 里也包含了 winsock2，但有 header guard 没事
#include <JuceHeader.h>
#include <queue>
#include <map>
#include "include/WifiSniffer.h"

class MainContentComponent : public juce::AudioAppComponent, public juce::Timer {
public:
    MainContentComponent() {
        setSize(600, 500); // 加高一点放新控件
        
        // 1. 状态栏
        statusLabel.setText("Status: Ready", juce::NotificationType::dontSendNotification);
        statusLabel.setBounds(10, 10, 580, 30);
        addAndMakeVisible(statusLabel);

        // 2. 身份选择器 (关键!)
        roleSelector.addItem("Node 1 (Desktop 192.168.1.1)", 1);
        roleSelector.addItem("Node 2 (Laptop 192.168.1.2 + Router)", 2);
        roleSelector.addItem("Node 3 (Phone 192.168.1.3)", 3);
        roleSelector.setSelectedId(1); // 默认是 Node 1
        roleSelector.setBounds(10, 50, 300, 30);
        roleSelector.onChange = [this] { updateRole(); };
        addAndMakeVisible(roleSelector);

        // 3. 日志框
        logEditor.setMultiLine(true);
        logEditor.setReadOnly(true);
        logEditor.setBounds(10, 90, 580, 250);
        addAndMakeVisible(logEditor);

        // 4. Ping 按钮
        pingButton.setButtonText("Ping 10 Times");
        pingButton.setBounds(200, 360, 200, 50);
        pingButton.onClick = [this] { startPingTest(); };
        addAndMakeVisible(pingButton);

        setAudioChannels(1, 1);
    }

    ~MainContentComponent() override { 
        stopTimer(); 
        shutdownAudio(); 
    }

    // === 核心逻辑：定时器 (处理 TAP 和 Ping) ===
    void timerCallback() override {
        // [Task 2] 从 TAP 网卡读取数据 (如果是路由器模式)
        // if (amIRouter) {
        //     uint8_t buffer[1500];
        //     int len = tap.read(buffer, 1500);
            
        //     if (len > 0) {
        //         // 收到系统/手机发来的 IP 包 -> 封装成 Audio Frame 发出去
        //         log("DEBUG: TAP read " + String(len) + " bytes"); 
        //         std::string rawData((char*)buffer, len);
                
        //         // 简单起见，我们假设目的 IP 就在包里，直接广播
        //         // 也可以解析一下 header 看看去哪，但广播最稳
        //         FrameType frame(Config::PING, 0, 0, rawData);
                
        //         if (writer) {
        //             writer->send(frame);
        //             // log("TAP >> Forwarded " + String(len) + " bytes to Audio");
        //         }
        //     }
        // }

        // [Task 1] 自动 Ping 逻辑
        if (isPinging && pingCounter < 10) {
            pingCounter++;
            // 根据身份决定 Ping 谁
            String target = (myIP == "192.168.1.1") ? "192.168.1.2" : "192.168.1.1";
            sendPing(target, pingCounter); 
        } else if (isPinging) {
            isPinging = false; // 停
            log("\n--- Ping statistics: Done ---");
        }
    }

private:
    // === 成员变量 ===
    Tap tap; // 虚拟网卡
    String myIP = "192.168.1.2";
    bool amIRouter = true;
    
    std::map<uint16_t, std::chrono::steady_clock::time_point> pingSentTime;
    int pingCounter = 0;
    bool isPinging = false;
    std::unique_ptr<WifiSniffer> sniffer;
    // === 辅助函数 ===
    void updateRole() {
        int id = roleSelector.getSelectedId();
        if (id == 1) {
            myIP = "192.168.1.1";
            amIRouter = false; // 台式机不是路由器
        } else if (id == 2) {
            myIP = "192.168.1.2";
            amIRouter = true;  // 笔记本是路由器
        }
        
        log("Role switched to: " + myIP + (amIRouter ? " (Router Mode)" : ""));
            if (sniffer) {
        sniffer->stopThread(2000);
        sniffer.reset();
    }
        // 尝试打开 TAP (只有 Router 需要，但为了防呆，都试一下也无妨)
        if (amIRouter) {
        // Node 2: 启动 Sniffer，监听热点网关 IP
        // 请确认你的热点网关是不是 192.168.137.1
        // 如果 ipconfig 显示是别的，请在这里修改
        sniffer = std::make_unique<WifiSniffer>("192.168.137.1", 
            [this](const uint8_t* data, int len) {
                // 这是回调函数：当抓到发往 1.1 的包时执行
                log("DEBUG: Sniffer Callback triggered! Size=" + String(len));
                // 1. 构造 Frame
                std::string rawData((char*)data, len);
                FrameType frame(Config::PING, 0, 0, rawData);
                
                // 2. 直接通过音频转发！
                if (writer) {
                    writer->send(frame);
                    log("DEBUG: Sent to Audio Writer");
                } else {
                    log("ERROR: Writer is NULL!");
                }
            });
        
        sniffer->startThread();
        log("Sniffer started on 192.168.137.1");
        }
    }

    void startPingTest() {
        pingCounter = 0;
        isPinging = true;
        // 启动定时器，10ms 检查一次 TAP，每 100 次(1秒)发一次 Ping
        // 为了简单，我们让 timerCallback 既负责读 TAP 也负责发 Ping
        // 我们可以把 startTimer 改成 10ms，然后用计数器控制 Ping 频率
        startTimer(10); 
    }
    
    // (为了配合 10ms Timer，我们需要稍微修改 timerCallback 里的 Ping 逻辑)
    // 简单起见，我们还是用 1000ms Timer 发 Ping？不行，TAP 必须快读。
    // 修正策略：Timer 设为 10ms。Ping 用一个 counter 计数。
    int tickCounter = 0;
    // (请把上面的 timerCallback 修改逻辑看准：)
    /* 
       void timerCallback() override {
           // 1. 读 TAP (每次都做)
           // ... 代码同上 ...

           // 2. 发 Ping (每 100 次 tick 做一次 = 1秒)
           tickCounter++;
           if (isPinging && tickCounter >= 100) {
               tickCounter = 0;
               if (pingCounter < 10) { ... sendPing ... } 
               else { isPinging = false; ... }
           }
       }
    */

    void log(const String& msg) {
        MessageManager::callAsync([this, msg]() {
            logEditor.moveCaretToEnd();
            logEditor.insertTextAtCaret(msg + "\n");
        });
    }

    uint16_t calculateChecksum(void* vdata, size_t length) {
        char* data = (char*)vdata;
        uint32_t acc = 0;
        for (size_t i = 0; i + 1 < length; i += 2) {
            uint16_t word;
            memcpy(&word, data + i, 2);
            acc += ntohs(word);
        }
        if (length % 2 == 1) {
            uint16_t word = 0;
            memcpy(&word, data + length - 1, 1);
            acc += ntohs(word);
        }
        while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
        return htons((uint16_t)~acc);
    }

    void sendPing(String targetIPStr, int seq) {
        const int payloadSize = 32;
        const int totalSize = sizeof(IPv4Header) + sizeof(ICMPHeader) + payloadSize;
        std::vector<uint8_t> buffer(totalSize, 0);

        IPv4Header* ip = (IPv4Header*)buffer.data();
        ip->version = 4; ip->ihl = 5; ip->ttl = 64; ip->protocol = 1;
        ip->total_length = htons(totalSize);
        ip->src_ip = htonl(Str2IPType(myIP.toStdString())); 
        ip->dst_ip = htonl(Str2IPType(targetIPStr.toStdString())); 
        ip->checksum = calculateChecksum(ip, sizeof(IPv4Header));

        ICMPHeader* icmp = (ICMPHeader*)(buffer.data() + sizeof(IPv4Header));
        icmp->type = 8; icmp->code = 0; icmp->id = htons(1); icmp->sequence = htons((uint16_t)seq);
        
        uint8_t* payload = buffer.data() + sizeof(IPv4Header) + sizeof(ICMPHeader);
        for(int i=0; i<payloadSize; ++i) payload[i] = (uint8_t)('a' + (i % 26));
        icmp->checksum = calculateChecksum(icmp, sizeof(ICMPHeader) + payloadSize);

        std::string rawData((char*)buffer.data(), buffer.size());
        FrameType frame(Config::PING, 0, 0, rawData); 
        
        if (writer) {
            pingSentTime[(uint16_t)seq] = std::chrono::steady_clock::now();
            writer->send(frame);
        }
    }

    void sendEchoReply(const IPv4Header* srcIPHead, const ICMPHeader* srcICMPHead, const uint8_t* payload, int payloadLen) {
        int totalSize = sizeof(IPv4Header) + sizeof(ICMPHeader) + payloadLen;
        std::vector<uint8_t> buffer(totalSize, 0);

        IPv4Header* ip = (IPv4Header*)buffer.data();
        *ip = *srcIPHead;
        ip->src_ip = srcIPHead->dst_ip; 
        ip->dst_ip = srcIPHead->src_ip;
        ip->ttl = 64; // 重置 TTL
        ip->checksum = 0;
        ip->checksum = calculateChecksum(ip, sizeof(IPv4Header));

        ICMPHeader* icmp = (ICMPHeader*)(buffer.data() + sizeof(IPv4Header));
        *icmp = *srcICMPHead;
        icmp->type = 0; 
        icmp->checksum = 0;
        
        uint8_t* destPayload = buffer.data() + sizeof(IPv4Header) + sizeof(ICMPHeader);
        memcpy(destPayload, payload, payloadLen);
        icmp->checksum = calculateChecksum(icmp, sizeof(ICMPHeader) + payloadLen);

        std::string rawData((char*)buffer.data(), buffer.size());
        FrameType frame(Config::PING, 0, 0, rawData);
        if (writer) writer->send(frame);
    }

    void processFrame(FrameType& frame) {
        if (frame.body.size() < sizeof(IPv4Header)) return;
        IPv4Header* ipHeader = (IPv4Header*)frame.body.data();
        if (ipHeader->version != 4) return;

        String srcIP = IPType2Str(ntohl(ipHeader->src_ip));
        String dstIP = IPType2Str(ntohl(ipHeader->dst_ip));

        // 1. 发给我的
        if (dstIP == myIP) {
            if (ipHeader->protocol == 1 && frame.body.size() >= sizeof(IPv4Header) + sizeof(ICMPHeader)) {
                ICMPHeader* icmp = (ICMPHeader*)(frame.body.data() + sizeof(IPv4Header));
                uint16_t seq = ntohs(icmp->sequence);

                if (icmp->type == 8) { // Request -> Reply
                    uint8_t* payloadPtr = (uint8_t*)icmp + sizeof(ICMPHeader);
                    int payloadLen = frame.body.size() - sizeof(IPv4Header) - sizeof(ICMPHeader);
                    sendEchoReply(ipHeader, icmp, payloadPtr, payloadLen);
                } else if (icmp->type == 0) { // Reply -> Log
                    if (pingSentTime.count(seq)) {
                        auto now = std::chrono::steady_clock::now();
                        auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(now - pingSentTime[seq]).count();
                        log("Reply from " + srcIP + ": bytes=32 time=" + String(rtt) + "ms TTL=" + String(ipHeader->ttl));
                        pingSentTime.erase(seq);
                    }
                }
            }
            return;
        }

        // 2. [Task 2] 转发逻辑 (Router)
        if (amIRouter) {
            // 写入 TAP 网卡 (交给系统去处理，比如转发给手机)
            if (tap.write(frame.body.data(), frame.body.size())) {
                log("ROUTER: Forwarded packet to System/TAP (" + srcIP + " -> " + dstIP + ")");
            }
        }
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
        AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        setup.bufferSize = 144; 
        setup.sampleRate = 48000;
        deviceManager.setAudioDeviceSetup(setup, true);

        reader = std::make_unique<Reader>(&directInput, &directInputLock, [this](FrameType& f) { processFrame(f); });
        reader->startThread();
        writer = std::make_unique<Writer>(&directOutput, &directOutputLock);
        
        // 默认初始化一下，虽然会在 updateRole 里再次初始化
        updateRole();
        log("Audio Initialized.");
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override {
        // ... (保持原样，负责搬运数据) ...
        // 直接复制你原来的 getNextAudioBlock 代码即可
        auto* device = deviceManager.getCurrentAudioDevice();
        auto activeInputChannels = device->getActiveInputChannels();
        auto activeOutputChannels = device->getActiveOutputChannels();
        auto maxInputChannels = activeInputChannels.getHighestBit() + 1;
        auto maxOutputChannels = activeOutputChannels.getHighestBit() + 1;
        auto buffer = bufferToFill.buffer;
        auto bufferSize = buffer->getNumSamples();

        for (auto channel = 0; channel < maxOutputChannels; ++channel) {
            if ((!activeInputChannels[channel] || !activeOutputChannels[channel]) || maxInputChannels == 0) {
                buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);
            } else {
                const float* readPtr = buffer->getReadPointer(channel);
                directInputLock.enter();
                for (int i = 0; i < bufferSize; ++i) directInput.push(readPtr[i]);
                directInputLock.exit();

                buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);

                float* writePtr = buffer->getWritePointer(channel);
                directOutputLock.enter();
                for (int i = 0; i < bufferSize; ++i) {
                    if (!directOutput.empty()) {
                        writePtr[i] = directOutput.front();
                        directOutput.pop();
                    } else {
                        writePtr[i] = 0.0f;
                    }
                }
                directOutputLock.exit();
            }
        }
    }

    void releaseResources() override {
        if (reader) reader->stopThread(2000);
        reader = nullptr;
        writer = nullptr;
    }

    std::unique_ptr<Reader> reader;
    std::unique_ptr<Writer> writer;
    std::queue<float> directInput;
    CriticalSection directInputLock;
    std::queue<float> directOutput;
    CriticalSection directOutputLock;

    juce::Label statusLabel;
    juce::TextEditor logEditor;
    juce::TextButton pingButton;
    juce::ComboBox roleSelector; // 新增

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};