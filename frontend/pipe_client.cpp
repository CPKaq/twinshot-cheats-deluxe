// pipe_client.cpp —— 命名管道客户端实现
#include "pipe_client.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\twinshot-trainer";
constexpr size_t kMaxOutbound = 64;
constexpr size_t kMaxInbound = 64 * 1024;
} // namespace

PipeClient::~PipeClient() {
    Stop();
}

void PipeClient::Start(HWND notifyWnd, UINT notifyMsg) {
    if (running_.load()) return;
    hwnd_ = notifyWnd;
    msg_ = notifyMsg;
    running_.store(true);
    thread_ = std::thread(&PipeClient::ThreadMain, this);
}

void PipeClient::Stop() {
    running_.store(false);
    if (thread_.joinable()) {
        // 如果管道线程正卡在 ReadFile/WriteFile，取消它的同步 I/O 后再 join。
        CancelSynchronousIo(reinterpret_cast<HANDLE>(thread_.native_handle()));
        thread_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    connected_.store(false);
}

bool PipeClient::Send(const std::string& line) {
    if (!connected_.load()) return false;
    std::lock_guard<std::mutex> lock(outMu_);
    if (outbound_.size() >= kMaxOutbound) outbound_.pop_front();
    outbound_.push_back(line);
    return true;
}

void PipeClient::Post(PipeMsg::Type type, std::string text) {
    if (!hwnd_ || !msg_ || !IsWindow(hwnd_)) return;
    auto* msg = new PipeMsg{type, std::move(text)};
    if (!PostMessageW(hwnd_, msg_, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
}

bool PipeClient::Connect() {
    HANDLE p = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (p == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(kPipeName, 200);
        return false;
    }

    pipe_ = p;
    connected_.store(true);
    inbound_.clear();
    ClearOutbound();
    Post(PipeMsg::Connected);
    return true;
}

void PipeClient::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    bool wasConnected = connected_.exchange(false);
    inbound_.clear();
    ClearOutbound();
    if (wasConnected) Post(PipeMsg::Disconnected);
}

void PipeClient::ClearOutbound() {
    std::lock_guard<std::mutex> lock(outMu_);
    outbound_.clear();
}

void PipeClient::FlushOutbound() {
    for (;;) {
        std::string line;
        {
            std::lock_guard<std::mutex> lock(outMu_);
            if (outbound_.empty()) return;
            line = std::move(outbound_.front());
            outbound_.pop_front();
        }

        size_t offset = 0;
        while (offset < line.size()) {
            DWORD written = 0;
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(line.size() - offset, 4096));
            if (!WriteFile(pipe_, line.data() + offset, chunk, &written, nullptr) ||
                written == 0) {
                Disconnect();
                return;
            }
            offset += written;
        }
    }
}

void PipeClient::ReadAvailable() {
    DWORD avail = 0;
    if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) {
        Disconnect();
        return;
    }
    if (avail == 0) return;

    char buf[4096];
    DWORD toRead = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(buf)));
    DWORD read = 0;
    if (!ReadFile(pipe_, buf, toRead, &read, nullptr) || read == 0) {
        Disconnect();
        return;
    }
    inbound_.append(buf, read);

    size_t start = 0;
    for (;;) {
        size_t nl = inbound_.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = inbound_.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) Post(PipeMsg::Line, std::move(line));
    }
    if (start > 0) inbound_.erase(0, start);
    if (inbound_.size() > kMaxInbound) inbound_.clear(); // 异常对端，丢弃
}

void PipeClient::ThreadMain() {
    while (running_.load()) {
        if (!connected_.load()) {
            if (!Connect()) {
                // 等待游戏启动/注入；Stop 最多等这一次睡眠。
                for (int i = 0; i < 5 && running_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }
        }

        ReadAvailable();
        if (!connected_.load()) continue;
        FlushOutbound();
        if (!connected_.load()) continue;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    connected_.store(false);
}
