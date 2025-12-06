#pragma once
#include "include/config.h"
#include "include/reader.h"
#include "include/writer.h"
#include "include/utils.h"
#include "include/ipv4_header.h"
#include "include/icmp_header.h"
#include <JuceHeader.h>
#include <winsock2.h>
#include <queue>

class MainContentComponent : public juce::AudioAppComponent {
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
        pingButton.setButtonText("Send PING");
        pingButton.setBounds(200, 320, 200, 50);
        pingButton.onClick = [this] { sendPing(); };
        addAndMakeVisible(pingButton);

        // 音频设置
        setAudioChannels(1, 1);
    }

    ~MainContentComponent() override { shutdownAudio(); }

private:
    void log(const String& msg) {
        // 在主线程更新 UI
        MessageManager::callAsync([this, msg]() {
            logEditor.moveCaretToEnd();
            logEditor.insertTextAtCaret(msg + "\n");
            statusLabel.setText(msg, juce::NotificationType::dontSendNotification);
        });
    }

    // 标准的 Internet Checksum 算法
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

    // 发送 Ping 逻辑
    void sendPing() {
        // 1. 准备缓冲区 (IP头 + ICMP头 + 数据)
        const int payloadSize = 32; // 标准 Ping 通常带 32 字节数据
        const int totalSize = sizeof(IPv4Header) + sizeof(ICMPHeader) + payloadSize;
        std::vector<uint8_t> buffer(totalSize, 0);

        // 2. 填充 IP 头
        IPv4Header* ip = (IPv4Header*)buffer.data();
        ip->version = 4;
        ip->ihl = 5; // Header Length = 5 * 32bit = 20 bytes
        ip->tos = 0;
        ip->total_length = htons(totalSize);
        ip->id = htons(12345); // 随机 ID
        ip->flags_offset = 0;
        ip->ttl = 64;
        ip->protocol = 1; // 1 代表 ICMP 协议
        ip->src_ip = htonl(Str2IPType("192.168.1.1")); // 假装我是 .1
        ip->dst_ip = htonl(Str2IPType("192.168.1.2")); // 假装发给 .2
        ip->checksum = 0;
        ip->checksum = calculateChecksum(ip, sizeof(IPv4Header));

        // 3. 填充 ICMP 头 (紧跟在 IP 头后面)
        ICMPHeader* icmp = (ICMPHeader*)(buffer.data() + sizeof(IPv4Header));
        icmp->type = 8; // 8 = Echo Request (Ping 请求)
        icmp->code = 0;
        icmp->id = htons(1);      // 标识符
        icmp->sequence = htons(1);// 序列号
        
        // 填充 Payload (比如 abcdef...)
        uint8_t* payload = buffer.data() + sizeof(IPv4Header) + sizeof(ICMPHeader);
        for(int i=0; i<payloadSize; ++i) payload[i] = (uint8_t)('a' + (i % 26));

        // 计算 ICMP 校验和 (包含头和数据)
        icmp->checksum = 0;
        icmp->checksum = calculateChecksum(icmp, sizeof(ICMPHeader) + payloadSize);

        // 4. 封装进 Aethernet Frame 发送
        // 把二进制 buffer 转成 string (为了适配 FrameType)
        std::string rawData((char*)buffer.data(), buffer.size());
        
        // 使用 Config::IP_PACKET 类型 (如果你还没定义，去 config.h 加一个)
        // 或者暂时借用 Config::PING
        FrameType frame(Config::PING, 0, 0, rawData); 
        
        if (writer) {
            writer->send(frame);
            log("TX >> ICMP Echo Request (Size: " + String(totalSize) + ")");
        }
    }

    // 接收处理逻辑 (由 Reader 线程调用)
    void processFrame(FrameType& frame) {
        // 1. 尝试解析 IP 头
        if (frame.body.size() >= sizeof(IPv4Header)) {
            // 将 body 的数据强转为 IPv4Header 指针
            IPv4Header* ipHeader = (IPv4Header*)frame.body.data();
            
            // 检查版本号是否为 4 (0x45 的高4位)
            if (ipHeader->version == 4) {
                // 提取 IP 地址 (注意网络字节序转换)
                // ntohl: Network to Host Long
                String srcIP = IPType2Str(ntohl(ipHeader->src_ip));
                String dstIP = IPType2Str(ntohl(ipHeader->dst_ip));
                
                String protocol = (ipHeader->protocol == 1) ? "ICMP" : String(ipHeader->protocol);
                
                String logMsg = "RX << IPv4 Packet [" + protocol + "] " + 
                                srcIP + " -> " + dstIP + 
                                " (Len: " + String(ntohs(ipHeader->total_length)) + ")";
                log(logMsg);

                // 如果是 ICMP，进一步解析
                if (ipHeader->protocol == 1 && frame.body.size() >= sizeof(IPv4Header) + sizeof(ICMPHeader)) {
                    // 跳过 IP 头，找到 ICMP 头
                    ICMPHeader* icmp = (ICMPHeader*)(frame.body.data() + sizeof(IPv4Header));
                    
                    if (icmp->type == 8) {
                        log("   Type: Echo Request (Ping)");
                        // 自动回复逻辑... (稍后可以把自动回复也改成构建真正的 IP 包)
                    } else if (icmp->type == 0) {
                        log("   Type: Echo Reply (Pong)");
                    }
                }
                return; // 解析成功，不再打印原始乱码
            }
        }

        // 如果不是 IP 包，或者是旧的测试数据，保持原样打印
        String typeStr = (frame.type == Config::PING) ? "PING" : 
                         (frame.type == Config::PONG) ? "PONG" : "Unknown";
        String msg = "RX << Raw Frame: " + typeStr + " : " + String(frame.body.c_str());
        log(msg);
    }

    // === 音频生命周期 ===
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override {
        // 初始化 Reader 线程，传入回调函数
        reader = std::make_unique<Reader>(&directInput, &directInputLock, 
            [this](FrameType& f) { processFrame(f); });
        reader->startThread();

        // 初始化 Writer
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
                // 1. 读取麦克风 -> 输入队列
                const float* readPtr = buffer->getReadPointer(channel);
                directInputLock.enter();
                for (int i = 0; i < bufferSize; ++i) directInput.push(readPtr[i]);
                directInputLock.exit();

                // 2. 清空 Buffer 准备写入
                buffer->clear(channel, bufferToFill.startSample, bufferToFill.numSamples);

                // 3. 输出队列 -> 扬声器
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

    // 成员变量
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