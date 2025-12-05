#pragma once

#include "utils.h"
#include <JuceHeader.h>
#include <cassert>
#include <queue>
#include <string>

class Writer {
public:
    // 删除默认构造函数
    Writer() = delete;
    Writer(const Writer &) = delete;
    Writer(const Writer &&) = delete;

    // 构造函数
    explicit Writer(std::queue<float> *bufferOut, CriticalSection *lockOutput) :
            output(bufferOut), protectOutput(lockOutput) {}

    void send(const FrameType &frame) {
        // 1. 构造数据包：Preamble + Frame + CRC
        // 这里的 preamble 定义在 utils.h 中
        std::string str = preamble + frame.wholeString() + inString(frame.crc());
        
        protectOutput->enter();
        
        // 2. 调制 (Modulation)
        // 逻辑：1 -> [1, 1, -1, -1], 0 -> [-1, -1, 1, 1]
        for (auto byte : str) {
            for (int bitPos = 0; bitPos < 8; ++bitPos) {
                // 取出第 bitPos 位
                if ((byte >> bitPos) & 1) {
                    output->push(1.0f);
                    output->push(1.0f);
                    output->push(-1.0f);
                    output->push(-1.0f);
                } else {
                    output->push(-1.0f);
                    output->push(-1.0f);
                    output->push(1.0f);
                    output->push(1.0f);
                }
            }
        }

        // 3. 阻塞等待发送完成 (这是原作者的关键逻辑)
        // 注意：这里的锁操作非常微妙，为了不死锁，使用了 exit -> enter 的切换
        while (!output->empty()) {
            protectOutput->exit();
            // 让出时间片给音频线程去消费数据
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            protectOutput->enter();
        }
        
        protectOutput->exit();
        
        // 打印日志方便调试
        fprintf(stderr, "\tFrame sent! %s:%u %s\n", IPType2Str(frame.ip).c_str(), frame.port, frame.body.c_str());
    }

private:
    std::queue<float> *output{nullptr};
    CriticalSection *protectOutput;
};