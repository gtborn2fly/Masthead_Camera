#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>

#include <opencv2/opencv.hpp>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

struct CameraStream
{
    int camIndex;
    std::string mountPoint;
    GstElement *appsrc = nullptr;
    cv::VideoCapture cap;
};

static std::string getTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static gboolean push_frame(CameraStream *stream)
{
    cv::Mat frame;
    if (!stream->cap.read(frame))
        return TRUE;

    // Horizontal line
    int y = frame.rows / 2;
    cv::line(frame,
             cv::Point(0, y),
             cv::Point(frame.cols, y),
             cv::Scalar(0, 255, 0),
             2);

    // Timestamp
    cv::putText(frame,
                getTimestamp(),
                cv::Point(20, 40),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0,
                cv::Scalar(0, 255, 255),
                2,
                cv::LINE_AA);

    GstBuffer *buffer = gst_buffer_new_allocate(
        nullptr,
        frame.total() * frame.elemSize(),
        nullptr);

    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    memcpy(map.data, frame.data, frame.total() * frame.elemSize());
    gst_buffer_unmap(buffer, &map);

    // Timestamp buffers immediately
    GST_BUFFER_PTS(buffer) = gst_util_get_timestamp();
    GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 30);

    GstFlowReturn ret;
    g_signal_emit_by_name(stream->appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    return TRUE;
}

static void media_configure(GstRTSPMediaFactory *,
                            GstRTSPMedia *media,
                            gpointer user_data)
{
    auto *stream = static_cast<CameraStream *>(user_data);

    GstElement *pipeline = gst_rtsp_media_get_element(media);
    stream->appsrc =
        gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "mysrc");

    g_object_set(stream->appsrc,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 "block", FALSE,
                 "do-timestamp", TRUE,
                 nullptr);

    gst_object_unref(pipeline);

    // Push at exact frame rate (33 ms)
    g_timeout_add(33, (GSourceFunc)push_frame, stream);
}

static void setup_rtsp_stream(GstRTSPServer *server, CameraStream &stream)
{
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    std::string launch =
        "( appsrc name=mysrc "
        "caps=video/x-raw,format=BGR,width=1280,height=720,framerate=30/1 "
        "is-live=true format=time do-timestamp=true "
        "! queue max-size-buffers=1 leaky=downstream "
        "! videoconvert "
        "! x264enc tune=zerolatency speed-preset=ultrafast "
        "key-int-max=30 bframes=0 threads=1 bitrate=2000 "
        "! rtph264pay pt=96 config-interval=1 mtu=1200 "
        ")";

    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_media_factory_set_latency(factory, 0);
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    g_signal_connect(factory, "media-configure",
                     (GCallback)media_configure, &stream);

    gst_rtsp_mount_points_add_factory(
        mounts,
        stream.mountPoint.c_str(),
        factory);

    g_object_unref(mounts);
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    CameraStream cam0{0, "/cam0"};
    CameraStream cam1{1, "/cam1"};

    cam0.cap.open(0, cv::CAP_LIBCAMERA);
    cam1.cap.open(1, cv::CAP_LIBCAMERA);

    for (auto *cam : {&cam0, &cam1})
    {
        cam->cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        cam->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        cam->cap.set(cv::CAP_PROP_FPS, 30);
        cam->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

        if (!cam->cap.isOpened())
        {
            std::cerr << "Failed to open camera\n";
            return -1;
        }
    }

    GstRTSPServer *server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554");

    setup_rtsp_stream(server, cam0);
    setup_rtsp_stream(server, cam1);

    gst_rtsp_server_attach(server, nullptr);

    std::cout << "Ultra-low-latency RTSP streams:\n";
    std::cout << "  rtsp://<PI_IP>:8554/cam0\n";
    std::cout << "  rtsp://<PI_IP>:8554/cam1\n";

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    return 0;
}