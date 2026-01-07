#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cmath>
#include <iostream>

#include "sh2.h"
#include "shtp.h"

static const char* I2C_DEVICE = "/dev/i2c-1";
static const int BNO08X_ADDR = 0x4A;

int i2c_fd;

// -------- HAL callbacks --------
static int hostRead(void*, uint8_t* data, uint32_t len) {
    return (read(i2c_fd, data, len) == (int)len) ? SH2_OK : SH2_ERR;
}

static int hostWrite(void*, const uint8_t* data, uint32_t len) {
    return (write(i2c_fd, data, len) == (int)len) ? SH2_OK : SH2_ERR;
}

static void delayMs(uint32_t ms) {
    usleep(ms * 1000);
}

// -------- Quaternion → Euler --------
void quaternionToEuler(float w, float x, float y, float z,
                        float& roll, float& pitch, float& yaw) {
    roll = atan2(2.0f * (w*x + y*z),
                 1.0f - 2.0f * (x*x + y*y));

    pitch = asin(2.0f * (w*y - z*x));

    yaw = atan2(2.0f * (w*z + x*y),
                1.0f - 2.0f * (y*y + z*z));
}

int main() {
    // Open I2C
    i2c_fd = open(I2C_DEVICE, O_RDWR);
    ioctl(i2c_fd, I2C_SLAVE, BNO08X_ADDR);

    // SH-2 HAL
    sh2_hal_t hal{};
    hal.read = hostRead;
    hal.write = hostWrite;
    hal.delay_ms = delayMs;

    if (sh2_open(&hal, nullptr) != SH2_OK) {
        std::cerr << "SH2 open failed\n";
        return -1;
    }

    // Enable rotation vector at 50 Hz
    sh2_enableSensor(sh2_RotationVector, 20000); // 20,000 µs = 50 Hz

    sh2_SensorValue_t value;

    while (true) {
        sh2_processInput();

        while (sh2_getNextEvent(&value) == SH2_OK) {
            if (value.sensorId == sh2_RotationVector) {

                float w = value.un.rotationVector.real;
                float x = value.un.rotationVector.i;
                float y = value.un.rotationVector.j;
                float z = value.un.rotationVector.k;

                float roll, pitch, yaw;
                quaternionToEuler(w, x, y, z, roll, pitch, yaw);

                // Convert to degrees
                roll  *= 180.0f / M_PI;
                pitch *= 180.0f / M_PI;
                yaw   *= 180.0f / M_PI;

                std::cout << "Roll: " << roll
                          << "  Pitch: " << pitch
                          << "  Heading: " << yaw << std::endl;
            }
        }
    }
}