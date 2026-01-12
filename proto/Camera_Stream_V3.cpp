#include <opencv2/opencv.hpp>
#include <libcamera/libcamera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/framebuffer_allocator.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <iostream>
#include <memory>
#include <vector>
#include <mutex>

#include <sys/mman.h>
#include <unistd.h>

using namespace libcamera;

// Global camera and frame
static Camera *g_camera = nullptr;
static cv::Mat g_frame;
static std::mutex g_frameMutex;

// ======== GStreamer appsrc need-data callback ========
static void need_data(GstAppSrc *src, guint, gpointer) {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    if (g_frame.empty())
        return;

    // Convert BGR → I420
    cv::Mat frameYUV;
    cv::cvtColor(g_frame, frameYUV, cv::COLOR_BGR2YUV_I420);

    GstBuffer *buffer = gst_buffer_new_allocate(nullptr,
                                                frameYUV.total() * frameYUV.elemSize(),
                                                nullptr);
    gst_buffer_fill(buffer, 0, frameYUV.data, frameYUV.total() * frameYUV.elemSize());

    GstFlowReturn ret;
    g_signal_emit_by_name(src, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
}

// ======== libcamera request complete callback ========
static void requestComplete(Request *request) {
    if (request->status() != Request::RequestComplete)
        return;

    const auto &buffers = request->buffers();
    FrameBuffer *buffer = buffers.begin()->second;
    const FrameBuffer::Plane &plane = buffer->planes()[0];

    void *mapped = mmap(nullptr, plane.length,
                        PROT_READ, MAP_SHARED,
                        plane.fd.get(), 0);
    if (mapped == MAP_FAILED)
        return;

    // OpenCV: XRGB8888 → BGR
    cv::Mat frameRGBA(480, 640, CV_8UC4, mapped, 640 * 4);
    cv::Mat frameBGR;
    cv::cvtColor(frameRGBA, frameBGR, cv::COLOR_RGBA2BGR);

    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        g_frame = frameBGR.clone();
    }

    munmap(mapped, plane.length);

    request->reuse(Request::ReuseBuffers);
    g_camera->queueRequest(request);
}

// ======== RTSP media configure callback ========
static void media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer) {
    GstElement *pipeline = gst_rtsp_media_get_element(media);
    GstElement *appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysrc");

    // Set caps
    GstCaps *caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "I420",
        "width", G_TYPE_INT, 640,
        "height", G_TYPE_INT, 480,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        nullptr
    );
    gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);
    gst_caps_unref(caps);

    gst_app_src_set_stream_type(GST_APP_SRC(appsrc), GST_APP_STREAM_TYPE_STREAM);
    g_signal_connect(appsrc, "need-data", G_CALLBACK(need_data), nullptr);

    gst_object_unref(appsrc);
    gst_object_unref(pipeline);
}

int main() {
    gst_init(nullptr, nullptr);

    // ======== Setup RTSP server ========
    GstRTSPServer *server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554");

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();


    gst_rtsp_media_factory_set_launch(factory,
        "( appsrc name=mysrc is-live=true block=true format=time ! "
        "videoconvert ! queue ! x264enc tune=zerolatency bitrate=4000 speed-preset=ultrafast ! "
        "rtph264pay name=pay0 pt=96 )");

    gst_rtsp_media_factory_set_shared(factory, TRUE);
    g_signal_connect(factory, "media-configure", G_CALLBACK(media_configure), nullptr);

    gst_rtsp_mount_points_add_factory(mounts, "/mystream", factory);
    g_object_unref(mounts);
    gst_rtsp_server_attach(server, NULL);

    std::cout << "RTSP server ready at rtsp://<Pi-IP>:8554/mystream\n";

    // ======== Setup libcamera ========
    CameraManager cm;
    cm.start();

    if (cm.cameras().empty()) {
        std::cerr << "No cameras found!" << std::endl;
        return -1;
    }

    std::shared_ptr<Camera> camera = cm.cameras()[0];
    camera->acquire();
    std::cout << "Camera acquired: " << camera->id() << std::endl;

    g_camera = camera.get();

    auto config = camera->generateConfiguration({ StreamRole::Viewfinder });
    config->at(0).pixelFormat = formats::XRGB8888;
    config->at(0).size.width = 640;
    config->at(0).size.height = 480;

    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to configure camera" << std::endl;
        return -1;
    }

    FrameBufferAllocator allocator(camera);
    for (auto &cfg : *config)
        allocator.allocate(cfg.stream());

    Stream *stream = config->at(0).stream();
    const auto &buffers = allocator.buffers(stream);
    std::vector<std::unique_ptr<Request>> requests;
    for (auto &buffer : buffers) {
        auto request = camera->createRequest();
        request->addBuffer(stream, buffer.get());
        requests.push_back(std::move(request));
    }

    camera->requestCompleted.connect(requestComplete);
    camera->start();
    for (auto &request : requests)
        camera->queueRequest(request.get());

    // ======== Start GMainLoop ========
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // Cleanup (never reached here)
    camera->stop();
    camera->release();
    cm.stop();

    return 0;
}
