// pipe_client.h —— 外置前端到 trainer.dll 的命名管道客户端
//
// 前端只在管道线程里做 I/O；解析出的每行/连接状态通过 PostMessage 交给 UI 线程。
// 协议见 docs/architecture-frontend.md：JSON Lines / UTF-8 / 单客户端。
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

constexpr UINT WM_APP_PIPE = WM_APP + 1;

struct PipeMsg {
    enum Type { Line, Connected, Disconnected };
    Type type = Line;
    std::string text;
};

class PipeClient {
public:
    PipeClient() = default;
    ~PipeClient();

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    void Start(HWND notifyWnd, UINT notifyMsg);
    void Stop();

    // UI 线程调用；只入队，不阻塞。未连接时返回 false。
    bool Send(const std::string& line);
    bool connected() const { return connected_.load(); }

private:
    void ThreadMain();
    bool Connect();
    void Disconnect();
    void FlushOutbound();
    void ReadAvailable();
    void Post(PipeMsg::Type type, std::string text = {});
    void ClearOutbound();

    HWND hwnd_ = nullptr;
    UINT msg_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    HANDLE pipe_ = INVALID_HANDLE_VALUE;

    std::mutex outMu_;
    std::deque<std::string> outbound_;
    std::string inbound_;
};
