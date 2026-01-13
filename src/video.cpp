#include <gst/gst.h>
#include <gst/video/video.h>
#include <cairo.h>

#include <iostream>
#include <attitude.hpp>
#include <video.hpp>
#include <string>
#include <cstring>
#include <sys/socket.h>

// Drawing callback for Camera 1
static void on_draw_overlay(GstElement *overlay, cairo_t *cr, guint64 timestamp, 
                           guint64 duration, gpointer user_data) {
    
    double center_x = WIDTH / 2.0;
    double center_y = HEIGHT / 2.0;
    double height_per_deg = HEIGHT / VERTICAL_FOV_DEG;

    double pitch = 0.0;
    double roll = 0.0;
    double heading = 0.0;

    getAttitude(&pitch, &roll, &heading);

    // Convert degrees to radians
    double roll_rad = roll * DEG_TO_RAD;

    // Compute the angle line pixel heights
    double height_0_deg  = (center_y + height_per_deg * (pitch + VERTICAL_OFFSET_DEG));
    
    // Set drawing parameters (White line)
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
    cairo_set_line_width(cr, 3.0);

    // Put a line every 5 degrees from -5 to 30
    for (auto cur_line : ANGLE_LINE_SETTINGS){

        double cur_height = height_0_deg - (height_per_deg * cur_line.angle);


        cairo_save(cr);
        cairo_translate(cr, center_x, cur_height);
        cairo_rotate(cr, -roll_rad);

        // Draw the horizontal line (-300 to 300 pixels from center)
        cairo_move_to(cr, -WIDTH * cur_line.width_ratio / 2, 0);
        cairo_line_to(cr, WIDTH  * cur_line.width_ratio / 2, 0);
        cairo_stroke(cr);
        
        cairo_restore(cr);
        
        if(cur_line.display_text){
            cairo_save(cr);

            // Optional: Draw the "0" text using Cairo instead of a separate element
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 25.0);
            cairo_move_to(cr, center_x - WIDTH*0.2, cur_height - 10);
            cairo_show_text(cr, std::to_string(cur_line.angle).c_str());
            cairo_restore(cr);
        }
    }

#ifdef DEBUG
    cairo_save(cr);

    // Optional: Draw the Pitch text using Cairo instead of a separate element
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 25.0);
    cairo_move_to(cr, 10, HEIGHT - 70);
    cairo_show_text(cr, ("Pitch  =" + std::to_string(pitch)).c_str());

    cairo_restore(cr);
    cairo_save(cr);

    // Optional: Draw the Roll text using Cairo instead of a separate element
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 25.0);
    cairo_move_to(cr, 10, HEIGHT - 40);
    cairo_show_text(cr, ("Roll   =" + std::to_string(roll)).c_str());

    cairo_restore(cr);
    cairo_save(cr);

    // Optional: Draw the Heading text using Cairo instead of a separate element
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 25.0);
    cairo_move_to(cr, 10, HEIGHT - 10);
    cairo_show_text(cr, ("Heading=" + std::to_string(heading)).c_str());
#endif
}


int startStreaming() {
    GstElement *pipeline, *pipeline2;
    GstBus *bus, *bus2;
    GstMessage *msg, *msg2;

    gst_init(NULL, NULL);

    // Pipeline 1: Includes Dynamic Cairo Overlay
    // Note: videoconvert to BGRA is required for Cairo, then back to NV12 for x264enc
    const std::string pipeline_desc = 
        
        // Camera Video Source 1 - Forward with normal angle lense
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@88000/imx708@1a\" ! "
        
        // Convert the video to common format
        "videoconvert ! "

        // Indicate that the video is to be scaled and not cropped. Maintain aspect ratio with add-borders.
        "videoscale add-borders=true ! "
        
        // Set the resolution of the video and convert it to NV12
        "video/x-raw,format=NV12,width=" + std::to_string(WIDTH) + ",height=" + std::to_string(HEIGHT) + ",framerate=20/1 ! "
 
        "queue max-size-buffers=13 leaky=downstream ! videoconvert ! "

        "video/x-raw,format=BGRA ! " 

        "cairooverlay name=horizon_overlay ! "

        "videoconvert ! video/x-raw,format=NV12 ! "

        // Valve to stop buffers from reaching the encoder when no one is watching
        "valve name=stream_valve drop=true ! " 
        
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=4 key-int-max=30 ! "
        
        "h264parse ! "

        "mpegtsmux latency=0 pat-interval=100000 pmt-interval=100000 ! " 
        
        "srtsink name=mysink uri=srt://:5000?mode=listener&latency=50 wait-for-connection=true";

    // Pipeline 2: Standard stream
    const std::string pipeline_desc2 = 
        "libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@80000/imx477@1a\" ! "
        "videoconvert ! "
        "videoscale add-borders=true ! "
        "video/x-raw,format=NV12,width=" + std::to_string(WIDTH_2) + ",height=" + std::to_string(HEIGHT_2) + ",framerate=20/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "videoflip method=rotate-180 ! videoconvert ! "
        
        // Valve to stop buffers from reaching the encoder when no one is watching
        "valve name=stream_valve2 drop=true ! " 
        
        "x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 threads=4 key-int-max=30 ! "
        "h264parse ! "
        "mpegtsmux latency=0 pat-interval=100000 pmt-interval=100000 ! " 
        "srtsink name=mysink2 uri=srt://:5001?mode=listener&latency=50 wait-for-connection=true";

    pipeline = gst_parse_launch(pipeline_desc.c_str(), NULL);
    pipeline2 = gst_parse_launch(pipeline_desc2.c_str(), NULL);

    if (!pipeline || !pipeline2) {
        std::cerr << "Failed to create pipelines." << std::endl;
        return -1;
    }

    /******************* Stream Valves ******************/
    // This only allows the encoder to run when the stream is connected. Since
    // only one stream will be connected at a time it allows for all 4 cores to 
    // be used for encoding and increases thruput for the one video stream.

    // Get references to the elements
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
    GstElement *valve = gst_bin_get_by_name(GST_BIN(pipeline), "stream_valve");

    // Signal: Client Connects -> Open Valve (Start Encoding)
    g_signal_connect(sink, "caller-added", G_CALLBACK(+[](GstElement* sink, int unused, sockaddr* addr, gpointer valve_ptr) {
        g_print("Client connected to camera 1! Starting encoder...\n");
        g_object_set(G_OBJECT(valve_ptr), "drop", FALSE, NULL);
    }), valve);

    // Signal: Client Disconnects -> Close Valve (Stop Encoding)
    g_signal_connect(sink, "caller-removed", G_CALLBACK(+[](GstElement* sink, int unused, sockaddr* addr, gpointer valve_ptr) {
        g_print("Client disconnected to camera 1. Throttling CPU...\n");
        g_object_set(G_OBJECT(valve_ptr), "drop", TRUE, NULL);
    }), valve);



    // Get references to the elements
    GstElement *sink2 = gst_bin_get_by_name(GST_BIN(pipeline2), "mysink2");
    GstElement *valve2 = gst_bin_get_by_name(GST_BIN(pipeline2), "stream_valve2");

    // Signal: Client Connects -> Open Valve (Start Encoding)
    g_signal_connect(sink2, "caller-added", G_CALLBACK(+[](GstElement* sink, int unused, sockaddr* addr, gpointer valve_ptr) {
        g_print("Client connected to camera 2! Starting encoder...\n");
        g_object_set(G_OBJECT(valve_ptr), "drop", FALSE, NULL);
    }), valve2);

    // Signal: Client Disconnects -> Close Valve (Stop Encoding)
    g_signal_connect(sink2, "caller-removed", G_CALLBACK(+[](GstElement* sink, int unused, sockaddr* addr, gpointer valve_ptr) {
        g_print("Client disconnected to camera 2. Throttling CPU...\n");
        g_object_set(G_OBJECT(valve_ptr), "drop", TRUE, NULL);
    }), valve2);



    

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