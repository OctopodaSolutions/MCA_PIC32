#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <xc.h>
#include <stdio.h>
#include <string.h>

// Function pointer type for write string operation
typedef void (*WriteStringFn)(const char*);

// Device Info structure definition
typedef struct {
    char serialNumber[16];     // Device serial number
    char hwVersion[8];         // Hardware version
    char licenseNumber[32];    // License number
    char installDate[11];      // Installation date (YYYY-MM-DD)
    char warrantyDate[11];     // Warranty expiration date (YYYY-MM-DD)
} DeviceInfo;

// Function prototypes
void getDeviceInfo(WriteStringFn writeString);
const char* getDeviceInfoString(void);

// External declaration of the device config
extern const DeviceInfo deviceConfig;

#endif // DEVICE_INFO_H