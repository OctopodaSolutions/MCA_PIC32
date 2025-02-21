//============================================================================
// Configuration Bits
//============================================================================
#pragma config FNOSC = FRCPLL    // Internal Fast RC oscillator with PLL
#pragma config FSOSCEN = OFF     // Secondary Oscillator Disable
#pragma config POSCMOD = OFF     // Primary oscillator disabled
#pragma config OSCIOFNC = OFF    // CLKO Output Signal Disable
#pragma config FPBDIV = DIV_1    // Peripheral Bus Clock = System Clock
#pragma config FWDTEN = OFF      // Watchdog Timer Disable
#pragma config WDTPS = PS1       // Watchdog Timer Postscale
#pragma config FCKSM = CSDCMD    // Clock Switching & Fail Safe Clock Monitor Disable
#pragma config ICESEL = ICS_PGx1 // ICE/ICD Comm Channel Select
#pragma config PWP = OFF         // Program Flash Write Protect Disable
#pragma config BWP = OFF         // Boot Flash Write Protect Disable
#pragma config CP = OFF          // Code Protect Disable
#pragma config FPLLMUL = MUL_15  // PLL Multiplier (15x)
#pragma config FPLLIDIV = DIV_2  // PLL Input Divider (2x Divider)
#pragma config FPLLODIV = DIV_1  // PLL Output Divider (1x Divider)

//============================================================================
// Includes
//============================================================================
#include <xc.h>
#include <sys/attribs.h>  // Required for __ISR macro
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "device_info.h"
#include "status.h"

//============================================================================
// Defines
//============================================================================
#define SYSCLK      60000000L
#define PBCLK       60000000L
#define UART_BAUD   115200
#define I2C_FREQ    100000  // 100kHz I2C frequency
#define DAC_ADDR    0x0C    
#define CMD_SIZE    64      // Increased buffer size for longer commands
#define RESPONSE_SIZE 512

//============================================================================
// Global Variables
//============================================================================
char cmdBuffer[CMD_SIZE];
unsigned int cmdIndex = 0;
bool dacConnected = false;

// NEW FLAG: Control continuous FPGA data reading
bool continuousFPGARead = false;

// Chip Select for PGA112AIDGST via SPI 
#define PGA_CS          LATCbits.LATC2

//============================================================================
// Function Prototypes
//============================================================================
void SYSTEM_Init(void);
void GPIO_Init(void);
void UART1_Init(void);
void UART2_Init(void);
void I2C2_Init(void);
void I2C2_Start(void);
void I2C2_Stop(void);
void I2C2_Write(unsigned char data);
unsigned char I2C2_Read(unsigned char ack);
void setDAC_Voltage(unsigned int voltage);
unsigned int getDAC_Voltage(void);
void sendFPGA_Command(const char* cmd, unsigned int value);
void processCommand(const char* cmd);
bool checkDAC_Connection(void);
void delay_ms(unsigned int ms);
void performPOR(void);

// UART2 function prototypes
void UART2_Write(unsigned char data);
void UART2_WriteString(const char* str);
unsigned char UART2_DataReady(void);
char UART2_Read(void);

// SPI Functions for PGA112AIDGST
void SPI_Init(void);
unsigned char SPI_Transfer(unsigned char data);
void SPI_Write(unsigned char data);
unsigned char SPI_Read(void);
void PGA112_Write(unsigned char reg, unsigned char value);
unsigned char PGA112_Read(unsigned char reg);

//============================================================================
// Utility Functions
//============================================================================
void delay_ms(unsigned int ms) {
    unsigned int i;
    unsigned int count = ms * (SYSCLK / 2000000);
    for(i = 0; i < count; i++) {
        asm("nop");
    }
}

//============================================================================
// System Initialization
//============================================================================
void SYSTEM_Init(void) {
    // Unlock system for clock configuration
    SYSKEY = 0;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    // Configure OSCCON
    OSCCONbits.NOSC = 0x0001;     // FRC with PLL
    OSCCONbits.FRCDIV = 0;        // FRC divider = 1
    OSCCONbits.PBDIV = 0;         // Peripheral bus clock = SYSCLK
    
    while(OSCCONbits.OSWEN != 0); // Wait for switch
    while(OSCCONbits.SLOCK != 1); // Wait for PLL lock
    
    SYSKEY = 0x33333333;          // Lock system configuration
}

void GPIO_Init(void) {
    // Unlock PPS
    SYSKEY = 0x00000000;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    // Configure UART1 pins
    TRISAbits.TRISA4 = 1;    // U1RX as input
    TRISAbits.TRISA0 = 0;    // U1TX as output
    
    // Configure UART2 pins
    TRISBbits.TRISB8 = 1;    // U2RX as input
    TRISBbits.TRISB9 = 0;    // U2TX as output
    
    // Configure I2C2 pins for DAC
    TRISBbits.TRISB3 = 0;    // SCL2
    TRISBbits.TRISB2 = 0;    // SDA2
    
    // Map pins
    U1RXR = 0b0010;          // RA4 -> U1RX
    RPA0R = 0b0001;          // RA0 -> U1TX
    
    U2RXR = 0b0100;          // RB8 -> U2RX
    RPB9R = 0b0010;          // RB9 -> U2TX
    
   // SPI Pins not included here need to add after the pin confirmation
   
    // Lock PPS
    SYSKEY = 0x33333333;
}

//============================================================================
// UART1 Initialization & Functions
//============================================================================
void UART1_Init(void) {
    // Disable UART1 before configuration
    U1MODEbits.ON = 0;
    
    // Clear UART1 configuration
    U1MODE = 0;
    U1STA = 0;
    
    // Calculate and set baud rate
    U1BRG = ((PBCLK / (16 * UART_BAUD)) - 1);
    
    // Configure UART1 mode
    U1MODEbits.BRGH = 0;      // Standard Speed mode
    U1MODEbits.PDSEL = 0;     // 8-bit data, no parity
    U1MODEbits.STSEL = 0;     // 1 stop bit
    
    // Enable TX and RX
    U1STAbits.UTXEN = 1;
    U1STAbits.URXEN = 1;
    
    // Enable UART1
    U1MODEbits.ON = 1;
    
    delay_ms(1);
}

void UART1_Write(unsigned char data) {
    while(U1STAbits.UTXBF);   // Wait until transmit buffer is not full
    U1TXREG = data;
    while(!U1STAbits.TRMT);   // Wait until transmit shift register is empty
}

void UART1_WriteString(const char* str) {
    while(*str != '\0') {
        UART1_Write(*str++);
    }
}

unsigned char UART1_DataReady(void) {
    return U1STAbits.URXDA;
}

char UART1_Read(void) {
    if(U1STAbits.OERR) {          // Clear overrun error
        U1STAbits.OERR = 0;
    }
    while(!U1STAbits.URXDA);      // Wait for data to be available
    return U1RXREG;               // Return received data
}

//============================================================================
// UART2 Initialization & Functions (FPGA)
//============================================================================
void UART2_Init(void) {
    // Disable UART2 before configuration
    U2MODEbits.ON = 0;
    
    // Clear UART2 configuration
    U2MODE = 0;
    U2STA = 0;
    
    // Calculate and set baud rate
    U2BRG = ((PBCLK / (16 * UART_BAUD)) - 1);
    
    // Configure UART2 mode
    U2MODEbits.BRGH = 0;      // Standard Speed mode
    U2MODEbits.PDSEL = 0;     // 8-bit data, no parity
    U2MODEbits.STSEL = 0;     // 1 stop bit
    
    // Enable TX and RX
    U2STAbits.UTXEN = 1;
    U2STAbits.URXEN = 1;
    
    // Enable UART2
    U2MODEbits.ON = 1;
    
    delay_ms(1);
}

void UART2_Write(unsigned char data) {
    while(U2STAbits.UTXBF);   // Wait until transmit buffer is not full
    U2TXREG = data;
    while(!U2STAbits.TRMT);   // Wait until transmit shift register is empty
}

void UART2_WriteString(const char* str) {
    while(*str != '\0') {
        UART2_Write(*str++);
    }
}

unsigned char UART2_DataReady(void) {
    return U2STAbits.URXDA;
}

char UART2_Read(void) {
    if(U2STAbits.OERR) {          // Clear overrun error
        U2STAbits.OERR = 0;
    }
    while(!U2STAbits.URXDA);      // Wait for data to be available
    return U2RXREG;               // Return received data
}

//============================================================================
// I2C2 Initialization & Functions (for DAC)
//============================================================================
void I2C2_Init(void) {
    I2C2CON = 0;
    I2C2BRG = (PBCLK / (2 * I2C_FREQ)) - 2;
    I2C2CONbits.ON = 1;
}

void I2C2_Start(void) {
    I2C2CONbits.SEN = 1;
    while(I2C2CONbits.SEN);
}

void I2C2_Stop(void) {
    I2C2CONbits.PEN = 1;
    while(I2C2CONbits.PEN);
}

void I2C2_Write(unsigned char data) {
    I2C2TRN = data;
    while(I2C2STATbits.TRSTAT);
}

unsigned char I2C2_Read(unsigned char ack) {
    I2C2CONbits.RCEN = 1;
    while(!I2C2STATbits.RBF);
    unsigned char data = I2C2RCV;
    I2C2CONbits.ACKDT = !ack;
    I2C2CONbits.ACKEN = 1;
    while(I2C2CONbits.ACKEN);
    return data;
}

//============================================================================
// DAC-Related Functions
//============================================================================
void setDAC_Voltage(unsigned int voltage) {
    if(voltage > 1000) voltage = 1000;  // Clamp to max value
    if(voltage < 0) voltage = 0;        // Clamp to min value
    
    unsigned int dac_value = (voltage * 65535UL) / 1000; // Convert voltage to DAC value
    
    // Try up to 3 times to set the voltage
    for(int attempts = 0; attempts < 3; attempts++) {
        I2C2_Start();
        I2C2_Write(DAC_ADDR << 1);    // Write address
        I2C2_Write((dac_value >> 8) & 0xFF);  // High byte
        I2C2_Write(dac_value & 0xFF);         // Low byte
        I2C2_Stop();
        
        // Verify the setting
        unsigned int read_voltage = getDAC_Voltage();
        if(abs(read_voltage - voltage) <= 2) { // Allow small tolerance
            break;
        }
        delay_ms(10);  // Wait before retry
    }
}

unsigned int getDAC_Voltage(void) {
    unsigned int dac_value;
    I2C2_Start();
    I2C2_Write((DAC_ADDR << 1) | 1);  // Read address
    dac_value = I2C2_Read(1) << 8;    // High byte
    dac_value |= I2C2_Read(0);        // Low byte
    I2C2_Stop();
    return (dac_value * 1000) / 65535; // Convert DAC value to voltage
}

bool checkDAC_Connection(void) {
    I2C2_Start();
    I2C2_Write(DAC_ADDR << 1);    // Write address
    bool connected = !I2C2STATbits.ACKSTAT;  // Check if ACK received
    I2C2_Stop();
    return connected;
}

//============================================================================
// FPGA-Related Functions
//============================================================================
void sendFPGA_Command(const char* cmd, unsigned int value) {
    char fpga_cmd[32];
    sprintf(fpga_cmd, "%s,%u\r\n", cmd, value);
    UART2_WriteString(fpga_cmd);
}

void sendFPGA_SimpleCommand(const char* cmd) {
    UART2_WriteString(cmd);
    UART2_WriteString("\r\n");
}
// function to read a single value from FPGA 
// unsigned int readFPGA_Value(const char* cmd) {
//     // Implementation if needed...
//     return 0;
// }

void readFPGAData(void) {
    // Check if any data is available in UART2 buffer and forward it to UART1
    while(UART2_DataReady()) {
        char fpga_data = UART2_Read();
        UART1_Write(fpga_data);
    }
}

//============================================================================
// SPI Functions for PGA112AIDGST
//============================================================================
void SPI_Init(void) {
    // Configure SPI1 for Master mode, mode 0 (CKP=0, CKE=1)
    SPI1CON = 0;              // Reset configuration
    SPI1STATCLR = 0x40;       // Clear overflow flag
    SPI1CONbits.MSTEN = 1;    // Enable Master mode
    SPI1CONbits.CKP = 0;      // Clock idle low
    SPI1CONbits.CKE = 1;      // Data is shifted out on the rising edge
    SPI1CONbits.SMP = 0;      // Input sampled at middle of data output time
    SPI1BRG = 1;              // Set baud rate (adjust as needed)
    SPI1CONbits.ON = 1;       // Enable SPI module
}

unsigned char SPI_Transfer(unsigned char data) {
    SPI1BUF = data;
    while(!SPI1STATbits.SPIRBF);  // Wait until transfer complete
    return SPI1BUF;
}

void SPI_Write(unsigned char data) {
    SPI_Transfer(data);
}

unsigned char SPI_Read(void) {
    return SPI_Transfer(0xFF);  // Send dummy byte and return received data
}

void PGA112_Write(unsigned char reg, unsigned char value) {
    // Pull CS low to start communication
    PGA_CS = 0;
    SPI_Write(reg);
    SPI_Write(value);
    PGA_CS = 1; // Pull CS high to end communication
}

unsigned char PGA112_Read(unsigned char reg) {
    unsigned char value;
    PGA_CS = 0;
    SPI_Write(reg);  // Send register address (protocol-dependent)
    value = SPI_Read();
    PGA_CS = 1;
    return value;
}

//============================================================================
// Command Processing
//============================================================================
void processCommand(const char* cmd) {
    char command[16];
    char param1[16];
    char param2[16];
    unsigned int value;
    
    // Initialize parameters to empty strings
    command[0] = param1[0] = param2[0] = '\0';
    
    // Parse command and parameters
    sscanf(cmd, "%[^,],%[^,],%s", command, param1, param2);
    
    // Convert command to uppercase
    for(int i = 0; command[i]; i++) {
        if(command[i] >= 'a' && command[i] <= 'z') {
            command[i] = command[i] - 32;
        }
    }
    
    // System Control Commands
    if(strcmp(command, "START") == 0) {
        status.systemState = 1;
        
        // Enable continuous FPGA data reading
        sendFPGA_SimpleCommand("start");
        readFPGAData();
        continuousFPGARead = true;
        
        UART1_WriteString("System started\r\n");
        updateAndPrintStatus(UART1_WriteString, &status, "systemState", 1);
    }
    else if(strcmp(command, "STOP") == 0) {
        status.systemState = 0;
        
        // Disable continuous FPGA data reading
        continuousFPGARead = false;
        sendFPGA_SimpleCommand("stop");
        
        UART1_WriteString("System stopped\r\n");
        updateAndPrintStatus(UART1_WriteString, &status, "systemState", 0);
    }
    else if(strcmp(command, "RESET") == 0) {
        setDAC_Voltage(400);
        sendFPGA_Command("RST", 0);
        UART1_WriteString("System reset complete\r\n");
        printStatusUpdate(UART1_WriteString, &status);
    }
    else if(strcmp(command, "STATUS") == 0) {
        printStatusUpdate(UART1_WriteString, &status);
    }
    // DAC Commands
    else if(strcmp(command, "SETV") == 0) {
        value = atoi(param1);
        if(value > 0 && value <= 1000) {
            setDAC_Voltage(value);
            updateAndPrintStatus(UART1_WriteString, &status, "voltage", value);
        } else {
            UART1_WriteString("Error: Voltage must be between 1 and 1000V\r\n");
        }
    }
    else if(strcmp(command, "GETV?") == 0) {
        value = getDAC_Voltage();
        updateAndPrintStatus(UART1_WriteString, &status, "voltage", value);
    }
    // FPGA - ADC Gain Commands
    else if(strcmp(command, "SETCG") == 0 || strcmp(command, "CG") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 63) {
            sendFPGA_Command("CG", value);
            updateAndPrintStatus(UART1_WriteString, &status, "courseGain", value);
        } else {
            UART1_WriteString("Error: Course gain must be between 0 and 63\r\n");
        }
    }
    else if(strcmp(command, "SETFG") == 0 || strcmp(command, "FG") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 255) {
            sendFPGA_Command("FG", value);
            updateAndPrintStatus(UART1_WriteString, &status, "fineGain", value);
        } else {
            UART1_WriteString("Error: Fine gain must be between 0 and 255\r\n");
        }
    }
    else if(strcmp(command, "SETDG") == 0 || strcmp(command, "DG") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 255) {
            sendFPGA_Command("DG", value);
            updateAndPrintStatus(UART1_WriteString, &status, "digitalGain", value);
        } else {
            UART1_WriteString("Error: Digital gain must be between 0 and 255\r\n");
        }
    }
    // FPGA - ADC Polarity Command
    else if(strcmp(command, "SETPOL") == 0 || strcmp(command, "POL") == 0) {
        value = atoi(param2);
        if(value == 0 || value == 1) {
            sendFPGA_Command("POL", value);
            updateAndPrintStatus(UART1_WriteString, &status, "polarity", value);
        } else {
            UART1_WriteString("Error: Polarity must be 0 (negative) or 1 (positive)\r\n");
        }
    }
    // FPGA - Timing Commands
    else if(strcmp(command, "SETTT") == 0 || strcmp(command, "TT") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 1000) {
            sendFPGA_Command("TT", value);
            updateAndPrintStatus(UART1_WriteString, &status, "thresholdTime", value);
        } else {
            UART1_WriteString("Error: Threshold time must be between 0 and 1000 seconds\r\n");
        }
    }
    else if(strcmp(command, "SETRT") == 0 || strcmp(command, "RT") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 10000) {
            sendFPGA_Command("RT", value);
            updateAndPrintStatus(UART1_WriteString, &status, "riseTime", value);
        } else {
            UART1_WriteString("Error: Rise time must be between 0 and 10000 ns\r\n");
        }
    }
    else if(strcmp(command, "SETFT") == 0 || strcmp(command, "FT") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 10000) {
            sendFPGA_Command("FT", value);
            updateAndPrintStatus(UART1_WriteString, &status, "flatTime", value);
        } else {
            UART1_WriteString("Error: Flat time must be between 0 and 10000 ns\r\n");
        }
    }
    // FPGA - Signal Processing Commands
    else if(strcmp(command, "SETPZ") == 0 || strcmp(command, "PZ") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 1000) {
            sendFPGA_Command("PZ", value);
            updateAndPrintStatus(UART1_WriteString, &status, "poleZero", value);
        } else {
            UART1_WriteString("Error: Pole-zero must be between 0 and 1000\r\n");
        }
    }
    else if(strcmp(command, "SETDB") == 0 || strcmp(command, "DB") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 1000) {
            sendFPGA_Command("DB", value);
            updateAndPrintStatus(UART1_WriteString, &status, "digitalBL", value);
        } else {
            UART1_WriteString("Error: Digital baseline must be between 0 and 1000\r\n");
        }
    }
    else if(strcmp(command, "SETPR") == 0 || strcmp(command, "PR") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 100) {
            sendFPGA_Command("PR", value);
            updateAndPrintStatus(UART1_WriteString, &status, "pileupReject", value);
        } else {
            UART1_WriteString("Error: Pile-up reject must be between 0 and 100\r\n");
        }
    }
    else if(strcmp(command, "SETPT") == 0 || strcmp(command, "PT") == 0) {
        value = atoi(param2);
        if(value >= 0 && value <= 86400) {
            sendFPGA_Command("PT", value);
            updateAndPrintStatus(UART1_WriteString, &status, "presetTime", value);
        } else {
            UART1_WriteString("Error: Preset time must be between 0 and 86400 seconds\r\n");
        }
    }
    else if(strcmp(command, "INFO") == 0) {
        getDeviceInfo(UART1_WriteString);
    }
    else if(strcmp(command, "HELP") == 0) {
        UART1_WriteString("\r\nAvailable Commands:\r\n");
        // (Your help text here)
    } else if(strcmp(command, "SETC") == 0 || strcmp(command, "COUNT") == 0) {
        value = atoi(param2);
        // Optionally add bounds checking if needed
        sendFPGA_Command("COUNT", value);
        updateAndPrintStatus(UART1_WriteString, &status, "count", value);
    }
    // SETSCH,SCH,value set start channel
    else if(strcmp(command, "SETSCH") == 0 || strcmp(command, "SCH") == 0) {
        value = atoi(param2);
        sendFPGA_Command("SCH", value);
        updateAndPrintStatus(UART1_WriteString, &status, "startChannel", value);
    }
    // SETECH,ECH,Value set end channel
    else if(strcmp(command, "SETECH") == 0 || strcmp(command, "ECH") == 0) {
        value = atoi(param2);
        sendFPGA_Command("ECH", value);
        updateAndPrintStatus(UART1_WriteString, &status, "endChannel", value);
    }
    // SETCH,CH,Value  set number of channels (e.g., 1024, 2048, 4096, etc.)
    else if(strcmp(command, "SETCH") == 0 || strcmp(command, "CH") == 0) {
        value = atoi(param2);
        // You might want to restrict this to allowed values (1024, 2048, 4096, etc.)
        sendFPGA_Command("CH", value);
        updateAndPrintStatus(UART1_WriteString, &status, "channels", value);
    }
    // SETLLD,LLD,Value    set LLD value
    else if(strcmp(command, "SETLLD") == 0 || strcmp(command, "LLD") == 0) {
        value = atoi(param2);
        sendFPGA_Command("LLD", value);
        updateAndPrintStatus(UART1_WriteString, &status, "LLD", value);
    } else if(strcmp(command, "SETDT") == 0 || strcmp(command, "DT") == 0) {
        value = atoi(param2);
        // Optionally, you can add bounds checking for dwell time here.
        sendFPGA_Command("DT", value);
        updateAndPrintStatus(UART1_WriteString, &status, "dwellTime", value);
    }
    // SETULD,ULD,Value     set ULD value
    else if(strcmp(command, "SETULD") == 0 || strcmp(command, "ULD") == 0) {
        value = atoi(param2);
        sendFPGA_Command("ULD", value);
        updateAndPrintStatus(UART1_WriteString, &status, "ULD", value);
    }
    // SETHVON,ON,value   1 is HV on, 0 is HV off
    else if(strcmp(command, "SETHVON") == 0 || strcmp(command, "ON") == 0) {
        value = atoi(param2);
        if(value == 0 || value == 1) {
            sendFPGA_Command("ON", value);
            updateAndPrintStatus(UART1_WriteString, &status, "hvStatus", value);
        } else {
            UART1_WriteString("Error: HV status must be 0 (off) or 1 (on)\r\n");
        }
    }
}

void processModeCommand(const char *cmd) {
    char cmdCopy[CMD_SIZE];
    // Copy the input command into a modifiable buffer
    strncpy(cmdCopy, cmd, CMD_SIZE);
    cmdCopy[CMD_SIZE - 1] = '\0';  // Ensure null termination

    // Tokenize the command string using commas as delimiters.
    // Reserve up to 21 tokens (mode plus maximum parameters).
    char *tokens[21];
    int tokenCount = 0;
    char *token = strtok(cmdCopy, ",");
    while (token != NULL && tokenCount < 21) {
        tokens[tokenCount++] = token;
        token = strtok(NULL, ",");
    }

    if(tokenCount < 1) {
        UART1_WriteString("Error: No mode specified.\r\n");
        return;
    }

    // Check for mode selection (PHA or MCS)
    if(strcmp(tokens[0], "PHA") == 0) {
        // Expected 18 parameters after "PHA" (total tokens = 19)
        if(tokenCount != 19) {
            UART1_WriteString("Error: Incorrect number of parameters for PHA mode.\r\n");
            return;
        }
        int presetTime    = atoi(tokens[1]);
        int counts        = atoi(tokens[2]);
        int startChannel  = atoi(tokens[3]);
        int endChannel    = atoi(tokens[4]);
        int noOfChannels  = atoi(tokens[5]);
        int LLD           = atoi(tokens[6]);
        int ULD           = atoi(tokens[7]);
        int coarseGain    = atoi(tokens[8]);
        int fineGain      = atoi(tokens[9]);
        int inputPolarity = atoi(tokens[10]);
        int threshold     = atoi(tokens[11]);
        int riseTime      = atoi(tokens[12]);
        int flatTime      = atoi(tokens[13]);
        int poleZeros     = atoi(tokens[14]);
        int digitalBLR    = atoi(tokens[15]);
        int pileupReject  = atoi(tokens[16]);
        int HV            = atoi(tokens[17]);
        int HV_on_off     = atoi(tokens[18]);

        // Update system status with parsed values
        updateAndPrintStatus(UART1_WriteString, &status, "presetTime", presetTime);
        updateAndPrintStatus(UART1_WriteString, &status, "counts", counts);
        updateAndPrintStatus(UART1_WriteString, &status, "startChannel", startChannel);
        updateAndPrintStatus(UART1_WriteString, &status, "endChannel", endChannel);
        updateAndPrintStatus(UART1_WriteString, &status, "noOfChannels", noOfChannels);
        updateAndPrintStatus(UART1_WriteString, &status, "LLD", LLD);
        updateAndPrintStatus(UART1_WriteString, &status, "ULD", ULD);
        updateAndPrintStatus(UART1_WriteString, &status, "coarseGain", coarseGain);
        updateAndPrintStatus(UART1_WriteString, &status, "fineGain", fineGain);
        updateAndPrintStatus(UART1_WriteString, &status, "inputPolarity", inputPolarity);
        updateAndPrintStatus(UART1_WriteString, &status, "threshold", threshold);
        updateAndPrintStatus(UART1_WriteString, &status, "riseTime", riseTime);
        updateAndPrintStatus(UART1_WriteString, &status, "flatTime", flatTime);
        updateAndPrintStatus(UART1_WriteString, &status, "poleZeros", poleZeros);
        updateAndPrintStatus(UART1_WriteString, &status, "digitalBLR", digitalBLR);
        updateAndPrintStatus(UART1_WriteString, &status, "pileupReject", pileupReject);
        updateAndPrintStatus(UART1_WriteString, &status, "HV", HV);
        updateAndPrintStatus(UART1_WriteString, &status, "HV_on_off", HV_on_off);

        UART1_WriteString("PHA mode configuration accepted.\r\n");
    }
    else if(strcmp(tokens[0], "MCS") == 0) {
        // Expected 19 parameters after "MCS" (total tokens = 20)
        if(tokenCount != 20) {
            UART1_WriteString("Error: Incorrect number of parameters for MCS mode.\r\n");
            return;
        }
        int presetTime    = atoi(tokens[1]);
        int counts        = atoi(tokens[2]);
        int startChannel  = atoi(tokens[3]);
        int endChannel    = atoi(tokens[4]);
        int noOfChannels  = atoi(tokens[5]);
        int LLD           = atoi(tokens[6]);
        int ULD           = atoi(tokens[7]);
        int coarseGain    = atoi(tokens[8]);
        int fineGain      = atoi(tokens[9]);
        int inputPolarity = atoi(tokens[10]);
        int threshold     = atoi(tokens[11]);
        int riseTime      = atoi(tokens[12]);
        int flatTime      = atoi(tokens[13]);
        int poleZeros     = atoi(tokens[14]);
        int digitalBLR    = atoi(tokens[15]);
        int pileupReject  = atoi(tokens[16]);
        int HV            = atoi(tokens[17]);
        int HV_on_off     = atoi(tokens[18]);
        int dwellTime     = atoi(tokens[19]);

        // Update system status with parsed values
        updateAndPrintStatus(UART1_WriteString, &status, "presetTime", presetTime);
        updateAndPrintStatus(UART1_WriteString, &status, "counts", counts);
        updateAndPrintStatus(UART1_WriteString, &status, "startChannel", startChannel);
        updateAndPrintStatus(UART1_WriteString, &status, "endChannel", endChannel);
        updateAndPrintStatus(UART1_WriteString, &status, "noOfChannels", noOfChannels);
        updateAndPrintStatus(UART1_WriteString, &status, "LLD", LLD);
        updateAndPrintStatus(UART1_WriteString, &status, "ULD", ULD);
        updateAndPrintStatus(UART1_WriteString, &status, "coarseGain", coarseGain);
        updateAndPrintStatus(UART1_WriteString, &status, "fineGain", fineGain);
        updateAndPrintStatus(UART1_WriteString, &status, "inputPolarity", inputPolarity);
        updateAndPrintStatus(UART1_WriteString, &status, "threshold", threshold);
        updateAndPrintStatus(UART1_WriteString, &status, "riseTime", riseTime);
        updateAndPrintStatus(UART1_WriteString, &status, "flatTime", flatTime);
        updateAndPrintStatus(UART1_WriteString, &status, "poleZeros", poleZeros);
        updateAndPrintStatus(UART1_WriteString, &status, "digitalBLR", digitalBLR);
        updateAndPrintStatus(UART1_WriteString, &status, "pileupReject", pileupReject);
        updateAndPrintStatus(UART1_WriteString, &status, "HV", HV);
        updateAndPrintStatus(UART1_WriteString, &status, "HV_on_off", HV_on_off);
        updateAndPrintStatus(UART1_WriteString, &status, "dwellTime", dwellTime);

        // Optionally, send corresponding FPGA/DAC commands here.
        UART1_WriteString("MCS mode configuration accepted.\r\n");
    }
    else {
        UART1_WriteString("Error: Invalid mode specified. Use 'PHA' or 'MCS'.\r\n");
    }
}

//============================================================================
// Power-On Routine
//============================================================================
void performPOR(void) {
    // Initialize peripherals
    GPIO_Init();
    UART1_Init();
    UART2_Init();
//    I2C2_Init();
    // SPI_Init();
    
    // Load saved status values first
    initStatus();
    
    // Debug output
    char debugStr[64];
    sprintf(debugStr, "Loaded voltage: %d\r\n", status.voltage);
    UART1_WriteString(debugStr);
    
    // Check connections (just for info display)
    dacConnected = checkDAC_Connection();
    
    UART1_WriteString("\r\nInitializing System:\r\n");
    
    // Always restore DAC voltage regardless of connection status
    setDAC_Voltage(status.voltage);
    sprintf(debugStr, "Restored Voltage: %d V\r\n", status.voltage);
    UART1_WriteString(debugStr);
    
    // Always restore all FPGA settings regardless of connection status
    sendFPGA_Command("CG", status.courseGain);    
    sendFPGA_Command("FG", status.fineGain);      
    sendFPGA_Command("DG", status.digitalGain);   
    sendFPGA_Command("POL", status.polarity);     
    sendFPGA_Command("TT", status.thresholdTime); 
    sendFPGA_Command("RT", status.riseTime);      
    sendFPGA_Command("FT", status.flatTime);      
    sendFPGA_Command("PZ", status.poleZero);      
    sendFPGA_Command("DB", status.digitalBL);     
    sendFPGA_Command("PR", status.pileupReject);  
    sendFPGA_Command("PT", status.presetTime);    
    
    // Just report connection status for information
    UART1_WriteString("DAC: ");
    UART1_WriteString(dacConnected ? "Connected\r\n" : "Not Connected\r\n");
    
    UART1_WriteString("Power-On Reset complete\r\n");
    printStatusUpdate(UART1_WriteString, &status);
}

//============================================================================
// Main
//============================================================================
int main(void) {
    SYSTEM_Init();
    GPIO_Init();
    UART1_Init();
    UART2_Init();
    // I2C2_Init();  
    // SPI_Init();
    
    // Restore system status
    initStatus();
    
    // Initial message
    UART1_WriteString("\r\nPIC32 Control System Ready\r\n");
    
    while(1) {
        // Check for incoming UART1 data character-by-character
        if(UART1_DataReady()) {
            char data = U1RXREG;
            
            // Echo received character back
            UART1_Write(data);
            
            // If newline or carriage return, process the command buffer
            if(data == '\r' || data == '\n') {
                if(cmdIndex > 0) {
                    cmdBuffer[cmdIndex] = '\0';
                    UART1_WriteString("\r\n");
                    
                    // Check if the command starts with "PHA" or "MCS" for full configuration
                    if(strncmp(cmdBuffer, "PHA", 3) == 0 || strncmp(cmdBuffer, "MCS", 3) == 0) {
                        processModeCommand(cmdBuffer);
                    } else {
                        processCommand(cmdBuffer);
                    }
                    
                    cmdIndex = 0;  // Reset the command buffer index
                }
            }
            else if(data == '\b' || data == 0x7F) {  // Handle backspace or delete
                if(cmdIndex > 0) {
                    cmdIndex--;
                    UART1_WriteString("\b \b");
                }
            }
            else if(cmdIndex < CMD_SIZE - 1) {
                cmdBuffer[cmdIndex++] = data;
            }
        }
        
        // Non-blocking FPGA data read if enabled
        if(continuousFPGARead == true) {
            readFPGAData();
        }
        
    }
    
    return 0;
}