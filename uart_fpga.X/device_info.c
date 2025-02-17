#include "device_info.h"

// Store device info in program memory
// Place this in a dedicated section of program memory
const DeviceInfo __attribute__((section(".device_config"))) deviceConfig = {
    .serialNumber = "XS2024001",
    .hwVersion = "v1.0.0",
    .licenseNumber = "NTPC-2024-012-DEC",
    .installDate = "11-12-2024",
    .warrantyDate = "10-12-2026"
};

// Static buffer for info string
static char infoBuffer[256];

// Function to get device info as a string
const char* getDeviceInfoString(void) {
    sprintf(infoBuffer, 
            "Device Information:\r\n"
            "Serial Number: %s\r\n"
            "Hardware Version: %s\r\n"
            "License Number: %s\r\n"
            "Installation Date: %s\r\n"
            "Warranty Date: %s\r\n",
            deviceConfig.serialNumber,
            deviceConfig.hwVersion,
            deviceConfig.licenseNumber,
            deviceConfig.installDate,
            deviceConfig.warrantyDate);
            
    return infoBuffer;
}

// Function to send device info using provided write function
void getDeviceInfo(WriteStringFn writeString) {
    if (writeString != NULL) {
        writeString(getDeviceInfoString());
    }
}