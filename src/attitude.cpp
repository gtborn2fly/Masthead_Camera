#include <attitude.hpp>
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

// Pitch roll and yaw. There may not be an updated
// with every call to getAttitude.
static double m_pitch   = 0.0;
static double m_roll    = 0.0;
static double m_yaw = 0.0;

// I2C bus that is connected to the BNo085
int i2c_bus = 0;

// BNo085 message buffer
uint8_t buffer[128];

void enableRotationVector(int fd);
void parseAndRemap(uint8_t* data);

/**
 * @brief Initialize the I2C bus and enable the rotation vector caclulations. 
 *
 * Setup the I2C bus on the Raspberry Pi. Open communications with the 
 * BNO085 9DOF Sensor and enable the Rotation Vector sensing and calculations.
 *
 * @return error - 0 for no error, 1 for I2C initialization failure.
 */
int initAttitude(){

    i2c_bus = open("/dev/i2c-1", O_RDWR);
    if (i2c_bus < 0 || ioctl(i2c_bus, I2C_SLAVE, BNO08X_ADDR) < 0) return 1;

    // Flush boot messages
    for(int i=0; i<10; i++) { read(i2c_bus, buffer, 128); usleep(10000); }

    enableRotationVector(i2c_bus);

    return 0;

}

/**
 * @brief Get the current Attitude. 
 *
 * Receive the current Pitch, Roll and yaw Angle.
 *
 * @return pitch - Camera pitch angle.
 * @return roll  - Camera roll angle.
 * @return yaw   - Camera yaw angle.
 */
void getAttitude(double *pitch, double *roll, double *yaw){

    int bytes = read(i2c_bus, buffer, 128);
    if (bytes > 4 && buffer[2] == 0x03) { // Channel 3: Input Reports
        int i = 4; // Skip SHTP Header
        while (i < bytes - 10) {
            if (buffer[i] == 0xFB) { // Skip Timebase Report (5 bytes)
                i += 5;
            } else if (buffer[i] == 0x08) { // Found Gaming Rotation Vector
                parseAndRemap(&buffer[i + 4]); // 4-byte offset: ID, Seq, Status, Delay
                break;
            } else {
                i++;
            }
        }
    }

    *pitch   = m_pitch;
    *roll    = m_roll;
    *yaw = m_yaw;
}

/**
 * @brief Enable the rotation vector report on BNO085. 
 *
 * Enable the sensing and calculation of the current Pitch, Roll 
 * and Yaw Angle.
 *
 * @param fd I2C bus identifier.
 */
void enableRotationVector(int fd) {
    // SHTP Header (4 bytes) + Set Feature Command (17 bytes)
    uint8_t cmd[21] = {
        21, 0, 2, 0,          // Length 21, Channel 2 (Control), Seq 0
        0xFD, 0x08, 0, 0, 0,  // 0x08 Gaming Rotation Vector - Gaming ignores magnetometer to reduce jumps. The Yaw drifts and is not tied to Heading.
        0x50, 0xC3, 0, 0,     // 50,000us (20Hz)
        0, 0, 0, 0, 0, 0, 0, 0
    };
    write(fd, cmd, 21);
}

/**
 * @brief Parse, convert and remap the angle data.
 *
 * Parse the response from the BNO085, convert it to Pitch,
 * Roll and Yaw angles and remap to the orientation of
 * sensor on the camera.
 *
 * @param data Raw binary data recieved form the BNO085 sensor. Header removed.
 */
void parseAndRemap(uint8_t* data) {
    // 1. Extract raw data from SHTP packet (Q14 format)
    // Order for Gaming Rotation Vector is: i, j, k, real (x, y, z, w)
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
    double temp_roll, temp_pitch, temp_yaw;
    
    // Calculate sin(pitch) for singularity check
    double sinp = 2 * (qw * qy - qz * qx);

    if (std::abs(sinp) >= 0.999) {
        // Singularity Case: Pitch is +/- 90 degrees
        // Force roll to 0 to prevent unstable jumps
        temp_pitch = std::copysign(M_PI / 2, sinp);
        temp_roll = 0.0;
        temp_yaw = 2 * atan2(qx, qw);
    } else {
        // Standard Case
        temp_pitch = asin(sinp);
        temp_roll = atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy));
        temp_yaw = atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz));
    }

    // 4. Convert to degrees and apply custom camera offsets
    // Original remap logic preserved after stabilization:
    // Roll: 90 - calculated_roll
    // Yaw: calculated_yaw - 90
    m_roll  = 90.0 - (temp_roll * 180.0 / M_PI);
    m_pitch = temp_pitch * 180.0 / M_PI;
    m_yaw   = (temp_yaw * 180.0 / M_PI) - 90.0;
}