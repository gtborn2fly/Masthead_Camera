#pragma once

#define BNO08X_ADDR 0x4A

// Public Function Prototypes

// Initialize the BNo085 9DOF sensor. It communicated via I2C.
int initAttitude();

// Get the current pitch, roll and heading values. If there is not
// a new value, use the one stored from the previous reading.
void getAttitude(double *pitch, double *roll, double *heading);