#pragma once

// [修正] 必须先包含 winsock2.h，再包含 windows.h 和 iphlpapi.h
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <vector>
#include <string>
#include <iostream>

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "ws2_32.lib") // 确保链接 winsock

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

        uint32_t status = 1;
        DWORD len;
        if (!DeviceIoControl(handle, TAP_WIN_IOCTL_SET_MEDIA_STATUS, &status, sizeof(status), &status, sizeof(status), &len, NULL)) {
            std::cerr << "[TAP] Warning: Could not set media status" << std::endl;
        }

        std::cerr << "[TAP] Successfully opened " << adapterName << std::endl;
        return true;
    }

    int read(void* buffer, int bufferSize) {
        if (handle == INVALID_HANDLE_VALUE) return 0;
        
        DWORD readBytes = 0;
        OVERLAPPED ol = {0};
        ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!ReadFile(handle, buffer, bufferSize, &readBytes, &ol)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ol.hEvent, 1) == WAIT_OBJECT_0) {
                    GetOverlappedResult(handle, &ol, &readBytes, FALSE);
                } else {
                    CancelIo(handle);
                }
            }
        }
        CloseHandle(ol.hEvent);
        return readBytes;
    }

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
                std::string sName(wName.begin(), wName.end());
                if (sName == name) {
                    std::string adapterName = pCurr->AdapterName;
                    free(pAddresses);
                    return adapterName;
                }
            }
        }
        free(pAddresses);
        return "";
    }
};