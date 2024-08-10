/*
* Copyright 2017 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
#include <windows.h>
#include "example/HelloWorld.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradientShader.h"
#include "tools/fonts/FontToolUtils.h"
#include "tools/window/DisplayParams.h"


#include <string.h>
#include < tchar.h >

using namespace sk_app;
using skwindow::DisplayParams;


class SocketServer {
public:
    SocketServer(int port)
            : port_(port), ListenSocket(INVALID_SOCKET), ClientSocket(INVALID_SOCKET) {
        // 初始化 Winsock
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "WSAStartup failed with error: " << result << std::endl;
            return;
        }

        // 创建套接字
        ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ListenSocket == INVALID_SOCKET) {
            std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return;
        }

        // 绑定套接字
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port_);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        result = bind(ListenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
        if (result == SOCKET_ERROR) {
            std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            return;
        }

        // 监听连接
        result = listen(ListenSocket, SOMAXCONN);
        if (result == SOCKET_ERROR) {
            std::cerr << "listen failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            return;
        }

        std::cout << "Waiting for client to connect..." << std::endl;

    }

    ~SocketServer() {
        closesocket(ClientSocket);
        closesocket(ListenSocket);
        WSACleanup();
    }

    void accept_client() {
        // 接受连接
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            return;
        }

        std::cout << "Client connected." << std::endl;
    }

    char* receive_msg(int* len) {
        char recvbuf[1024 * 128];
        int recvbuflen = 1024 * 128;
        int result = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if (result > 0) {
            std::cout << "Bytes received: " << result << std::endl;
            std::cout << "Message: " << recvbuf << std::endl;
            *len = result;
            return recvbuf;
        } else if (result == 0) {
            std::cout << "Connection closing..." << std::endl;
        } else {
            std::cerr << "recv failed with error: " << WSAGetLastError() << std::endl;
        }
    }

private:
    WSADATA wsaData;
    SOCKET ListenSocket;
    SOCKET ClientSocket;
    sockaddr_in serverAddr;
    int port_;
};


Application* Application::Create(int argc, char** argv, void* platformData) {
    return new HelloWorld(argc, argv, platformData);
}

HelloWorld::HelloWorld(int argc, char** argv, void* platformData)
#if defined(SK_GL)
        : fBackendType(Window::kNativeGL_BackendType),
#elif defined(SK_VULKAN)
        : fBackendType(Window::kVulkan_BackendType),
#else
        : fBackendType(Window::kRaster_BackendType),
#endif
        fRotationAngle(0) {
    SkGraphics::Init();

    fWindow = Window::CreateNativeWindow(platformData);
    fWindow->setRequestedDisplayParams(DisplayParams());

    // register callbacks
    fWindow->pushLayer(this);

    fWindow->attach(fBackendType);

    fTypeface = ToolUtils::CreateTypefaceFromResource("fonts/Roboto-Regular.ttf");
    if (!fTypeface) {
        fTypeface = ToolUtils::DefaultPortableTypeface();
    }


   // Start the pipe thread
    startPipeThread();
    startMonitorThread();
}

HelloWorld::~HelloWorld() {
    stopPipeThread();
    stopMonitorThread();
    fWindow->detach();
    delete fWindow;
}

void HelloWorld::startPipeThread() {
    fPipeThread = std::thread([this]() {
        SocketServer server(8080);

        std::cout << "Waiting for client to connect..." << std::endl;
        server.accept_client();

        while (true) {
            // 服务端接收消息
            int* len = new int(0);
            char* result = server.receive_msg(len);

            // std::cout << "Client connected, waiting for message..." << std::endl;
            // 读取管道数据
            std::cout<<*len<<std::endl;
            receiveDataFromPipe(result, *len);

            // 使用互斥锁和条件变量通知主线程
            {
                std::lock_guard<std::mutex> lock(fMutex);
                fDataReady = true;
            }
            fCondition.notify_one();
        }
    });
}

void HelloWorld::stopPipeThread() {
    if (fPipeThread.joinable()) {
        fPipeThread.join();
    }
}

void HelloWorld::startMonitorThread() {
    fMonitorThread = std::thread([this]() {
        while (true) {
            // 每秒打印一次数据流量
            //std::this_thread::sleep_for(std::chrono::seconds(1));
            //size_t dataSize = fDataSize.exchange(0);
            //std::cout << "流量消耗: " << dataSize / 1024 << "kb/s" << std::endl;
        }
    });
}

void HelloWorld::stopMonitorThread() {
    if (fMonitorThread.joinable()) {
        fMonitorThread.join();
    }
}


void HelloWorld::updateTitle() {
    if (!fWindow) {
        return;
    }

    SkString title("Hello World ");
    if (Window::kRaster_BackendType == fBackendType) {
        title.append("Raster");
    } else {
#if defined(SK_GL)
        title.append("GL");
#elif defined(SK_VULKAN)
        title.append("Vulkan");
#elif defined(SK_DAWN)
        title.append("Dawn");
#else
        title.append("Unknown GPU backend");
#endif
    }

    fWindow->setTitle(title.c_str());
}

void HelloWorld::onBackendCreated() {
    this->updateTitle();
    fWindow->show();
    fWindow->inval();
}

#include "include/private/chromium/GrDeferredDisplayList.h"

#include "include/gpu/GrDirectContext.h"
#include "src/gpu/ganesh/GrDirectContextPriv.h"
#include "src/gpu/ganesh/GrRenderTargetProxy.h"
#include "src/gpu/ganesh/GrRenderTask.h"
#include "src/gpu/ganesh/surface/SkSurface_Ganesh.h"
#include "src/image/SkSurface_Base.h"

#include <utility>

GrDeferredDisplayList::GrDeferredDisplayList(const GrSurfaceCharacterization& characterization,
                                             sk_sp<GrRenderTargetProxy> targetProxy,
                                             sk_sp<LazyProxyData> lazyProxyData)
        : fCharacterization(characterization)
        , fArenas(true)
        , fTargetProxy(std::move(targetProxy))
        , fLazyProxyData(std::move(lazyProxyData)) {
    SkASSERT(fTargetProxy->isDDLTarget());
}

GrDeferredDisplayList::~GrDeferredDisplayList() {
#if defined(SK_DEBUG)
    for (auto& renderTask : fRenderTasks) {
        SkASSERT(renderTask->unique());
    }
#endif
}

GrDeferredDisplayList::ProgramIterator::ProgramIterator(GrDirectContext* dContext,
                                                        GrDeferredDisplayList* ddl)
        : fDContext(dContext), fProgramData(ddl->programData()), fIndex(0) {}

GrDeferredDisplayList::ProgramIterator::~ProgramIterator() {}

bool GrDeferredDisplayList::ProgramIterator::compile() {
    if (!fDContext || fIndex < 0 || fIndex >= (int)fProgramData.size()) {
        return false;
    }

    return fDContext->priv().compile(fProgramData[fIndex].desc(), fProgramData[fIndex].info());
}

bool GrDeferredDisplayList::ProgramIterator::done() const {
    return fIndex >= (int)fProgramData.size();
}

void GrDeferredDisplayList::ProgramIterator::next() { ++fIndex; }

namespace skgpu::ganesh {

bool DrawDDL(SkSurface* surface, sk_sp<const GrDeferredDisplayList> ddl) {
    if (!surface || !ddl) {
        return false;
    }
    auto sb = asSB(surface);
    if (!sb->isGaneshBacked()) {
        return false;
    }
    auto gs = static_cast<SkSurface_Ganesh*>(surface);
    return gs->draw(ddl);
}

bool DrawDDL(sk_sp<SkSurface> surface, sk_sp<const GrDeferredDisplayList> ddl) {
    return DrawDDL(surface.get(), ddl);
}

}

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkStream.h"
     // 将 SkImage 转换为 SkPicture
sk_sp<SkPicture> createPictureFromImage(sk_sp<SkImage> image) {
    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(image->width(), image->height());

    canvas->drawImage(image, 0, 0);

    return recorder.finishRecordingAsPicture();
}
// 序列化 SkPicture
std::vector<char> serializePicture(sk_sp<SkPicture> picture) {
    SkDynamicMemoryWStream stream;
    picture->serialize(&stream);

    std::vector<char> data(stream.bytesWritten());
    stream.copyTo(data.data());

    return data;
}

void HelloWorld::receiveDataFromPipe(const char* data, size_t size) {
    std::cout << "receive" << std::endl;
    std::lock_guard<std::mutex> lock(fMutex);
    SkMemoryStream stream(data, size);
    fPicture = SkPicture::MakeFromStream(&stream);
    if (fPicture) {
        std::cout << "Jnjjj" << std::endl;
    }
}



void HelloWorld::onPaint(SkSurface* surface) {
     auto canvas = surface->getCanvas();

    // Clear background
    // canvas->clear(SK_ColorWHITE);
    // unsigned char raw[248] = {
    //         0x50, 0xa2, 0xa1, 0xb8, 0x1c, 0x02, 0x00, 0x00, 0xe0, 0x2f, 0x90, 0xc1, 0x1c, 0x02,
    //         0x00, 0x00, 0x66, 0xf5, 0xce, 0x88, 0xfb, 0x7f, 0x00, 0x00, 0x30, 0xd6, 0x7f, 0x22,
    //         0x2f, 0x00, 0x00, 0x00, 0x30, 0xd6, 0x7f, 0x22, 0x2f, 0x00, 0x00, 0x00, 0xf8, 0xd6,
    //         0x7f, 0x22, 0x2f, 0x00, 0x00, 0x00, 0x92, 0x8a, 0xd1, 0x88, 0xfb, 0x7f, 0xaa, 0x01,
    //         0xdf, 0x93, 0x1c, 0x75, 0x81, 0x87, 0x00, 0x00, 0xf0, 0xdc, 0x7f, 0x22, 0x2f, 0x00,
    //         0x00, 0x00, 0xc0, 0xf9, 0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00, 0xa8, 0xf9, 0x0c, 0xc1,
    //         0x1c, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0xf6,
    //         0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00, 0xb0, 0xf6, 0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00,
    //         0xce, 0x96, 0xdc, 0x88, 0xfb, 0x7f, 0x00, 0x00, 0xd0, 0xb6, 0xfa, 0xc1, 0x1c, 0x02,
    //         0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    //         0x00, 0x00, 0x00, 0x00, 0x1b, 0x07, 0xdc, 0x7f, 0xfc, 0x7f, 0x00, 0x00, 0x01, 0xd7,
    //         0x7f, 0x22, 0x2f, 0x00, 0x00, 0x00, 0x80, 0xd8, 0x7f, 0x22, 0x2f, 0x00, 0x00, 0x01,
    //         0xe0, 0x2f, 0x90, 0xc1, 0x1c, 0x02, 0x00, 0x00, 0x6c, 0xfb, 0x0c, 0xc1, 0x1c, 0x02,
    //         0x00, 0x00, 0xd0, 0xd6, 0x7f, 0x22, 0x2f, 0x00, 0x00, 0x00, 0xd0, 0xd6, 0x7f, 0x22,
    //         0x2f, 0x00, 0x00, 0x00, 0x60, 0xf6, 0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00, 0x60, 0xf6,
    //         0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00, 0x6c, 0xfb, 0x0c, 0xc1, 0x1c, 0x02, 0x00, 0x00,
    //         0x19, 0x68, 0xdb, 0x87, 0xfc, 0x7f, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // };
    //GrDeferredDisplayList* mid = (GrDeferredDisplayList*)raw;
    //sk_sp<GrDeferredDisplayList> ddl = sk_sp(mid);
    //auto ddl = sk_sp<GrDeferredDisplayList>(new GrDeferredDisplayList(
    //        fCharacterization, std::move(fTargetProxy), std::move(fLazyProxyData)));

    //fContext->priv().moveRenderTasksToDDL(ddl.get());
    //skgpu::ganesh::DrawDDL(surface,ddl);


         // Clear background
     canvas->clear(SK_ColorWHITE);

     if (fPicture) {
        canvas->drawPicture(fPicture);
     } else {
        // 如果没有接收到数据，绘制一个默认的图形
        SkPaint paint;
        paint.setColor(SK_ColorRED);
        SkRect rect = SkRect::MakeXYWH(10, 10, 128, 128);
        canvas->drawRect(rect, paint);
     }
        // Clear background
        //canvas->clear(SK_ColorWHITE);

        //SkPaint paint;
        //paint.setColor(SK_ColorRED);

        //// Draw a rectangle with red paint
        //SkRect rect = SkRect::MakeXYWH(10, 10, 128, 128);
        //canvas->drawRect(rect, paint);
        //rect = SkRect::MakeXYWH(15, 15, 128, 128);
        //canvas->drawRect(rect, paint);
        //rect = SkRect::MakeXYWH(20, 20, 128, 128);
        //canvas->drawRect(rect, paint);
        //rect = SkRect::MakeXYWH(100, 100, 128, 128);
        //canvas->drawRect(rect, paint);

        //// 获取 SkImage 对象
        //sk_sp<SkImage> image = surface->makeImageSnapshot();

        //// 将 SkImage 转换为 SkPicture
        //sk_sp<SkPicture> picture1 = createPictureFromImage(image);

        //// 序列化 SkPicture
        //std::vector<char> data = serializePicture(picture1);
        //std::cout << data.data() << std::endl;
        //// 反序列化 SkPicture
        //SkMemoryStream stream(data.data(), data.size());
        //sk_sp<SkPicture> picture2 = SkPicture::MakeFromStream(&stream);
        //canvas->clear(SK_ColorWHITE);
        //canvas->drawPicture(picture2);
    // 获取 SkBitmap 对象
    //SkBitmap bitmap;
    //image->asLegacyBitmap(&bitmap);



    //SkPixmap src;
    //std::cout<<image->peekPixels(&src);

    //SkDynamicMemoryWStream dst0;
    ////bool success = SkPngEncoder::Encode(&dst0, src, SkPngEncoder::Options());
    //auto encoder1 = SkPngEncoder::Make(&dst0, src, SkPngEncoder::Options());
    ////std::cout << image->height() << std::endl;
    //for (int i = 0; i < src.height(); i++) {
    //    bool success = encoder1->encodeRows(1);
    //    std::cout << success << std::endl;
    //}


    // //Set up a linear gradient and draw a circle
    //{
    //    SkPoint linearPoints[] = { { 0, 0 }, { 300, 300 } };
    //    SkColor linearColors[] = { SK_ColorGREEN, SK_ColorBLACK };
    //    paint.setShader(SkGradientShader::MakeLinear(linearPoints, linearColors, nullptr, 2,
    //                                                 SkTileMode::kMirror));
    //    paint.setAntiAlias(true);

    //    canvas->drawCircle(200, 200, 64, paint);

    //    // Detach shader
    //    paint.setShader(nullptr);
    //}

    //// Draw a message with a nice black paint
    //SkFont font(fTypeface, 20);
    //font.setSubpixel(true);
    //paint.setColor(SK_ColorBLACK);

    //canvas->save();
    //static const char message[] = "Hello World ";

    //// Translate and rotate
    //canvas->translate(300, 300);
    //fRotationAngle += 0.2f;
    //if (fRotationAngle > 360) {
    //    fRotationAngle -= 360;
    //}
    //canvas->rotate(fRotationAngle);

    //// Draw the text
    //canvas->drawSimpleText(message, strlen(message), SkTextEncoding::kUTF8, 0, 0, font, paint);

    //canvas->restore();
}


void HelloWorld::onIdle() {
        std::unique_lock<std::mutex> lock(fMutex);
        fCondition.wait(lock, [this]() { return fDataReady; });

        // 重置数据标记
        fDataReady = false;

    // Just re-paint continuously
    fWindow->inval();
}

bool HelloWorld::onChar(SkUnichar c, skui::ModifierKey modifiers) {
    if (' ' == c) {
        if (Window::kRaster_BackendType == fBackendType) {
#if defined(SK_GL)
            fBackendType = Window::kNativeGL_BackendType;
#elif defined(SK_VULKAN)
            fBackendType = Window::kVulkan_BackendType;
#else
            SkDebugf("No GPU backend configured\n");
            return true;
#endif
        } else {
            fBackendType = Window::kRaster_BackendType;
        }
        fWindow->detach();
        fWindow->attach(fBackendType);
    }
    return true;
}
