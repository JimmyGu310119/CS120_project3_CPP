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
        String typeStr = (frame.type == Config::PING) ? "PING" : 
                         (frame.type == Config::PONG) ? "PONG" : "Unknown";
        
        String msg = "RX << " + typeStr + " from " + IPType2Str(frame.ip) + ": " + String(frame.body.c_str());
        log(msg);

        // 如果收到 PING，自动回复 PONG
        if (frame.type == Config::PING) {
            // 构造回复帧
            // 交换 src/dst ip (虽然这里我们还没填 src ip，简单起见原样发回)
            FrameType reply(Config::PONG, frame.ip, 0, frame.body);
            
            if (writer) {
                // 稍微延时一点点再发，避免冲突 (因为我们还没做 CSMA)
                Thread::sleep(100); 
                writer->send(reply);
                log("TX >> Auto-Reply PONG");
            }
        }
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