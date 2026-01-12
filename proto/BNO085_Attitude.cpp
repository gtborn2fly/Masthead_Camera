#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstdint>
#include <cstdio> 

#define BNO08X_ADDR 0x4A

void enableRotationVector(int fd) {
    // SHTP Header (4 bytes) + Set Feature Command (17 bytes)
    uint8_t cmd[21] = {
        21, 0, 2, 0,       // Length 21, Channel 2 (Control), Seq 0
        0xFD, 0x05, 0, 0, 0, 
        0x50, 0xC3, 0, 0,  // 50,000us (20Hz)
        0, 0, 0, 0, 0, 0, 0, 0
    };
    write(fd, cmd, 21);
}

void parseAndRemap(uint8_t* data) {
    // 1. Extract raw data from SHTP packet (Q14 format)
    int16_t raw_i = (int16_t)(data[1] << 8 | data[0]);
    int16_t raw_j = (int16_t)(data[3] << 8 | data[2]);
    int16_t raw_k = (int16_t)(data[5] << 8 | data[4]);
    int16_t raw_r = (int16_t)(data[7] << 8 | data[6]);

    // 2. Convert to float (divide by 2^14)
    float qx = raw_i / 16384.0f;
    float qy = raw_j / 16384.0f;
    float qz = raw_k / 16384.0f;
    float qw = raw_r / 16384.0f;

    // 3. Calculate Euler Angles (Standard Z-Y-X sequence)
    // Roll: Rotation about X-axis
    double roll = 90 - (atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) * 180.0 / M_PI);

    // Pitch: Rotation about Y-axis (In this frame, Y is your Pitch axis)
    double sinp = 2 * (qw * qy - qz * qx);
    double pitch;
    if (std::abs(sinp) >= 1)
        pitch = std::copysign(M_PI / 2, sinp) * 180.0 / M_PI;
    else
        pitch = asin(sinp) * 180.0 / M_PI;

    // Yaw: Rotation about Z-axis
    double yaw = (-1 * (atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * 180.0 / M_PI)) - 90;

    // Adjust to 0 to 360 deg
    if (yaw < 0){
        yaw += 360;
    }
    else if (yaw > 360){
        yaw -= 360;
    }

    printf("Pitch: %6.2f | Roll: %6.2f | Yaw: %6.2f   \r", pitch, roll, yaw);
    fflush(stdout);
}

int main() {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0 || ioctl(fd, I2C_SLAVE, BNO08X_ADDR) < 0) return 1;

    // Flush boot messages
    uint8_t buffer[128];
    for(int i=0; i<10; i++) { read(fd, buffer, 128); usleep(10000); }

    enableRotationVector(fd);

    while (true) {
        int bytes = read(fd, buffer, 128);
        if (bytes > 4 && buffer[2] == 0x03) { // Channel 3: Input Reports
            int i = 4; // Skip SHTP Header
            while (i < bytes - 10) {
                if (buffer[i] == 0xFB) { // Skip Timebase Report (5 bytes)
                    i += 5;
                } else if (buffer[i] == 0x05) { // Found Rotation Vector
                    parseAndRemap(&buffer[i + 4]); // 4-byte offset: ID, Seq, Status, Delay
                    break;
                } else {
                    i++;
                }
            }
        }
        usleep(10000);
    }
    return 0;
}
