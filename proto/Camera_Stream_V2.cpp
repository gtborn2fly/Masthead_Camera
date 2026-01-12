#include <opencv2/opencv.hpp>
#include <libcamera/libcamera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/framebuffer_allocator.h>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>

using namespace libcamera;

static libcamera::Camera *g_camera = nullptr;
static cv::VideoWriter g_writer;

static void requestComplete(Request *request)
{
    
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

    cv::Mat frameRGBA(
        480,
        640,
        CV_8UC4,
        mapped,
        640 * 4
    );

    cv::Mat frameBGR;
    cv::cvtColor(frameRGBA, frameBGR, cv::COLOR_RGBA2BGR);

    // 🚀 STREAM TO VLC
    g_writer.write(frameBGR);

    munmap(mapped, plane.length);

    request->reuse(Request::ReuseBuffers);
    g_camera->queueRequest(request);

}

int main() {
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

    // Use the viewfinder configuration
    std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration({ StreamRole::Viewfinder });
    config->at(0).pixelFormat = formats::XRGB8888;
    config->at(0).size.width = 640;
    config->at(0).size.height = 480;

    if (camera->configure(config.get()) != 0) {
        std::cerr << "Failed to configure camera" << std::endl;
        return -1;
    }

    FrameBufferAllocator *allocator = new FrameBufferAllocator(camera);

    for (StreamConfiguration &cfg : *config) {
        int ret = allocator->allocate(cfg.stream());
        if (ret < 0) {
            std::cerr << "Can't allocate buffers" << std::endl;
            return -ENOMEM;
        }

        size_t allocated = allocator->buffers(cfg.stream()).size();
        std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
    }

    StreamConfiguration &streamConfig = config->at(0);
    std::cout << "Default viewfinder configuration is: " << streamConfig.toString() << std::endl;

    Stream *stream = streamConfig.stream();
    const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
    std::vector<std::unique_ptr<Request>> requests;

    for (unsigned int i = 0; i < buffers.size(); ++i) {
        std::unique_ptr<Request> request = camera->createRequest();
        if (!request)
        {
            std::cerr << "Can't create request" << std::endl;
            return -ENOMEM;
        }

        const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
        int ret = request->addBuffer(stream, buffer.get());
        if (ret < 0)
        {
            std::cerr << "Can't set buffer for request"
                << std::endl;
            return ret;
        }

        requests.push_back(std::move(request));
    }

    camera->requestCompleted.connect(requestComplete);
    camera->start();

    std::string gst_pipeline =
        "appsrc ! videoconvert ! "
        "x264enc tune=zerolatency bitrate=4000 speed-preset=ultrafast ! "
        "rtph264pay name=pay0 pt=96 ! "
        "gdppay ! tcpserversink host=0.0.0.0 port=8554 sync=false";

    g_writer.open(
        gst_pipeline,
        cv::CAP_GSTREAMER,
        0,
        30.0,
        cv::Size(640, 480),
        true
    );

    if (!g_writer.isOpened()) {
        std::cerr << "Failed to open GStreamer pipeline" << std::endl;
        return -1;
    }

    for (std::unique_ptr<Request> &request : requests)
        camera->queueRequest(request.get());

    while (true)
        
        std::this_thread::sleep_for(std::chrono::seconds(3));



    // cv::Mat frame(480, 640, CV_8UC4);

    // std::cout << "Press Ctrl+C to quit" << std::endl;

    // while (true) {
    //     // In a real implementation, you would grab buffers from the camera,
    //     // map them, and copy the pixel data to `frame`. Placeholder here:
    //     for (std::unique_ptr<Request> &request : requests)
    //         camera->queueRequest(request.get());


    //     frame.setTo(cv::Scalar(0, 0, 255, 0));  // Red placeholder
    //     cv::imshow("Pi Camera", frame);
    //     if (cv::waitKey(1) == 'q') break;
    // }

    camera->stop();
    camera->release();
    cm.stop();
}
