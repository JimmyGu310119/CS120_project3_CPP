#pragma once
#include "include/config.h"
#include "include/reader.h"
#include "include/writer.h"
#include "include/utils.h"
#include <JuceHeader.h>
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

    // 发送 Ping 逻辑
    void sendPing() {
        // 构造一个 Frame
        // Type: PING
        // IP: 假定目标是 192.168.1.2 (仅仅为了填充字段，目前不用于路由)
        // Payload: "Hello Aethernet"
        std::string msg = "PingPayload-" + String(juce::Random::getSystemRandom().nextInt(100)).toStdString();
        
        FrameType frame(Config::PING, Str2IPType("192.168.1.2"), 0, msg);
        
        if (writer) {
            writer->send(frame);
            log("TX >> PING: " + String(msg));
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