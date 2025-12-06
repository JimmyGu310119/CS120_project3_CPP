#pragma once

// [重要] 防止 Windows.h 包含旧的 WinSock.h 导致冲突
#define WIN32_LEAN_AND_MEAN 

#include "include/config.h"
#include "include/reader.h"
#include "include/writer.h"
#include "include/utils.h"
#include "include/ipv4_header.h"
#include "include/icmp_header.h"
#include <JuceHeader.h>
#include <winsock2.h> // 提供 htons, ntohs 等函数
#include <queue>
#include <map>

// [修改] 继承 Timer 以实现自动 Ping 10 次
class MainContentComponent : public juce::AudioAppComponent, public juce::Timer {
public:
    MainContentComponent() {
        // UI 初始化
        setSize(600, 400);
        
        statusLabel.setText("Status: Ready", juce::NotificationType::dontSendNotification);
        statusLabel.setBounds(10, 10, 580, 30);
        addAndMakeVisible(statusLabel);

        logEditor.setMultiLine(true);
        logEditor.setReadOnly(true);
        logEditor.setBounds(10, 50, 580, 250);
        addAndMakeVisible(logEditor);

        // Ping 按钮
        pingButton.setButtonText("Ping 10 Times"); // 改名了
        pingButton.setBounds(200, 320, 200, 50);
        pingButton.onClick = [this] { startPingTest(); };
        addAndMakeVisible(pingButton);

        // 音频设置
        setAudioChannels(1, 1);
    }

    ~MainContentComponent() override { 
        stopTimer(); // 记得停止定时器
        shutdownAudio(); 
    }

    // [新增] 定时器回调：每秒触发一次发送
    void timerCallback() override {
        if (pingCounter < 10) {
            pingCounter++;
            // 发送给 192.168.1.2 (假设对方是 .2，如果你是Node2，这里要改)
            sendPing("192.168.1.2", pingCounter); 
        } else {
            stopTimer();
            log("\n--- Ping statistics ---");
            log("10 packets transmitted.");
        }
    }

private:
    // 记录发送时间：Seq -> Time
    std::map<uint16_t, std::chrono::steady_clock::time_point> pingSentTime;
    int pingCounter = 0; // 计数器

    void startPingTest() {
        pingCounter = 0;
        log("\n--- Pinging 192.168.1.2 with 32 bytes of data ---");
        startTimer(1000); // 启动定时器，间隔 1000ms
    }

    void log(const String& msg) {
        MessageManager::callAsync([this, msg]() {
            logEditor.moveCaretToEnd();
            logEditor.insertTextAtCaret(msg + "\n");
            statusLabel.setText(msg, juce::NotificationType::dontSendNotification);
        });
    }

    // 标准 Checksum 算法
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

    // 发送 Ping (Echo Request)
    void sendPing(String targetIPStr, int seq) {
        const int payloadSize = 32;
        const int totalSize = sizeof(IPv4Header) + sizeof(ICMPHeader) + payloadSize;
        std::vector<uint8_t> buffer(totalSize, 0);

        // 1. IP Header
        IPv4Header* ip = (IPv4Header*)buffer.data();
        ip->version = 4;
        ip->ihl = 5;
        ip->tos = 0;
        ip->total_length = htons(totalSize);
        ip->id = htons(12345);
        ip->flags_offset = 0;
        ip->ttl = 64;
        ip->protocol = 1; // ICMP
        
        // 注意：如果你是 Node2，这里 src 应该填 1.2，dst 填 1.1
        // 这里默认写死你是 Node1 (1.1) -> Node2 (1.2)
        ip->src_ip = htonl(Str2IPType("192.168.1.1")); 
        ip->dst_ip = htonl(Str2IPType(targetIPStr.toStdString())); 
        
        ip->checksum = 0;
        ip->checksum = calculateChecksum(ip, sizeof(IPv4Header));

        // 2. ICMP Header
        ICMPHeader* icmp = (ICMPHeader*)(buffer.data() + sizeof(IPv4Header));
        icmp->type = 8; // Echo Request
        icmp->code = 0;
        icmp->id = htons(1);      
        icmp->sequence = htons((uint16_t)seq);
        
        // 3. Payload
        uint8_t* payload = buffer.data() + sizeof(IPv4Header) + sizeof(ICMPHeader);
        for(int i=0; i<payloadSize; ++i) payload[i] = (uint8_t)('a' + (i % 26));

        // ICMP Checksum
        icmp->checksum = 0;
        icmp->checksum = calculateChecksum(icmp, sizeof(ICMPHeader) + payloadSize);

        // 4. Send Frame
        std::string rawData((char*)buffer.data(), buffer.size());
        FrameType frame(Config::PING, 0, 0, rawData); 
        
        if (writer) {
            // 记录发送时间
            pingSentTime[(uint16_t)seq] = std::chrono::steady_clock::now();
            writer->send(frame);
            // 本地就不打印发送日志了，刷屏不好看，只打印接收
        }
    }

    // 构造并发送 Reply (Echo Reply)
    void sendEchoReply(const IPv4Header* srcIPHead, const ICMPHeader* srcICMPHead, const uint8_t* payload, int payloadLen) {
        int totalSize = sizeof(IPv4Header) + sizeof(ICMPHeader) + payloadLen;
        std::vector<uint8_t> buffer(totalSize, 0);

        // IP
        IPv4Header* ip = (IPv4Header*)buffer.data();
        *ip = *srcIPHead;
        ip->src_ip = srcIPHead->dst_ip; // 交换 IP
        ip->dst_ip = srcIPHead->src_ip;
        ip->checksum = 0;
        ip->checksum = calculateChecksum(ip, sizeof(IPv4Header));

        // ICMP
        ICMPHeader* icmp = (ICMPHeader*)(buffer.data() + sizeof(IPv4Header));
        *icmp = *srcICMPHead;
        icmp->type = 0; // Echo Reply
        icmp->checksum = 0;
        
        // Payload
        uint8_t* destPayload = buffer.data() + sizeof(IPv4Header) + sizeof(ICMPHeader);
        memcpy(destPayload, payload, payloadLen);

        // ICMP Checksum
        icmp->checksum = calculateChecksum(icmp, sizeof(ICMPHeader) + payloadLen);

        // Send
        std::string rawData((char*)buffer.data(), buffer.size());
        FrameType frame(Config::PING, 0, 0, rawData);
        
        if (writer) {
            //Thread::sleep(50); // 稍微避让
            writer->send(frame);
            // log("TX >> Auto-Reply PONG");
        }
    }

    // 接收处理
    void processFrame(FrameType& frame) {
        // 尝试解析 IP 头
        if (frame.body.size() >= sizeof(IPv4Header)) {
            IPv4Header* ipHeader = (IPv4Header*)frame.body.data();
            
            if (ipHeader->version == 4) {
                String srcIP = IPType2Str(ntohl(ipHeader->src_ip));
                
                // 解析 ICMP
                if (ipHeader->protocol == 1 && frame.body.size() >= sizeof(IPv4Header) + sizeof(ICMPHeader)) {
                    ICMPHeader* icmp = (ICMPHeader*)(frame.body.data() + sizeof(IPv4Header));
                    uint16_t seq = ntohs(icmp->sequence);

                    if (icmp->type == 8) {
                        // 收到 Ping -> 回复 Pong
                        // log("RX << Echo Request from " + srcIP + " seq=" + String(seq));
                        uint8_t* payloadPtr = (uint8_t*)icmp + sizeof(ICMPHeader);
                        int payloadLen = frame.body.size() - sizeof(IPv4Header) - sizeof(ICMPHeader);
                        sendEchoReply(ipHeader, icmp, payloadPtr, payloadLen);
                        
                    } else if (icmp->type == 0) {
                        // 收到 Pong -> 计算时间
                        if (pingSentTime.count(seq)) {
                            auto now = std::chrono::steady_clock::now();
                            auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(now - pingSentTime[seq]).count();
                            
                            // 打印标准 Ping 格式
                            String logMsg = "Reply from " + srcIP + 
                                            ": bytes=" + String(frame.body.size() - sizeof(IPv4Header) - sizeof(ICMPHeader)) +
                                            " time=" + String(rtt) + "ms TTL=" + String(ipHeader->ttl) +
                                            " seq=" + String(seq);
                            log(logMsg);
                            
                            pingSentTime.erase(seq);
                        }
                    }
                }
                return; 
            }
        }
        // 如果不是 IP 包
        log("RX << Unknown Frame");
    }

    // === JUCE Audio Boilerplate ===
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
        AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        setup.bufferSize = 144; // 144 samples @ 48kHz = 3ms 延迟
        setup.sampleRate = 48000;
        deviceManager.setAudioDeviceSetup(setup, true);
        reader = std::make_unique<Reader>(&directInput, &directInputLock, 
            [this](FrameType& f) { processFrame(f); });
        reader->startThread();
        writer = std::make_unique<Writer>(&directOutput, &directOutputLock);
        log("Audio Initialized. Rate: " + String(sampleRate));
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override {
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};