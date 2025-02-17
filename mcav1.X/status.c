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

// Function to get status string
const char* getStatusString(const SystemStatus* status)
{
    static char buffer[256];
    sprintf(buffer, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
            status->systemState,
            status->voltage,
            status->courseGain,
            status->fineGain,
            status->digitalGain,
            status->polarity,
            status->thresholdTime,
            status->riseTime,
            status->flatTime,
            status->poleZero,
            status->digitalBL,
            status->pileupReject,
            status->presetTime);
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
        UART1_WriteString("Status saved to flash\r\n"); 
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

void updateAndPrintStatus(WriteStringFn writeString, SystemStatus* status, const char* parameter, int value)
{
    bool updated = false;
    
    if(strcmp(parameter, "systemState") == 0)
    {
        status->systemState = value;
        updated = true;
    }
    else if(strcmp(parameter, "voltage") == 0)
    {
        status->voltage = value;
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
    else if(strcmp(parameter, "digitalGain") == 0)
    {
        status->digitalGain = value;
        updated = true;
    }
    else if(strcmp(parameter, "polarity") == 0)
    {
        status->polarity = value;
        updated = true;
    }
    else if(strcmp(parameter, "thresholdTime") == 0)
    {
        status->thresholdTime = value;
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
    else if(strcmp(parameter, "digitalBL") == 0)
    {
        status->digitalBL = value;
        updated = true;
    }
    else if(strcmp(parameter, "pileupReject") == 0)
    {
        status->pileupReject = value;
        updated = true;
    }
    else if(strcmp(parameter, "presetTime") == 0)
    {
        status->presetTime = value;
        updated = true;
    }
    
    // Save to flash only if a value was updated
    if(updated)
    {
        saveStatusToFlash();
    }
    
    // Print current status
    if(writeString != NULL)
    {
        writeString(getStatusString(status));
    }
}