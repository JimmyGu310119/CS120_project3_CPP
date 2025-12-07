#pragma once
#include "utils.h"
#include <JuceHeader.h>
#include <cassert>
#include <queue>

// 阈值
constexpr float PREAMBLE_THRESHOLD = 0.3f;

class Reader : public Thread {
    // 简单的判决逻辑
    static int judgeBit(float signal1, float signal2) {
        if (signal1 - signal2 > PREAMBLE_THRESHOLD) return 1;
        else if (signal2 - signal1 > PREAMBLE_THRESHOLD) return 0;
        else return -1; // 信号太弱或相位不对
    }

public:
    Reader(std::queue<float> *bufferIn, CriticalSection *lockInput, ProcessorType processFunc)
        : Thread("Reader"), input(bufferIn), protectInput(lockInput), process(std::move(processFunc)) {
    }

    ~Reader() override { 
        signalThreadShouldExit(); 
        waitForThreadToExit(1000);
    }

    // 读取一个字节
    char readByte() {
        float buffer[LENGTH_OF_ONE_BIT];
        char byte = 0;
        int bufferPos = 0, bitPos = 0;
        
        while (!threadShouldExit()) {
            protectInput->enter();
            if (input->empty()) {
                protectInput->exit();
                // 稍微休眠一下避免死循环占满 CPU
                wait(1); 
                continue;
            }
            buffer[bufferPos] = input->front();
            input->pop();
            protectInput->exit();

            if (++bufferPos == LENGTH_OF_ONE_BIT) {
                int bit = judgeBit(buffer[0], buffer[2]);
                if (bit == -1) { 
                    // 滑动窗口：丢弃最早的一个采样，尝试重新对齐
                    for (int i = 1; i < LENGTH_OF_ONE_BIT; ++i) buffer[i - 1] = buffer[i];
                    --bufferPos;
                    continue;
                }
                bufferPos = 0;
                byte = (char) (byte | (bit << bitPos));
                if (++bitPos == 8) break;
            }
        }
        return byte;
    }

    template<class T>
    void readObject(T &object) {
        for (size_t i = 0; i < sizeof(object); ++i) ((char *) &object)[i] = readByte();
    }

    // 等待前导码
    void waitForPreamble() {
        auto sync = std::deque<float>(LENGTH_PREAMBLE * 8 * LENGTH_OF_ONE_BIT, 0);
        while (!threadShouldExit()) {
            protectInput->enter();
            if (input->empty()) {
                protectInput->exit();
                wait(1);
                continue;
            }
            sync.pop_front();
            sync.push_back(input->front());
            input->pop();
            protectInput->exit();

            bool isPreamble = true;
            for (unsigned i = 0; isPreamble && i < 8 * LENGTH_PREAMBLE; ++i) {
                // 检查是否匹配 0x55, 0x55, 0x54
                isPreamble = (preamble[i / 8] >> (i % 8) & 1) == judgeBit(sync[i * LENGTH_OF_ONE_BIT], sync[i * LENGTH_OF_ONE_BIT + 2]);
            }
            if (isPreamble) return;
        }
    }

    void run() override {
        while (!threadShouldExit()) {
            waitForPreamble();
            if (threadShouldExit()) break;

            FrameType frame;
            // 反序列化头部
            readObject(frame.len);
            readObject(frame.type);
            readObject(frame.ip);
            readObject(frame.port);

            if (frame.len > MAX_LENGTH_BODY) {
                DBG("Wrong length: " + String(frame.len));
                continue;
            }

            // 读取 Body
            for (int i = 0; i < frame.len; ++i) { 
                frame.body.push_back(readByte()); 
            }


            // 读取 CRC
            unsigned int crcRead;
            readObject(crcRead);

            // 校验
            if (crcRead == frame.crc()) {
                DBG("DEBUG: CRC OK! Handing to process...");
                process(frame);
            } else {
                // 重点：看看是不是 CRC 挂了
                DBG("DEBUG: CRC FAIL! Read: " + String(crcRead) + " Calc: " + String(frame.crc()) + " Len: " + String(frame.len));
            }
        }
    }

private:
    std::queue<float> *input;
    CriticalSection *protectInput;
    ProcessorType process;
};