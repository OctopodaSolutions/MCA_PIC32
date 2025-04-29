#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Define function pointer type for WriteString function
typedef void (*WriteStringFn)(const char*);

// System status structure to hold all parameters - ordered according to token processing
typedef struct {
    uint32_t validKey;          // Flash validation key
    
    // Parameters in token processing order
    int presetTime;             // Preset time (0-86400 seconds) - token[1]
    int counts;                 // Total counts - token[2]
    int startChannel;           // Start channel - token[3]
    int endChannel;             // End channel - token[4]
    int noOfChannels;           // Number of channels - token[5]
    int LLD;                    // Lower level discriminator - token[6]
    int ULD;                    // Upper level discriminator - token[7]
    int courseGain;             // Coarse gain (0-63) - token[8]
    int fineGain;               // Fine gain (0-255) - token[9]
    int inputPolarity;          // Input polarity - token[10]
    int threshold;              // Threshold value - token[11]
    int riseTime;               // Rise time (0-10000 ns) - token[12]
    int flatTime;               // Flat time (0-10000 ns) - token[13]
    int poleZero;               // Pole-zero (0-1000) - token[14]
    int digitalBLR;             // Digital baseline restore - token[15]
    int pileupReject;           // Pile-up reject (0-100) - token[16]
    int voltage;                // HV/voltage value (0-1000) - token[17]
    int hvStatus;               // HV status (0 = off, 1 = on) - token[18]
    int dwellTime;              // Dwell time for MCS mode - token[19]
    
    // Additional parameters needed for other commands
    int digitalGain;            // Digital gain (0-255)
    int polarity;               // 0 = negative, 1 = positive
    int thresholdTime;          // Threshold time (0-1000 seconds)
    int digitalBL;              // Digital baseline (0-1000)
    int count;                  // Count value
    int channels;               // Number of channels
    
    // Mode selection
    int mode;                   // 0 = PHA, 1 = MCS
} SystemStatus;

// Function prototypes
void initStatus(void);
void saveStatusToFlash(void);
const char* getStatusString(const SystemStatus* status);
void printStatusUpdate(WriteStringFn writeString, const SystemStatus* status);
void updateAndPrintStatus(WriteStringFn writeString, SystemStatus* status, const char* parameter, int value);

#endif /* STATUS_H */