#include <gst/gst.h>
#include <gst/video/video.h>
#include <cairo.h>
#include <math.h>
#include <iostream>

// Global variable for the artificial horizon angle (degrees)
// Update this value from a sensor thread to tilt the line
double horizon_angle = 0.0; 

// Drawing callback for Camera 1
static void on_draw_overlay(GstElement *overlay, cairo_t *cr, guint64 timestamp, 
                           guint64 duration, gpointer user_data) {
    int width = 1280, height = 960;
    double center_x = width / 2.0;
    double center_y = height / 2.0;

    // Convert degrees to radians
    double radians = horizon_angle * (M_PI / 180.0);

    // Set drawing parameters (White line)
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
    cairo_set_line_width(cr, 3.0);

    cairo_save(cr);
    cairo_translate(cr, center_x, center_y);
    cairo_rotate(cr, radians);

    // Draw the horizontal line (-300 to 300 pixels from center)
    cairo_move_to(cr, -300, 0);
    cairo_line_to(cr, 300, 0);
    cairo_stroke(cr);
    
    cairo_restore(cr);

    // Optional: Draw the "0" text using Cairo instead of a separate element
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 25.0);
    cairo_move_to(cr, center_x - 10, center_y - 10);
    cairo_show_text(cr, "0");
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *pipeline2;
    GstBus *bus, *bus2;
    GstMessage *msg, *msg2;

    gst_init(&argc, &argv);

    // Pipeline 1: Includes Dynamic Cairo Overlay
    // Note: videoconvert to BGRA is required for Cairo, then back to NV12 for x264enc
    const gchar *pipeline_desc = 
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a\" ! "
        "video/x-raw,format=NV12,width=1280,height=960,framerate=30/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "video/x-raw,format=BGRA ! " 
        "cairooverlay name=horizon_overlay ! "
        "videoconvert ! video/x-raw,format=NV12 ! "
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=2 key-int-max=30 ! "
        "h264parse ! "
        "mpegtsmux latency=0 pat-interval=100000 pmt-interval=100000 ! " 
        "srtsink uri=srt://:5000?mode=listener&latency=50 wait-for-connection=false";

    // Pipeline 2: Standard stream
    const gchar *pipeline_desc2 = 
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a\" ! "
        "video/x-raw,format=NV12,width=1280,height=960,framerate=30/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "videoflip method=rotate-180 ! videoconvert ! " // <-- Inserted here
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=2 key-int-max=30 ! "
        "h264parse ! "
        "mpegtsmux latency=0 pat-interval=100000 pmt-interval=100000 ! " 
        "srtsink uri=srt://:5001?mode=listener&latency=50 wait-for-connection=false";

    pipeline = gst_parse_launch(pipeline_desc, NULL);
    pipeline2 = gst_parse_launch(pipeline_desc2, NULL);

    if (!pipeline || !pipeline2) {
        std::cerr << "Failed to create pipelines." << std::endl;
        return -1;
    }

    // Connect the drawing signal to the Cairo Overlay in Pipeline 1
    GstElement *overlay = gst_bin_get_by_name(GST_BIN(pipeline), "horizon_overlay");
    if (overlay) {
        g_signal_connect(overlay, "draw", G_CALLBACK(on_draw_overlay), NULL);
        gst_object_unref(overlay);
    }

    std::cout << "Streaming Camera 1 (Horizon) on port 5000..." << std::endl;
    std::cout << "Streaming Camera 2 on port 5001..." << std::endl;

    gst_element_set_state(pipeline,  GST_STATE_PLAYING);
    gst_element_set_state(pipeline2, GST_STATE_PLAYING);

    // Standard GStreamer bus management
    bus = gst_element_get_bus(pipeline);
    bus2 = gst_element_get_bus(pipeline2);

    // This waits for the first pipeline's bus; in a real app, you'd use a GLib MainLoop
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, 
        (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (msg != NULL) gst_message_unref(msg);
    gst_object_unref(bus);
    gst_object_unref(bus2);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_set_state(pipeline2, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_object_unref(pipeline2);

    return 0;
}
