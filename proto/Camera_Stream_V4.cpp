#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <iostream>

static void on_new_connection (GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) {
    std::cout << "New client connected!" << std::endl;
}

static void on_client_closed (GstRTSPClient *client, gpointer user_data) {
    std::cout << "Client disconnected!" << std::endl;
}

int main (int argc, char *argv[]) {
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    
    gst_init (&argc, &argv);
    
    loop = g_main_loop_new (NULL, FALSE);
    
    // Create a new RTSP server
    server = gst_rtsp_server_new ();
    g_signal_connect (server, "new-client", (GCallback) on_new_connection, NULL);
    
    // Get the mount points for the server
    mounts = gst_rtsp_server_get_mount_points (server);
    
    // Create a media factory for the camera stream
    factory = gst_rtsp_media_factory_new ();
    gst_rtsp_media_factory_set_launch (factory,
        "( libcamerasrc ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
        "x264enc tune=zerolatency speed-preset=ultrafast ! "
        "rtph264pay name=pay0 pt=96 )");

    gst_rtsp_media_factory_set_shared (factory, TRUE);
    gst_rtsp_media_factory_set_latency(factory, 0); 

    // Attach the factory to the /stream path
    gst_rtsp_mount_points_add_factory (mounts, "/stream", factory);
    
    // Unreference the mount points
    g_object_unref (mounts);
    
    // Attach the server to the default maincontext
    gst_rtsp_server_attach (server, NULL);
    
    // Start the main loop
    std::cout << "RTSP stream ready at rtsp://127.0.0.1:8554/stream" << std::endl;
    g_main_loop_run (loop);
    
    return 0;
}
