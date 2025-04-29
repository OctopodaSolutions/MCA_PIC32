/*******************************************************************************
  Main Source File with EEPROM Storage for PHA/MCA Settings and MCP2200 Integration
  Company: Microchip Technology Inc.
  File Name: main.c
  Summary: Integrates UART1, UART2, I2C1, and SPI1 for FPGA and EEPROM interaction, with MCP2200 for USB communication.
  Description: Parses commands, controls FPGA, DAC, and auto-saves settings to EEPROM on change. Uses MCP2200 for USB-to-UART communication.
           Modified to read 512-byte FPGA spectrum data every 5 seconds and print via UART1.
*******************************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <device.h>
#include "definitions.h"

// Common defines
#define RX_BUFFER_SIZE 256      // For user input (UART1)
#define TX_BUFFER_SIZE 512      // For responses (UART1, UART2)
#define SPECTRUM_DATA_SIZE 512  // FPGA spectrum data size
#define MCP4725_ADDRESS 0x60    // MCP4725 I2C address
char buffer[64];

// EEPROM defines
#define EEPROM_READ        0x03  // Read instruction for 25LC640A
#define EEPROM_WRITE       0x02  // Write instruction for 25LC640A
#define EEPROM_WREN        0x06  // Write Enable instruction
#define EEPROM_RDSR        0x05  // Read Status Register instruction
#define EEPROM_CS_PIN      LATCbits.LATC7
#define EEPROM_CS_LOW()    (EEPROM_CS_PIN = 0)
#define EEPROM_CS_HIGH()   (EEPROM_CS_PIN = 1)
#define EEPROM_ADDR_MODE   0x0000  // 1 byte for mode (PHA/MCA)
#define EEPROM_ADDR_SETTINGS 0x0001  // Start address for settings
#define MAX_EEPROM_SIZE    100    // Max bytes for settings
#define EEPROM_TOTAL_SIZE  8192   // 25LC640A total size in bytes

// Delay defines (assuming 48 MHz system clock)
#define DELAY_5MS_CYCLES 240000  // 5ms at 48 MHz
#define DELAY_1MS_CYCLES 48000   // 1ms at 48 MHz

// MCP2200 defines
#define UART1_BAUDRATE 115200
#define PERIPHERAL_CLOCK_FREQ 48000000UL // 48 MHz clock

// Device Information Defines
#define MODEL_NUMBER "NTPL-XMCA-001"
#define SERIAL_NUMBER "NTPL-250421-001"
#define CALIBRATION_EXPIRY "20-04-2026"
#define VERSION "VER-1.0"

// Mode flags
#define MODE_PHA 1
#define MODE_MCA 2

// FPGA command defines
#define FPGA_START_CMD  0x01
#define FPGA_STOP_CMD   0x02
#define FPGA_RESET_CMD  0x03
#define FPGA_START_BIT  0xBB
#define FPGA_END_BIT    0xCC

// Structure to hold all settings
typedef struct {
    int mode;              // PHA or MCA
    int presetTime;
    int counts;
    int startCh;
    int endCh;
    int noOfCh;
    int lld;
    int uld;
    int coarseGain;
    int fineGain;
    int polarity;
    int threshold;
    int riseTime;
    int flatTime;
    int poleZeros;
    int digitalBlr;
    int pileupReject;
    int highVoltage;
    int hvOnOff;
    int dwellTime;         // MCA only
} Settings_t;

static uint8_t uart1RxBuffer[RX_BUFFER_SIZE];
static uint8_t uart1TxBuffer[TX_BUFFER_SIZE];
static uint8_t uart2RxBuffer[TX_BUFFER_SIZE];
static uint8_t spectrumData[SPECTRUM_DATA_SIZE]; // Buffer for 512-byte FPGA data
static volatile bool isAcquisitionRunning = false;
static Settings_t currentSettings = {0};
static Settings_t lastSavedSettings = {0};
static volatile bool uart1RxComplete = false;
static volatile uint32_t uart1CallbackCount = 0;
static volatile bool spectrumDataReceived = false; // Flag for complete spectrum data
static uint32_t spectrumDataIndex = 0; // Current index in spectrumData buffer
static bool expectingStartByte = true; // State machine for parsing UART2 data
static volatile uint32_t lastSpectrumReadTime = 0; // Last time spectrum data was processed (Timer1 ticks)
static const uint32_t SPECTRUM_READ_INTERVAL = 5000; // 5 seconds in milliseconds (T

// Function to delay for a specified number of cycles (approximate)

#define TIMER1_PRESCALER 256
#define TIMER1_PERIOD 187 // 48 MHz / 256 / 1000 = 187.5 (~1 ms ticks)


void Timer1_Init(void) {
    T1CON = 0; // Clear Timer1 control register
    T1CONbits.TCKPS = 3; // Prescaler 1:256
    TMR1 = 0; // Clear timer counter
    PR1 = TIMER1_PERIOD; // Set period for 1 ms
    IFS0bits.T1IF = 0; // Clear interrupt flag
    T1CONbits.ON = 1; // Enable Timer1
}

// Get current Timer1 tick count (approximate milliseconds)
uint32_t Timer1_GetTicks(void) {
    return TMR1 + (IFS0bits.T1IF ? PR1 : 0); // Add period if overflow occurred
}


void delay_cycles(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++);
}

// Send message via UART1 (routed through MCP2200 to USB)
static void SendUART1Message(const char *message) {
    size_t len = strlen(message);
    if (len >= TX_BUFFER_SIZE - 2) {
        len = TX_BUFFER_SIZE - 2; // Reserve space for \r\n
    }
    memcpy(uart1TxBuffer, message, len);
    uart1TxBuffer[len] = '\r';
    uart1TxBuffer[len + 1] = '\n';
    len += 2;

    if (!UART1_Write(uart1TxBuffer, len)) {
        return; // Cannot log failure
    }

    uint32_t timeout = 1000000;
    while (UART1_WriteIsBusy() && timeout--);

    UART_ERROR errors = UART1_ErrorGet();
    if (errors != UART_ERROR_NONE) {
        return;
    }
}

// Print 512-byte spectrum data as hex via UART1
static void PrintSpectrumData(void) {
    char hexBuffer[64]; // Increased to 64 bytes to hold 48-byte output
    SendUART1Message("Received FPGA Spectrum Data (512 bytes):");

    for (uint32_t i = 0; i < SPECTRUM_DATA_SIZE; i += 16) {
        // Print 16 bytes per line
        snprintf(hexBuffer, sizeof(hexBuffer),
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 spectrumData[i], spectrumData[i + 1], spectrumData[i + 2], spectrumData[i + 3],
                 spectrumData[i + 4], spectrumData[i + 5], spectrumData[i + 6], spectrumData[i + 7],
                 spectrumData[i + 8], spectrumData[i + 9], spectrumData[i + 10], spectrumData[i + 11],
                 spectrumData[i + 12], spectrumData[i + 13], spectrumData[i + 14], spectrumData[i + 15]);
        SendUART1Message(hexBuffer);
    }
    SendUART1Message("End of Spectrum Data");
}

// MCP2200 Initialization Functions
void InitMCP2200(unsigned int VendorID, unsigned int ProductID) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Initializing MCP2200: VID=0x%04X, PID=0x%04X", VendorID, ProductID);
    SendUART1Message(buffer);
}

bool ConfigureMCP2200(unsigned char IOMap, unsigned long BaudRate, unsigned int RxLED, unsigned int TxLED, bool HardwareFlowControl, bool USBCFG, bool Suspend) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Configuring MCP2200: IOMap=0x%02X, BaudRate=%lu, RxLED=%d, TxLED=%d, HWFlowControl=%d",
             IOMap, BaudRate, RxLED, TxLED, HardwareFlowControl);
    SendUART1Message(buffer);
    return true; // Assume pre-configured
}

bool initialize_mcp2200(void) {
    SendUART1Message("Starting MCP2200 initialization");
    InitMCP2200(0x04D8, 0x00DF);
    if (!ConfigureMCP2200(0x3D, UART1_BAUDRATE, 1, 1, false, false, false)) {
        SendUART1Message("Error: MCP2200 configuration failed");
        return false;
    }
    SendUART1Message("MCP2200 initialization successful");
    delay_cycles(DELAY_1MS_CYCLES * 500);
    return true;
}

// UART Callbacks (only for UART1)
void UART1_RxCallback(uintptr_t context) {
    uart1RxComplete = true;
    uart1CallbackCount++;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "UART1 RX Callback: %u bytes, count: %u", UART1_ReadCountGet(), uart1CallbackCount);
    SendUART1Message(buffer);
}

// EEPROM Functions
uint8_t EEPROM_ReadStatus(void) {
    uint8_t txBuffer[2] = {EEPROM_RDSR, 0xFF};
    uint8_t rxBuffer[2];
    
    EEPROM_CS_LOW();
    SPI1_WriteRead(txBuffer, 2, rxBuffer, 2);
    while (SPI1_IsBusy());
    EEPROM_CS_HIGH();
    for (volatile uint32_t i = 0; i < DELAY_1MS_CYCLES; i++);
    return rxBuffer[1];
}

void EEPROM_WaitUntilReady(void) {
    uint32_t timeout = 1000000;
    while ((EEPROM_ReadStatus() & 0x01) && timeout--) {
        if (timeout == 0) {
            SendUART1Message("EEPROM Busy Timeout");
            break;
        }
    }
    for (volatile uint32_t i = 0; i < DELAY_5MS_CYCLES; i++);
}

void EEPROM_WriteEnable(void) {
    uint8_t txBuffer = EEPROM_WREN;
    
    EEPROM_CS_LOW();
    SPI1_Write(&txBuffer, 1);
    while (SPI1_IsBusy());
    EEPROM_CS_HIGH();
    for (volatile uint32_t i = 0; i < DELAY_1MS_CYCLES; i++);
}

bool EEPROM_WriteBytes(uint16_t address, uint8_t* data, uint8_t length) {
    if (length > MAX_EEPROM_SIZE || address + length > EEPROM_TOTAL_SIZE) {
        SendUART1Message("EEPROM: Invalid address or length exceeds capacity");
        return false;
    }
    
    uint8_t txBuffer[MAX_EEPROM_SIZE + 3];
    txBuffer[0] = EEPROM_WRITE;
    
    uint8_t bytesLeft = length;
    uint16_t currentAddr = address;
    uint8_t* dataPtr = data;
    
    while (bytesLeft > 0) {
        uint8_t chunkSize = (bytesLeft > 32) ? 32 : bytesLeft;
        txBuffer[1] = (currentAddr >> 8) & 0xFF;
        txBuffer[2] = currentAddr & 0xFF;
        memcpy(&txBuffer[3], dataPtr, chunkSize);
        
        EEPROM_WriteEnable();
        EEPROM_CS_LOW();
        SPI1_Write(txBuffer, chunkSize + 3);
        
        uint32_t timeout = 1000000;
        while (SPI1_IsBusy() && timeout--) {
            if (timeout == 0) {
                SendUART1Message("EEPROM: SPI Write Timeout");
                EEPROM_CS_HIGH();
                return false;
            }
        }
        EEPROM_CS_HIGH();
        for (volatile uint32_t i = 0; i < DELAY_5MS_CYCLES; i++);
        EEPROM_WaitUntilReady();
        bytesLeft -= chunkSize;
        currentAddr += chunkSize;
        dataPtr += chunkSize;
    }
    return true;
}

bool EEPROM_ReadBytes(uint16_t address, uint8_t* buffer, uint8_t length) {
    if (length > MAX_EEPROM_SIZE || address + length > EEPROM_TOTAL_SIZE) {
        SendUART1Message("EEPROM: Read size exceeds maximum or invalid address");
        return false;
    }
    
    uint8_t txBuffer[MAX_EEPROM_SIZE + 3] = {EEPROM_READ, (address >> 8) & 0xFF, address & 0xFF};
    uint8_t rxBuffer[MAX_EEPROM_SIZE + 3];
    
    EEPROM_CS_LOW();
    SPI1_WriteRead(txBuffer, length + 3, rxBuffer, length + 3);
    
    uint32_t timeout = 1000000;
    while (SPI1_IsBusy() && timeout--) {
        if (timeout == 0) {
            SendUART1Message("EEPROM: SPI Read Timeout");
            EEPROM_CS_HIGH();
            return false;
        }
    }
    EEPROM_CS_HIGH();
    for (volatile uint32_t i = 0; i < DELAY_1MS_CYCLES; i++);
    memcpy(buffer, &rxBuffer[3], length);
    return true;
}

// Auto-save settings when changed
void AutoSaveSettings(void) {
    if (memcmp(&currentSettings, &lastSavedSettings, sizeof(Settings_t)) != 0) {
        SendUART1Message("Settings changed, auto-saving to EEPROM...");
        
        uint8_t buffer[MAX_EEPROM_SIZE] = {0};
        buffer[0] = (uint8_t)currentSettings.mode;
        memcpy(&buffer[1], &currentSettings.presetTime, sizeof(int));
        memcpy(&buffer[5], &currentSettings.counts, sizeof(int));
        memcpy(&buffer[9], &currentSettings.startCh, sizeof(int));
        memcpy(&buffer[13], &currentSettings.endCh, sizeof(int));
        memcpy(&buffer[17], &currentSettings.noOfCh, sizeof(int));
        memcpy(&buffer[21], &currentSettings.lld, sizeof(int));
        memcpy(&buffer[25], &currentSettings.uld, sizeof(int));
        memcpy(&buffer[29], &currentSettings.coarseGain, sizeof(int));
        memcpy(&buffer[33], &currentSettings.fineGain, sizeof(int));
        memcpy(&buffer[37], &currentSettings.polarity, sizeof(int));
        memcpy(&buffer[41], &currentSettings.threshold, sizeof(int));
        memcpy(&buffer[45], &currentSettings.riseTime, sizeof(int));
        memcpy(&buffer[49], &currentSettings.flatTime, sizeof(int));
        memcpy(&buffer[53], &currentSettings.poleZeros, sizeof(int));
        memcpy(&buffer[57], &currentSettings.digitalBlr, sizeof(int));
        memcpy(&buffer[61], &currentSettings.pileupReject, sizeof(int));
        memcpy(&buffer[65], &currentSettings.highVoltage, sizeof(int));
        memcpy(&buffer[69], &currentSettings.hvOnOff, sizeof(int));
        memcpy(&buffer[73], &currentSettings.dwellTime, sizeof(int));
        
        char settingsStr[256];
        if (currentSettings.mode == MODE_PHA) {
            snprintf(settingsStr, 256, "Writing: PHA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                     currentSettings.presetTime, currentSettings.counts, currentSettings.startCh,
                     currentSettings.endCh, currentSettings.noOfCh, currentSettings.lld,
                     currentSettings.uld, currentSettings.coarseGain, currentSettings.fineGain,
                     currentSettings.polarity, currentSettings.threshold, currentSettings.riseTime,
                     currentSettings.flatTime, currentSettings.poleZeros, currentSettings.digitalBlr,
                     currentSettings.pileupReject, currentSettings.highVoltage, currentSettings.hvOnOff);
        } else {
            snprintf(settingsStr, 256, "Writing: MCA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                     currentSettings.presetTime, currentSettings.counts, currentSettings.startCh,
                     currentSettings.endCh, currentSettings.noOfCh, currentSettings.lld,
                     currentSettings.uld, currentSettings.coarseGain, currentSettings.fineGain,
                     currentSettings.polarity, currentSettings.threshold, currentSettings.riseTime,
                     currentSettings.flatTime, currentSettings.poleZeros, currentSettings.digitalBlr,
                     currentSettings.pileupReject, currentSettings.highVoltage, currentSettings.hvOnOff,
                     currentSettings.dwellTime);
        }
        SendUART1Message(settingsStr);
        
        if (!EEPROM_WriteBytes(EEPROM_ADDR_MODE, buffer,
                              currentSettings.mode == MODE_PHA ? 76 : 80)) {
            SendUART1Message("EEPROM: Failed to write settings");
            return;
        }
        
        SendUART1Message("Settings written");
        
        uint8_t readBuffer[MAX_EEPROM_SIZE];
        if (EEPROM_ReadBytes(EEPROM_ADDR_MODE, readBuffer,
                            currentSettings.mode == MODE_PHA ? 76 : 80)) {
            int mode = readBuffer[0];
            int presetTime, counts, startCh, endCh, noOfCh, lld, uld, coarseGain, fineGain, polarity,
                threshold, riseTime, flatTime, poleZeros, digitalBlr, pileupReject, highVoltage, hvOnOff, dwellTime;
            memcpy(&presetTime, &readBuffer[1], sizeof(int));
            memcpy(&counts, &readBuffer[5], sizeof(int));
            memcpy(&startCh, &readBuffer[9], sizeof(int));
            memcpy(&endCh, &readBuffer[13], sizeof(int));
            memcpy(&noOfCh, &readBuffer[17], sizeof(int));
            memcpy(&lld, &readBuffer[21], sizeof(int));
            memcpy(&uld, &readBuffer[25], sizeof(int));
            memcpy(&coarseGain, &readBuffer[29], sizeof(int));
            memcpy(&fineGain, &readBuffer[33], sizeof(int));
            memcpy(&polarity, &readBuffer[37], sizeof(int));
            memcpy(&threshold, &readBuffer[41], sizeof(int));
            memcpy(&riseTime, &readBuffer[45], sizeof(int));
            memcpy(&flatTime, &readBuffer[49], sizeof(int));
            memcpy(&poleZeros, &readBuffer[53], sizeof(int));
            memcpy(&digitalBlr, &readBuffer[57], sizeof(int));
            memcpy(&pileupReject, &readBuffer[61], sizeof(int));
            memcpy(&highVoltage, &readBuffer[65], sizeof(int));
            memcpy(&hvOnOff, &readBuffer[69], sizeof(int));
            memcpy(&dwellTime, &readBuffer[73], sizeof(int));
            
            char readStr[256];
            if (mode == MODE_PHA) {
                snprintf(readStr, 256, "Read back: PHA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                         presetTime, counts, startCh, endCh, noOfCh, lld, uld, coarseGain, fineGain,
                         polarity, threshold, riseTime, flatTime, poleZeros, digitalBlr, pileupReject,
                         highVoltage, hvOnOff);
            } else {
                snprintf(readStr, 256, "Read back: MCA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                         presetTime, counts, startCh, endCh, noOfCh, lld, uld, coarseGain, fineGain,
                         polarity, threshold, riseTime, flatTime, poleZeros, digitalBlr, pileupReject,
                         highVoltage, hvOnOff, dwellTime);
            }
            SendUART1Message(readStr);
            
            if (memcmp(buffer, readBuffer,
                      currentSettings.mode == MODE_PHA ? 76 : 80) == 0) {
                SendUART1Message("EEPROM: Write verification successful");
            } else {
                SendUART1Message("EEPROM: Write verification failed - data mismatch");
            }
        } else {
            SendUART1Message("EEPROM: Read-back verification failed");
        }
        
        memcpy(&lastSavedSettings, &currentSettings, sizeof(Settings_t));
        SendUART1Message("Settings auto-saved to EEPROM successfully");
    }
}

// Load settings from EEPROM
bool LoadSettingsFromEEPROM(Settings_t *settings) {
    SendUART1Message("Loading settings from EEPROM...");
    
    uint8_t eepromBuffer[MAX_EEPROM_SIZE];
    if (!EEPROM_ReadBytes(EEPROM_ADDR_MODE, eepromBuffer, MAX_EEPROM_SIZE)) {
        SendUART1Message("EEPROM: Failed to read settings");
        return false;
    }
    
    char hex[50];
    snprintf(hex, sizeof(hex), "EEPROM Raw: %02X %02X %02X %02X",
             eepromBuffer[0], eepromBuffer[1], eepromBuffer[2], eepromBuffer[3]);
    SendUART1Message(hex);
    
    settings->mode = eepromBuffer[0];
    if (settings->mode != MODE_PHA && settings->mode != MODE_MCA) {
        SendUART1Message("No valid settings found in EEPROM");
        return false;
    }
    
    memcpy(&settings->presetTime, &eepromBuffer[1], sizeof(int));
    memcpy(&settings->counts, &eepromBuffer[5], sizeof(int));
    memcpy(&settings->startCh, &eepromBuffer[9], sizeof(int));
    memcpy(&settings->endCh, &eepromBuffer[13], sizeof(int));
    memcpy(&settings->noOfCh, &eepromBuffer[17], sizeof(int));
    memcpy(&settings->lld, &eepromBuffer[21], sizeof(int));
    memcpy(&settings->uld, &eepromBuffer[25], sizeof(int));
    memcpy(&settings->coarseGain, &eepromBuffer[29], sizeof(int));
    memcpy(&settings->fineGain, &eepromBuffer[33], sizeof(int));
    memcpy(&settings->polarity, &eepromBuffer[37], sizeof(int));
    memcpy(&settings->threshold, &eepromBuffer[41], sizeof(int));
    memcpy(&settings->riseTime, &eepromBuffer[45], sizeof(int));
    memcpy(&settings->flatTime, &eepromBuffer[49], sizeof(int));
    memcpy(&settings->poleZeros, &eepromBuffer[53], sizeof(int));
    memcpy(&settings->digitalBlr, &eepromBuffer[57], sizeof(int));
    memcpy(&settings->pileupReject, &eepromBuffer[61], sizeof(int));
    memcpy(&settings->highVoltage, &eepromBuffer[65], sizeof(int));
    memcpy(&settings->hvOnOff, &eepromBuffer[69], sizeof(int));
    memcpy(&settings->dwellTime, &eepromBuffer[73], sizeof(int));
    
    char settingsStr[256];
    if (settings->mode == MODE_PHA) {
        snprintf(settingsStr, sizeof(settingsStr), "Loaded: PHA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                 settings->presetTime, settings->counts, settings->startCh,
                 settings->endCh, settings->noOfCh, settings->lld,
                 settings->uld, settings->coarseGain, settings->fineGain,
                 settings->polarity, settings->threshold, settings->riseTime,
                 settings->flatTime, settings->poleZeros, settings->digitalBlr,
                 settings->pileupReject, settings->highVoltage, settings->hvOnOff);
    } else {
        snprintf(settingsStr, sizeof(settingsStr), "Loaded: MCA,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d,%.10d",
                 settings->presetTime, settings->counts, settings->startCh,
                 settings->endCh, settings->noOfCh, settings->lld,
                 settings->uld, settings->coarseGain, settings->fineGain,
                 settings->polarity, settings->threshold, settings->riseTime,
                 settings->flatTime, settings->poleZeros, settings->digitalBlr,
                 settings->pileupReject, settings->highVoltage, settings->hvOnOff,
                 settings->dwellTime);
    }
    SendUART1Message(settingsStr);
    
    snprintf(buffer, sizeof(buffer), "Settings loaded: Mode=%s, HV=%d, HVON=%d",
             (settings->mode == MODE_PHA) ? "PHA" : "MCA",
             settings->highVoltage, settings->hvOnOff);
    SendUART1Message(buffer);
    
    return true;
}

// Write HV value to MCP4725
static bool MCP4725_WriteDAC(uint16_t hvValue, uint8_t hvOn) {
    if (hvValue > 1000) hvValue = 1000;
    if (!hvOn) hvValue = 0;

    uint16_t dacValue = (uint16_t)(((float)hvValue / 1000.0) * 2048.5);
    uint8_t data[2] = {(dacValue >> 8) & 0x0F, dacValue & 0xFF};

    bool success = I2C1_Write(MCP4725_ADDRESS, data, 2);
    while (I2C1_IsBusy());
    
    if (success) {
        float dacVoltage = (float)dacValue * 5.0 / 4095.0;
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "MCP4725 DAC: HV %s set to %uV (DAC: %u, %.3fV)",
                 hvOn ? "ON" : "OFF", hvValue, dacValue, dacVoltage);
        SendUART1Message(buffer);
    } else {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "MCP4725: DAC write failed, I2C Error: %u", I2C1_ErrorGet());
        SendUART1Message(buffer);
    }
    return success;
}

// Print device information
static void PrintDeviceInfo(void) {
    char tempBuffer[TX_BUFFER_SIZE];
    snprintf(tempBuffer, TX_BUFFER_SIZE, "%s,%s,%s,%s",
             MODEL_NUMBER, SERIAL_NUMBER, CALIBRATION_EXPIRY, VERSION);
    SendUART1Message("Device Info:");
    SendUART1Message(tempBuffer);
}

// Send initial prompt to user via UART1
static void SendInitialPrompt(void) {
    SendUART1Message("USB Communication via MCP2200 Established");
    SendUART1Message("Enter: MODE,TIME,COUNTS,START,END,CH,LLD,ULD,CG,FG,POL,THR,RISE,FLAT,PZ,BLR,PUR,HV(0-1000V),HVON [MCA:DWELL]");
    SendUART1Message("Commands: START, STOP, RESET, LOAD, INFO");
    SendUART1Message("Settings auto-save to EEPROM on change. Use 'LOAD' to restore previous settings.");
    SendUART1Message("Spectrum data (512 bytes) printed every 5 seconds when acquisition is running.");
}

// Process UART2 RX data (polling-based, reading from Arduino Serial2)
// Process UART2 RX data (polling-based, reading from Arduino Serial2)
static void ProcessUART2Rx(void) {
    size_t bytesRead = UART2_ReadCountGet();
    if (bytesRead == 0) {
        return;
    }

    // Process each byte in the UART2 buffer
    for (size_t i = 0; i < bytesRead; i++) {
        uint8_t byte = uart2RxBuffer[i];

        if (expectingStartByte) {
            if (byte == FPGA_START_BIT) {
                expectingStartByte = false;
                spectrumDataIndex = 0;
                spectrumDataReceived = false;
                SendUART1Message("UART2: Detected start byte (0xBB)");
            } else {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "UART2: Expected start byte (0xBB), got 0x%02X", byte);
                SendUART1Message(buffer);
            }
            continue;
        }

        if (spectrumDataIndex < SPECTRUM_DATA_SIZE) {
            // Store data bytes
            spectrumData[spectrumDataIndex++] = byte;
            if (spectrumDataIndex == SPECTRUM_DATA_SIZE) {
                // Expecting end byte next
                SendUART1Message("UART2: Received 512 bytes, expecting end byte (0xCC)");
            }
            continue;
        }

        if (spectrumDataIndex == SPECTRUM_DATA_SIZE && byte == FPGA_END_BIT) {
            // Complete packet received
            spectrumDataReceived = true;
            expectingStartByte = true;
            SendUART1Message("UART2: Complete spectrum data packet received (0xBB [512 bytes] 0xCC)");
            PrintSpectrumData();
            lastSpectrumReadTime = Timer1_GetTicks(); // Update last read time
        } else {
            // Invalid end byte or out-of-sequence data
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "UART2: Expected end byte (0xCC), got 0x%02X", byte);
            SendUART1Message(buffer);
            expectingStartByte = true;
            spectrumDataIndex = 0;
            spectrumDataReceived = false;
        }
    }

    // Clear buffer and restart read
    UART2_ReadAbort();
    memset(uart2RxBuffer, 0, TX_BUFFER_SIZE);
    UART2_Read(uart2RxBuffer, TX_BUFFER_SIZE);
}

static void ProcessUserInput(void) {
    if (uart1RxComplete) {
        uart1RxComplete = false;
        
        size_t bytesRead = UART1_ReadCountGet();
        if (bytesRead == 0) {
            SendUART1Message("UART1: No data received");
            goto clear_and_read;
        }
        
        if (bytesRead < RX_BUFFER_SIZE) {
            uart1RxBuffer[bytesRead] = '\0';
        } else {
            uart1RxBuffer[RX_BUFFER_SIZE - 1] = '\0';
            SendUART1Message("UART1: Input truncated to buffer size");
        }

        // Trim trailing whitespace/newlines
        char *end = (char *)uart1RxBuffer + strlen((char *)uart1RxBuffer) - 1;
        while (end >= (char *)uart1RxBuffer && (*end == '\n' || *end == '\r' || *end == ' ')) {
            *end = '\0';
            end--;
        }

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Raw Input (%u bytes): '%.30s'", bytesRead, uart1RxBuffer);
        SendUART1Message(buffer);

        // Check for single-word commands
        if (strcmp((char *)uart1RxBuffer, "START") == 0) {
            SendUART1Message("Processing START command");
            if (!isAcquisitionRunning) {
                isAcquisitionRunning = true;
                lastSpectrumReadTime = Timer1_GetTicks(); // Reset timer
                SendUART1Message("Acquisition started");
                
                uint8_t startCmd[3] = {FPGA_START_BIT, FPGA_START_CMD, FPGA_END_BIT};
                if (UART2_Write(startCmd, 3)) {
                    uint32_t timeout = 1000000;
                    while (UART2_WriteIsBusy() && timeout--);
                    if (timeout == 0) {
                        SendUART1Message("UART2: TX timeout");
                    } else {
                        SendUART1Message("FPGA: Start command sent");
                    }
                    delay_cycles(DELAY_1MS_CYCLES * 100); // Wait 100ms for response
                    UART2_Read(uart2RxBuffer, TX_BUFFER_SIZE); // Start reading data
                } else {
                    SendUART1Message("UART2: Failed to send start command");
                }
            } else {
                SendUART1Message("Acquisition already running");
            }
            goto clear_and_read;
        }
        else if (strcmp((char *)uart1RxBuffer, "STOP") == 0) {
            SendUART1Message("Processing STOP command");
            if (isAcquisitionRunning) {
                isAcquisitionRunning = false;
                SendUART1Message("Acquisition stopped");
                
                uint8_t stopCmd[3] = {FPGA_START_BIT, FPGA_STOP_CMD, FPGA_END_BIT};
                if (UART2_Write(stopCmd, 3)) {
                    uint32_t timeout = 1000000;
                    while (UART2_WriteIsBusy() && timeout--);
                    if (timeout == 0) {
                        SendUART1Message("UART2: TX timeout");
                    } else {
                        SendUART1Message("FPGA: Stop command sent");
                    }
                    delay_cycles(DELAY_1MS_CYCLES * 100); // Wait 100ms for response
                    UART2_Read(uart2RxBuffer, TX_BUFFER_SIZE); // Start reading data
                } else {
                    SendUART1Message("UART2: Failed to send stop command");
                }
            } else {
                SendUART1Message("Acquisition not running");
            }
            goto clear_and_read;
        }
        else if (strcmp((char *)uart1RxBuffer, "RESET") == 0) {
            SendUART1Message("Processing RESET command");
            isAcquisitionRunning = false;
            currentSettings.counts = 0;
            SendUART1Message("System reset (EEPROM preserved)");
            
            uint8_t resetCmd[3] = {FPGA_START_BIT, FPGA_RESET_CMD, FPGA_END_BIT};
            if (UART2_Write(resetCmd, 3)) {
                uint32_t timeout = 1000000;
                while (UART2_WriteIsBusy() && timeout--);
                if (timeout == 0) {
                    SendUART1Message("UART2: TX timeout");
                } else {
                    SendUART1Message("FPGA: Reset command sent");
                }
                delay_cycles(DELAY_1MS_CYCLES * 100); // Wait 100ms for response
                UART2_Read(uart2RxBuffer, TX_BUFFER_SIZE); // Start reading data
            } else {
                SendUART1Message("UART2: Failed to send reset command");
            }
            goto clear_and_read;
        }
        else if (strcmp((char *)uart1RxBuffer, "LOAD") == 0) {
            SendUART1Message("Processing LOAD command");
            if (LoadSettingsFromEEPROM(&currentSettings)) {
                MCP4725_WriteDAC(currentSettings.highVoltage, currentSettings.hvOnOff);
                
                uint8_t fpgaCmd[11] = {
                    FPGA_START_BIT,
                    (uint8_t)(currentSettings.fineGain & 0xFF),
                    (uint8_t)((currentSettings.threshold >> 8) & 0xFF),
                    (uint8_t)(currentSettings.threshold & 0xFF),
                    (uint8_t)((currentSettings.riseTime >> 2) & 0xFF),
                    (uint8_t)(((currentSettings.riseTime & 0x3) << 6) | ((currentSettings.flatTime >> 4) & 0x3F)),
                    (uint8_t)(((currentSettings.flatTime & 0xF) << 4) | ((currentSettings.poleZeros >> 4) & 0xF)),
                    (uint8_t)(((currentSettings.poleZeros & 0xF) << 4) | ((currentSettings.digitalBlr >> 2) & 0xF)),
                    (uint8_t)(((currentSettings.digitalBlr & 0x3) << 6) | (currentSettings.pileupReject & 0x1) << 5 | (currentSettings.polarity & 0x1) << 4),
                    (uint8_t)(currentSettings.mode == MODE_MCA ? (currentSettings.dwellTime & 0xFF) : 0),
                    FPGA_END_BIT
                };
                
                if (UART2_Write(fpgaCmd, 11)) {
                    uint32_t timeout = 1000000;
                    while (UART2_WriteIsBusy() && timeout--);
                    if (timeout == 0) {
                        SendUART1Message("UART2: TX timeout");
                    } else {
                        SendUART1Message("FPGA: Loaded settings applied successfully");
                    }
                } else {
                    SendUART1Message("UART2: Write failed for FPGA settings");
                }
                
                memcpy(&lastSavedSettings, &currentSettings, sizeof(Settings_t));
            }
            goto clear_and_read;
        }
        else if (strcmp((char *)uart1RxBuffer, "INFO") == 0) {
            SendUART1Message("Processing INFO command");
            PrintDeviceInfo();
            goto clear_and_read;
        }

        // Process settings input
        int tokenCount = 1;
        char *temp = (char *)uart1RxBuffer;
        while ((temp = strchr(temp, ',')) != NULL) {
            tokenCount++;
            temp++;
        }

        char *token = strtok((char *)uart1RxBuffer, ",");
        if (!token) {
            SendUART1Message("Invalid format");
            goto clear_and_read;
        }

        bool isMCA = (strcmp(token, "MCA") == 0);
        bool isPHA = (strcmp(token, "PHA") == 0);
        if (!isMCA && !isPHA) {
            SendUART1Message("Invalid mode (PHA/MCA)");
            goto clear_and_read;
        }

        if (isPHA && tokenCount != 19) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "PHA mode requires 18 params, got %d", tokenCount);
            SendUART1Message(buffer);
            goto clear_and_read;
        } else if (isMCA && tokenCount != 20) {
            char buffer[80];
            snprintf(buffer, sizeof(buffer), "MCA mode requires 19 params (incl. DWELL), got %d", tokenCount);
            SendUART1Message(buffer);
            goto clear_and_read;
        }

        Settings_t newSettings = {0};
        newSettings.mode = isPHA ? MODE_PHA : MODE_MCA;

        int paramIndex = 1;
        while ((token = strtok(NULL, ",")) != NULL) {
            if (!token || token[0] == '\0') {
                SendUART1Message("Empty token detected");
                goto clear_and_read;
            }
            int value = atoi(token);
            switch (paramIndex++) {
                case 1: newSettings.presetTime = value; break;
                case 2: newSettings.counts = value; break;
                case 3: newSettings.startCh = (value >= 0) ? value : 0; break;
                case 4: newSettings.endCh = (value <= 4095) ? value : 4095; break;
                case 5: newSettings.noOfCh = value; break;
                case 6: newSettings.lld = value; break;
                case 7: newSettings.uld = value; break;
                case 8: newSettings.coarseGain = value; break;
                case 9: newSettings.fineGain = value; break;
                case 10: newSettings.polarity = (value == 0 || value == 1) ? value : 0; break;
                case 11: newSettings.threshold = value; break;
                case 12: newSettings.riseTime = value; break;
                case 13: newSettings.flatTime = value; break;
                case 14: newSettings.poleZeros = value; break;
                case 15: newSettings.digitalBlr = value; break;
                case 16: newSettings.pileupReject = (value == 0 || value == 1) ? value : 0; break;
                case 17: newSettings.highVoltage = (value <= 1000) ? value : 1000; break;
                case 18: newSettings.hvOnOff = (value == 0 || value == 1) ? value : 0; break;
                case 19: if (isMCA) newSettings.dwellTime = value; break;
            }
        }

        memcpy(&currentSettings, &newSettings, sizeof(Settings_t));
        
        uint8_t fpgaCmd[11] = {
            FPGA_START_BIT,
            (uint8_t)(currentSettings.fineGain & 0xFF),
            (uint8_t)((currentSettings.threshold >> 8) & 0xFF),
            (uint8_t)(currentSettings.threshold & 0xFF),
            (uint8_t)((currentSettings.riseTime >> 2) & 0xFF),
            (uint8_t)(((currentSettings.riseTime & 0x3) << 6) | ((currentSettings.flatTime >> 4) & 0x3F)),
            (uint8_t)(((currentSettings.flatTime & 0xF) << 4) | ((currentSettings.poleZeros >> 4) & 0xF)),
            (uint8_t)(((currentSettings.poleZeros & 0xF) << 4) | ((currentSettings.digitalBlr >> 2) & 0xF)),
            (uint8_t)(((currentSettings.digitalBlr & 0x3) << 6) | (currentSettings.pileupReject & 0x1) << 5 | (currentSettings.polarity & 0x1) << 4),
            (uint8_t)(isMCA ? (currentSettings.dwellTime & 0xFF) : 0),
            FPGA_END_BIT
        };

        if (UART2_Write(fpgaCmd, 11)) {
            uint32_t timeout = 1000000;
            while (UART2_WriteIsBusy() && timeout--);
            if (timeout == 0) {
                SendUART1Message("UART2: TX timeout");
            } else {
                SendUART1Message("FPGA: Settings (including dwell time) sent successfully (11 bytes)");
            }
        } else {
            SendUART1Message("UART2: Write failed for FPGA settings");
        }

        MCP4725_WriteDAC(currentSettings.highVoltage, currentSettings.hvOnOff);
        AutoSaveSettings();

clear_and_read:
        UART1_ReadAbort();
        memset(uart1RxBuffer, 0, RX_BUFFER_SIZE);
        UART1_Read(uart1RxBuffer, RX_BUFFER_SIZE);
    }

    // Process UART2 data
    ProcessUART2Rx();

    // Check for periodic spectrum data read (every 5 seconds)
    if (isAcquisitionRunning && (Timer1_GetTicks() - lastSpectrumReadTime >= SPECTRUM_READ_INTERVAL)) {
        if (spectrumDataReceived) {
            // Data was already received and printed in ProcessUART2Rx
            SendUART1Message("Periodic check: Spectrum data already processed");
        } else {
            // No data received yet, log a warning
            SendUART1Message("Periodic check: No spectrum data received in last 5 seconds");
        }
        lastSpectrumReadTime = Timer1_GetTicks();
    }
}

int main(void) {
    SYS_Initialize(NULL);
    delay_cycles(DELAY_1MS_CYCLES * 100);

    // Initialize Timer1 for timing
    Timer1_Init();
    SendUART1Message("Timer1: Initialized for 1 ms ticks");

    // Toggle GPIO (e.g., RB7) to confirm execution
    TRISBbits.TRISB7 = 0; // Set RB7 as output
    LATBbits.LATB7 = 1;  // Set RB7 high (connect LED if available)

    // Test UART1 directly
    UART1_Write((uint8_t*)"Test UART1\r\n", 11);
    delay_cycles(DELAY_1MS_CYCLES * 100);

    UART1_Initialize();
    UART_SERIAL_SETUP uart1Setup = {
        .baudRate = UART1_BAUDRATE,
        .dataWidth = UART_DATA_8_BIT,
        .parity = UART_PARITY_NONE,
        .stopBits = UART_STOP_1_BIT
    };
    if (!UART1_SerialSetup(&uart1Setup, PERIPHERAL_CLOCK_FREQ)) {
        LATBbits.LATB7 = 0; // Clear RB7 on error
        SendUART1Message("Error: UART1 SerialSetup failed");
        while (true) {
            SYS_Tasks();
        }
    }
    SendUART1Message("PIC32MX250F128D: System initialized with MCP2200 USB Interface");

    UART1_ReadCallbackRegister(UART1_RxCallback, 0);
    SendUART1Message("UART1 initialized for user interface via MCP2200");

    UART2_Initialize();
    UART_SERIAL_SETUP uart2Setup = {
        .baudRate = UART1_BAUDRATE,
        .dataWidth = UART_DATA_8_BIT,
        .parity = UART_PARITY_NONE,
        .stopBits = UART_STOP_1_BIT
    };
    if (!UART2_SerialSetup(&uart2Setup, PERIPHERAL_CLOCK_FREQ)) {
        SendUART1Message("Error: UART2 SerialSetup failed");
        while (true) {
            SYS_Tasks();
        }
    }
    SendUART1Message("UART2: Initialized for FPGA communication (TX and RX)");
    UART2_Read(uart2RxBuffer, TX_BUFFER_SIZE); // Start reading data

    SendUART1Message("I2C1: Initialized for MCP4725 DAC");
    I2C_TRANSFER_SETUP setup = { .clkSpeed = 100000 };
    I2C1_TransferSetup(&setup, 0);

    EEPROM_CS_HIGH();
    SPI_TRANSFER_SETUP spiSetup = {
        .clockFrequency = 2000000,
        .clockPolarity = SPI_CLOCK_POLARITY_IDLE_LOW,
        .clockPhase = SPI_CLOCK_PHASE_LEADING_EDGE,
        .dataBits = SPI_DATA_BITS_8
    };
    if (SPI1_TransferSetup(&spiSetup, 0)) {
        SendUART1Message("SPI1: Initialized for EEPROM (2MHz, Mode 1)");
    }

    if (MCP4725_WriteDAC(0, 0)) {
        SendUART1Message("MCP4725 DAC: Detected and initialized to 0V");
    }

    SendUART1Message("Testing EEPROM at address 0x1000...");
    uint8_t testData[4] = {0x55, 0x55, 0x55, 0x55};
    if (EEPROM_WriteBytes(0x1000, testData, 4)) {
        SendUART1Message("EEPROM Test Write: 0x55 0x55 0x55 0x55 at 0x1000");
    } else {
        SendUART1Message("EEPROM Test Write Failed");
    }
    uint8_t readData[4];
    if (EEPROM_ReadBytes(0x1000, readData, 4)) {
        char hex[50];
        snprintf(hex, sizeof(hex), "EEPROM Test Read: %02X %02X %02X %02X",
                 readData[0], readData[1], readData[2], readData[3]);
        SendUART1Message(hex);
        if (memcmp(testData, readData, 4) == 0) {
            SendUART1Message("EEPROM: Test passed - read/write working correctly");
        } else {
            SendUART1Message("EEPROM: Test failed - data mismatch");
        }
    } else {
        SendUART1Message("EEPROM Test Read Failed");
    }

    if (!initialize_mcp2200()) {
        SendUART1Message("Failed to initialize MCP2200, halting...");
        while (true) {
            SYS_Tasks();
        }
    }

    if (LoadSettingsFromEEPROM(&currentSettings)) {
        memcpy(&lastSavedSettings, &currentSettings, sizeof(Settings_t));
    } else {
        memset(&currentSettings, 0, sizeof(Settings_t));
        memset(&lastSavedSettings, 0, sizeof(Settings_t));
    }

    SendInitialPrompt();
    UART1_Read(uart1RxBuffer, RX_BUFFER_SIZE);
    SendUART1Message("UART1: Waiting for input over USB...");

    while (true) {
        SYS_Tasks();
        ProcessUserInput();
        
        if (!uart1RxComplete && UART1_ReadCountGet() > 0) {
            SendUART1Message("UART1: Data detected without callback, forcing process");
            uart1RxComplete = true;
            ProcessUserInput();
        }
    }

    return (EXIT_FAILURE);
}