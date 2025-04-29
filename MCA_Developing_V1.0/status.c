#include "status.h"
#include <xc.h>
#include <stdbool.h>


// Place flash storage in program memory at specific physical address
// Reserve last row of program flash memory
#define FLASH_ROW_SIZE 512
#define FLASH_PAGE_SIZE 4096
#define FLASH_STATUS_ADDR 0x1D01F000

// Flash validation pattern
#define FLASH_VALID_KEY 0xAA55AA55

// Single declaration of flash storage
volatile const SystemStatus __attribute__((space(prog),aligned(FLASH_ROW_SIZE))) flashStatus = {
    .validKey = FLASH_VALID_KEY
};

// RAM copy of status
SystemStatus status;

static void unlockFlash(void) {
    // Disable interrupts during unlock sequence
    __builtin_disable_interrupts();
    
    // Flash unlock sequence
    NVMKEY = 0;
    NVMKEY = 0xAA996655;
    NVMKEY = 0x556699AA;
}

// Function to get status string as comma-separated values with redundant parameters removed
const char* getStatusString(const SystemStatus* status)
{
    static char buffer[512]; // Buffer for the status string
    sprintf(buffer, 
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            // Parameters in token processing order, 
            status->mode,
            status->presetTime,
            status->counts,            
            status->startChannel,
            status->endChannel,
            status->noOfChannels,
            status->LLD,
            status->ULD,
            status->courseGain,
            status->fineGain,
            status->inputPolarity,
            status->threshold,
            status->riseTime,
            status->flatTime,
            status->poleZero,
            status->digitalBLR,
            status->pileupReject,
            status->voltage,
            status->hvStatus,
            status->dwellTime);
            
    return buffer;
}

// Function to write a row to flash with verification
static bool writeFlashRow(uint32_t* dst, uint32_t* src) {
    bool success = true;
    
    // Set up the row program operation
    NVMCON = 0x4003;      // Row programming
    NVMADDR = (uint32_t)dst & 0x1FFFFFFF;  // Physical address
    NVMSRCADDR = (uint32_t)src & 0x1FFFFFFF;
    
    // Start programming
    unlockFlash();
    NVMCONSET = 0x8000;
    
    // Wait for completion
    while(NVMCON & 0x8000);
    
    // Check for errors
    if(NVMCON & 0x3000) {  // WRERR or LVDERR
        success = false;
    }
    
    // Re-enable interrupts
    __builtin_enable_interrupts();
    
    return success;
}

void initStatus(void) {
    // Check if flash has valid data
    if(flashStatus.validKey == FLASH_VALID_KEY) {
        // Load saved data from flash
        memcpy(&status, (void*)&flashStatus, sizeof(SystemStatus));
        extern void UART1_WriteString(const char*);
        UART1_WriteString("Loaded saved status\r\n");
    } else {
        // Initialize with defaults and save
        memset(&status, 0, sizeof(SystemStatus));
        status.validKey = FLASH_VALID_KEY;
        
        // Set default values in token processing order
        status.presetTime = 300;     // Default preset time (5 minutes)
        status.counts = 0;           // Default counts (total accumulated counts)
        status.noOfChannels = 1024;  // Default number of channels (spectrum size)
        status.startChannel = 0;     // Default start channel
        status.endChannel = 1023;    // Default end channel
        status.LLD = 10;             // Default LLD
        status.ULD = 1000;           // Default ULD
        status.courseGain = 32;      // Mid-range course gain
        status.fineGain = 128;       // Mid-range fine gain
        status.inputPolarity = 1;    // Default positive polarity
        status.threshold = 50;       // Default threshold
        status.riseTime = 1000;      // Default rise time
        status.flatTime = 1000;      // Default flat time
        status.poleZero = 500;       // Default pole-zero
        status.digitalBLR = 500;     // Default digital baseline restore
        status.pileupReject = 50;    // Default pile-up reject
        status.voltage = 1000;       // Default voltage/HV
        status.hvStatus = 0;         // HV off by default
        status.dwellTime = 100;      // Default dwell time for MCS mode
        status.mode = 0;             // PHA mode by default
        
        saveStatusToFlash();
        extern void UART1_WriteString(const char*);
        UART1_WriteString("Initialized new status\r\n");
    }
}

void saveStatusToFlash(void) {
    uint32_t rowBuffer[FLASH_ROW_SIZE/4];
    
    // Prepare row data
    memcpy(rowBuffer, &status, sizeof(SystemStatus));
    
    // Write to flash
    bool success = writeFlashRow((uint32_t*)&flashStatus, rowBuffer);
    
    extern void UART1_WriteString(const char*);
    if(success) {
//        UART1_WriteString("Status saved to flash\r\n"); 
    } else {
        UART1_WriteString("Error saving to flash\r\n");
    }
}

void printStatusUpdate(WriteStringFn writeString, const SystemStatus* status)
{
    if(writeString != NULL)
    {
        writeString(getStatusString(status));
    }
}

// Updated updateAndPrintStatus function with optional printing control
// Parameters are now in the same order as getStatusString
void updateAndPrintStatus(WriteStringFn writeString, SystemStatus* status, const char* parameter, int value)
{
    bool updated = false;
    bool printAfterUpdate = true; // By default, print status after update
    
    // Check if this is a special case where we don't want to print
    if (parameter != NULL && parameter[0] == '!') {
        // If parameter starts with '!', it means don't print after update
        parameter++; // Skip the '!' character
        printAfterUpdate = false;
    }
    
    // Handle parameters in the same order as getStatusString
    if(strcmp(parameter, "mode") == 0)
    {
        status->mode = value;
        updated = true;
    }
    else if(strcmp(parameter, "presetTime") == 0)
    {
        status->presetTime = value;
        updated = true;
    }
    else if(strcmp(parameter, "counts") == 0)
    {
        status->counts = value;
        updated = true;
    }
    else if(strcmp(parameter, "startChannel") == 0)
    {
        status->startChannel = value;
        updated = true;
    }
    else if(strcmp(parameter, "endChannel") == 0)
    {
        status->endChannel = value;
        updated = true;
    }
        else if(strcmp(parameter, "noOfChannels") == 0)
    {
        status->noOfChannels = value;
        updated = true;
    }
    else if(strcmp(parameter, "LLD") == 0)
    {
        status->LLD = value;
        updated = true;
    }
    else if(strcmp(parameter, "ULD") == 0)
    {
        status->ULD = value;
        updated = true;
    }
    else if(strcmp(parameter, "courseGain") == 0)
    {
        status->courseGain = value;
        updated = true;
    }
    else if(strcmp(parameter, "fineGain") == 0)
    {
        status->fineGain = value;
        updated = true;
    }
    else if(strcmp(parameter, "inputPolarity") == 0 || strcmp(parameter, "polarity") == 0)
    {
        // Handle both parameter names, but store in inputPolarity
        status->inputPolarity = value;
        updated = true;
    }
    else if(strcmp(parameter, "threshold") == 0 || strcmp(parameter, "thresholdTime") == 0)
    {
        // Handle both parameter names, but store in threshold
        status->threshold = value;
        updated = true;
    }
    else if(strcmp(parameter, "riseTime") == 0)
    {
        status->riseTime = value;
        updated = true;
    }
    else if(strcmp(parameter, "flatTime") == 0)
    {
        status->flatTime = value;
        updated = true;
    }
    else if(strcmp(parameter, "poleZero") == 0)
    {
        status->poleZero = value;
        updated = true;
    }
    else if(strcmp(parameter, "digitalBLR") == 0 || strcmp(parameter, "digitalBL") == 0)
    {
        // Handle both parameter names, but store in digitalBLR
        status->digitalBLR = value;
        updated = true;
    }
    else if(strcmp(parameter, "pileupReject") == 0)
    {
        status->pileupReject = value;
        updated = true;
    }
    else if(strcmp(parameter, "voltage") == 0 || strcmp(parameter, "HV") == 0)
    {
        status->voltage = value;
        updated = true;
    }
    else if(strcmp(parameter, "hvStatus") == 0 || strcmp(parameter, "HV_on_off") == 0)
    {
        status->hvStatus = value;
        updated = true;
    }
    else if(strcmp(parameter, "dwellTime") == 0)
    {
        status->dwellTime = value;
        updated = true;
    }
    
    // Save to flash only if a value was updated
    if(updated)
    {
        extern void saveStatus(void); // Declare the EEPROM save function
        saveStatus(); // Save to EEPROM
    }
    
    // Print current status only if requested
    if(writeString != NULL && printAfterUpdate)
    {
        writeString(getStatusString(status));
    }
}