#pragma once

#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <vector>
#include <string>
#include <iostream>

#pragma comment(lib, "IPHLPAPI.lib")

// TAP-Windows ioctl definitions
#define TAP_WIN_IOCTL(x) CTL_CODE(FILE_DEVICE_UNKNOWN, x, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define TAP_WIN_IOCTL_SET_MEDIA_STATUS      TAP_WIN_IOCTL(6)
#define TAP_WIN_IOCTL_CONFIG_TUN            TAP_WIN_IOCTL(10)

class Tap {
public:
    HANDLE handle = INVALID_HANDLE_VALUE;

    Tap() = default;
    
    ~Tap() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }

    // 通过名字（如 "tap0"）打开网卡
    bool open(std::string adapterName) {
        std::string guid = getAdapterGUID(adapterName);
        if (guid.empty()) {
            std::cerr << "[TAP] Error: Could not find adapter named " << adapterName << std::endl;
            return false;
        }

        std::string path = "\\\\.\\Global\\" + guid + ".tap";
        handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 
                            0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);

        if (handle == INVALID_HANDLE_VALUE) {
            std::cerr << "[TAP] Error: CreateFile failed (" << GetLastError() << ")" << std::endl;
            return false;
        }

        // 设置为“已连接”状态
        uint32_t status = 1;
        DWORD len;
        if (!DeviceIoControl(handle, TAP_WIN_IOCTL_SET_MEDIA_STATUS, &status, sizeof(status), &status, sizeof(status), &len, NULL)) {
            std::cerr << "[TAP] Warning: Could not set media status" << std::endl;
        }

        std::cerr << "[TAP] Successfully opened " << adapterName << std::endl;
        return true;
    }

    // 读取数据 (非阻塞尝试)
    int read(void* buffer, int bufferSize) {
        if (handle == INVALID_HANDLE_VALUE) return 0;
        
        DWORD readBytes = 0;
        // 注意：这里使用了同步读取，为了简单起见。如果在主线程会卡顿。
        // 但我们在 Audio 线程或者 Timer 线程读，数据量不大时通常没事。
        // 为了防止卡死，最好配合 OVERLAPPED，但为了代码极简，我们这里用 PeekNamedPipe 类似的逻辑或者直接读
        // 实际上 TAP 驱动不支持 Peek。
        // 改进策略：这里我们假设外部会在独立线程调用，或者我们只读有数据的。
        // 既然我们在 Audio Loop 里，最好不要阻塞。
        // 为了 Project 3 的简单性，我们假设 TAP 总是可读的，或者由操作系统调度。
        // 更加稳妥的方式是用 ReadFile 但不等待，这里先用最基础的阻塞读测试。
        
        OVERLAPPED ol = {0};
        ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!ReadFile(handle, buffer, bufferSize, &readBytes, &ol)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                // 等待 1ms，没数据就走
                if (WaitForSingleObject(ol.hEvent, 1) == WAIT_OBJECT_0) {
                    GetOverlappedResult(handle, &ol, &readBytes, FALSE);
                } else {
                    CancelIo(handle); // 没数据，取消
                }
            }
        }
        CloseHandle(ol.hEvent);
        return readBytes;
    }

    // 写入数据
    bool write(const void* buffer, int bufferSize) {
        if (handle == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        OVERLAPPED ol = {0};
        ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!WriteFile(handle, buffer, bufferSize, &written, &ol)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                WaitForSingleObject(ol.hEvent, INFINITE);
                GetOverlappedResult(handle, &ol, &written, FALSE);
            }
        }
        CloseHandle(ol.hEvent);
        return written == bufferSize;
    }

private:
    // 辅助函数：通过 FriendlyName 找 GUID
    std::string getAdapterGUID(const std::string& name) {
        ULONG outBufLen = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
        
        if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, pAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
        }

        if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, pAddresses, &outBufLen) == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr; pCurr = pCurr->Next) {
                std::wstring wName = pCurr->FriendlyName;
                std::string sName(wName.begin(), wName.end()); // 粗暴转换，仅限英文名
                if (sName == name) {
                    std::string adapterName = pCurr->AdapterName; // 这是 GUID
                    free(pAddresses);
                    return adapterName;
                }
            }
        }
        free(pAddresses);
        return "";
    }
};