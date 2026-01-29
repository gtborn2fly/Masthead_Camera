#pragma once

#include <math.h>

//#define DEBUG

static const float DEG_TO_RAD = (M_PI / 180.0);

struct AngleLineSettings {
    int   angle;
    float width_ratio;
    bool  display_text;
};

static const AngleLineSettings ANGLE_LINE_SETTINGS[] = {{-10, .3, true},
                                                        {-5,  .3, true},
                                                        {0,   .8, true},
                                                        {1,   .1, false},
                                                        {2,   .1, false},
                                                        {3,   .1, false},
                                                        {4,   .1, false},
                                                        {5,   .3, true},
                                                        {10,  .3, true},
                                                        {15,  .3, true}};

static const int WIDTH   = 1280, HEIGHT   = 1080;
static const int WIDTH_2 = 1280, HEIGHT_2 = 1080;

static const int   VERTICAL_FOV_DEG    = 41;
static const int   HORIZONTAL_FOV_DEG  = 67;
static const float VERTICAL_OFFSET_DEG = 10.0;  // Angle that the camera is pitched up.

// Public Function Prototypes
int startStreaming();
