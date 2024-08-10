/*
* Copyright 2017 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef HelloWorld_DEFINED
#define HelloWorld_DEFINED

#include "include/core/SkScalar.h"
#include "include/core/SkPicture.h"
#include "include/core/SkTypes.h"
#include "tools/sk_app/Application.h"
#include "tools/sk_app/Window.h"
#include "tools/skui/ModifierKey.h"
#include <winsock2.h>
#include <iostream>
#pragma comment(lib, "ws2_32.lib")
#include <thread>
#include <mutex>

class SkSurface;
class SkTypeface;

class HelloWorld : public sk_app::Application, sk_app::Window::Layer {
public:
    HelloWorld(int argc, char** argv, void* platformData);
    ~HelloWorld() override;

    void onIdle() override;

    void receiveDataFromPipe(const char* data, size_t size);
    void startPipeThread();
    void stopPipeThread();
    void startMonitorThread();
    void stopMonitorThread();
    void onBackendCreated() override;
    void onPaint(SkSurface*) override;
    bool onChar(SkUnichar c, skui::ModifierKey modifiers) override;

private:
    void updateTitle();

    sk_app::Window* fWindow;
    sk_app::Window::BackendType fBackendType;
    sk_sp<SkTypeface> fTypeface;

    SkScalar fRotationAngle;
    sk_sp<SkPicture> fPicture;           // 存储反序列化后的 SkPicture
    std::thread fPipeThread;             // 处理管道数据的线程
    std::thread fMonitorThread;          // 监控流量的线程
    std::mutex fMutex;                   // 互斥锁保护 fPicture
    std::condition_variable fCondition;  // 条件变量通知主线程

    std::atomic<size_t> fDataSize;       // 记录每秒的数据流量
    bool fDataReady = false;  // 标记是否有新数据
    HANDLE hPipe;
    char buffer[1024 * 1024];
    DWORD dwRead;


};

#endif
