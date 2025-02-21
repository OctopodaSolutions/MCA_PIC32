#ifndef STATUS_H
#define STATUS_H

#include <xc.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Flash memory validation key
#define FLASH_VALID_KEY 0xAA55AA55

typedef void (*WriteStringFn)(const char*);

typedef struct {
    uint32_t validKey;      // To check if flash has valid data
    int systemState;        // 0=stopped, 1=running
    int voltage;            // DAC voltage
    int courseGain;         // Course gain
    int fineGain;          // Fine gain
    int digitalGain;       // Digital gain
    int polarity;          // Input polarity
    int thresholdTime;     // Threshold time
    int riseTime;          // Rise time
    int flatTime;          // Flat time
    int poleZero;          // Pole-zero
    int digitalBL;         // Digital baseline
    int pileupReject;      // Pile-up reject
    int presetTime;        // Preset time
} SystemStatus;

// Function prototypes
void initStatus(void);
void saveStatusToFlash(void);
void loadStatusFromFlash(void);
void printStatusUpdate(WriteStringFn writeString, const SystemStatus* status);
const char* getStatusString(const SystemStatus* status);
void updateAndPrintStatus(WriteStringFn writeString, SystemStatus* status, const char* parameter, int value);

extern SystemStatus status;

#endif // STATUS_H