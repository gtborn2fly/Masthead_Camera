#include <gst/gst.h>
#include <iostream>

int main(int argc, char *argv[]) {
    GstElement *pipeline;
    GstElement *pipeline2;
    GstBus *bus;
    GstBus *bus2;
    GstMessage *msg;
    GstMessage *msg2;

    gst_init(&argc, &argv);

    // Replace <CLIENT_IP> with the IP address of your receiving device
    const gchar *pipeline_desc = 
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a\" ! video/x-raw,format=NV12,width=1280,height=960,framerate=30/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=2 ! "
        "mpegtsmux latency=0 ! " 
        "srtsink uri=srt://:5000?mode=listener&latency=50 wait-for-connection=false";

    pipeline = gst_parse_launch(pipeline_desc, NULL);

    if (!pipeline) {
        std::cerr << "Failed to create pipeline." << std::endl;
        return -1;
    }

    std::cout << "Streaming SRT to 192.168.68.135:5000..." << std::endl;

    // Replace <CLIENT_IP> with the IP address of your receiving device
    const gchar *pipeline_desc2 = 
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a\" ! video/x-raw,format=NV12,width=1280,height=960,framerate=30/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=2 ! "
        "mpegtsmux latency=0 ! " 
        "srtsink uri=srt://:5001?mode=listener&latency=50 wait-for-connection=false";

    pipeline2 = gst_parse_launch(pipeline_desc2, NULL);

    if (!pipeline2) {
        std::cerr << "Failed to create pipeline." << std::endl;
        return -1;
    }

    std::cout << "Streaming SRT to 192.168.68.135:5001..." << std::endl;

    // Start playing
    gst_element_set_state(pipeline,  GST_STATE_PLAYING);
    gst_element_set_state(pipeline2, GST_STATE_PLAYING);

    // Wait until error or EOS
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, 
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (msg != NULL)
        gst_message_unref(msg);

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);


    // Wait until error or EOS
    bus2 = gst_element_get_bus(pipeline2);
    msg2 = gst_bus_timed_pop_filtered(bus2, GST_CLOCK_TIME_NONE, 
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (msg2 != NULL)
        gst_message_unref(msg2);

    gst_object_unref(bus2);
    gst_element_set_state(pipeline2, GST_STATE_NULL);
    gst_object_unref(pipeline2);

    return 0;
}