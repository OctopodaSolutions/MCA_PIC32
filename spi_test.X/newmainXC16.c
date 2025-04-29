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
#pragma config FPLLMUL = MUL_15  // PLL Multiplier (15x)
#pragma config FPLLIDIV = DIV_2  // PLL Input Divider (2x Divider)
#pragma config FPLLODIV = DIV_1  // PLL Output Divider (1x Divider)

//============================================================================
// Includes
//============================================================================
#include <xc.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

//============================================================================
// Defines
//============================================================================
#define SYSCLK      60000000L
#define PBCLK       60000000L
#define UART_BAUD   115200
#define I2C_FREQ    100000  // 100kHz I2C frequency
#define DAC_ADDR    0x4C    // DAC8571 I2C address (A0 = 0, adjust if A0 = 1: 0x4D)
#define CMD_SIZE    64

// Modified: Changed check interval from 1 second to 2 seconds
#define CHECK_INTERVAL 2000UL  // 2 seconds

// Number of consecutive readings required to confirm state change
#define CONSECUTIVE_THRESHOLD 2  // Reduced from 3 to make detection faster

unsigned long _min_heap_size __attribute__((section(".heap"))) = 512;

//============================================================================
// Global Variables
//============================================================================
char cmdBuffer[CMD_SIZE];
unsigned int cmdIndex = 0;
bool dacConnected = false;
volatile unsigned long milliseconds = 0;

//============================================================================
// Function Prototypes
//============================================================================
void SYSTEM_Init(void);
void GPIO_Init(void);
void UART1_Init(void);
void I2C2_Init(void);
void Timer1_Init(void);
void I2C2_Start(void);
void I2C2_Restart(void);
void I2C2_Stop(void);
bool I2C2_Write(unsigned char data);
unsigned char I2C2_Read(unsigned char ack);
void setDAC_Voltage(unsigned int voltage);
unsigned int getDAC_Voltage(void);
bool checkDAC_Connection(void);
void UART1_Write(unsigned char data);
void UART1_WriteString(const char* str);
unsigned char UART1_DataReady(void);
char UART1_Read(void);
void processCommand(const char* cmd);
void __attribute__((interrupt(IPL2AUTO), vector(_TIMER_1_VECTOR))) Timer1_Handler(void);
void delay_ms(unsigned int ms);   // Added delay function prototype

//============================================================================
// System Initialization
//============================================================================
void SYSTEM_Init(void) {
    SYSKEY = 0;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    OSCCONbits.NOSC = 0x0001;     // FRC with PLL
    OSCCONbits.FRCDIV = 0;        // FRC divider = 1
    OSCCONbits.PBDIV = 0;         // Peripheral bus clock = SYSCLK
    
    while(OSCCONbits.OSWEN != 0); // Wait for switch
    while(OSCCONbits.SLOCK != 1); // Wait for PLL lock
    
    SYSKEY = 0x33333333;          // Lock system configuration
}

void GPIO_Init(void) {
    SYSKEY = 0x00000000;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    // UART1 pins
    TRISAbits.TRISA4 = 1;    // U1RX as input
    TRISAbits.TRISA0 = 0;    // U1TX as output
    
    // I2C2 pins (RB2=SDA2, RB3=SCL2)
    TRISBbits.TRISB2 = 1;    // SDA2
    TRISBbits.TRISB3 = 1;    // SCL2
    
    // PPS Mapping
    U1RXR = 0b0010;          // RA4 -> U1RX
    RPA0R = 0b0001;          // RA0 -> U1TX
    
    SYSKEY = 0x33333333;     // Lock PPS
}

//============================================================================
// Timer1 Functions
//============================================================================
void Timer1_Init(void) {
    T1CON = 0;                // Clear Timer1 control register
    TMR1 = 0;                 // Clear Timer1 counter
    
    // Modify the timer period to be less frequent
    PR1 = 30000 - 1;          // 0.5ms at 60MHz (reduced frequency)
    
    T1CONbits.TCKPS = 1;      // 1:8 prescaler (reduce timer frequency)
    T1CONbits.TCS = 0;        // Use PBCLK as clock source
    
    // Lower interrupt priority
    IPC1bits.T1IP = 1;        // Interrupt priority 1 (was 2)
    IPC1bits.T1IS = 0;        // Sub-priority 0
    
    // Clear the interrupt flag before enabling interrupt
    IFS0bits.T1IF = 0;        // Clear Timer1 interrupt flag
    IEC0bits.T1IE = 1;        // Enable Timer1 interrupt
    
    // Enable the timer last
    T1CONbits.ON = 1;         // Enable Timer1
}

void __attribute__((interrupt(IPL1AUTO), vector(_TIMER_1_VECTOR))) Timer1_Handler(void) {
    // Keep the interrupt handler minimal
    milliseconds++;
    IFS0bits.T1IF = 0;        // Clear interrupt flag
}

// Added reliable delay function based on millisecond timer
void delay_ms(unsigned int ms) {
    unsigned long startTime = milliseconds;
    while((milliseconds - startTime) < ms) {
        // Wait until the specified time has elapsed
        // This is a blocking delay function
    }
}

//============================================================================
// UART1 Functions
//============================================================================
void UART1_Init(void) {
    U1MODEbits.ON = 0;
    U1MODE = 0;
    U1STA = 0;
    
    U1BRG = (PBCLK / (16 * UART_BAUD)) - 1;
    
    U1MODEbits.BRGH = 0;      // Standard Speed mode
    U1MODEbits.PDSEL = 0;     // 8-bit data, no parity
    U1MODEbits.STSEL = 0;     // 1 stop bit
    
    U1STAbits.UTXEN = 1;
    U1STAbits.URXEN = 1;
    
    U1MODEbits.ON = 1;
}

void UART1_Write(unsigned char data) {
    while(U1STAbits.UTXBF);
    U1TXREG = data;
    while(!U1STAbits.TRMT);
}

void UART1_WriteString(const char* str) {
    while(*str != '\0') {
        UART1_Write(*str++);
        // Add small delay between characters to prevent transmission issues
        int i;
        for(i = 0; i < 1000; i++) {
            // Small delay
        }
    }
}

unsigned char UART1_DataReady(void) {
    return U1STAbits.URXDA;
}

char UART1_Read(void) {
    if(U1STAbits.OERR) {
        U1STAbits.OERR = 0;
    }
    while(!U1STAbits.URXDA);
    return U1RXREG;
}

//============================================================================
// I2C2 Functions (for DAC8571)
//============================================================================
void I2C2_Init(void) {
    I2C2CON = 0;
    I2C2BRG = (PBCLK / (2 * I2C_FREQ)) - 2;
    I2C2CONbits.DISSLW = 1;  // Disable slew rate control for 100kHz
    I2C2CONbits.ON = 1;
    
    // Make sure I2C bus is in idle state
    int timeout = 1000;
    while(I2C2STATbits.P != 1 && timeout > 0) {
        timeout--;
    }
    
    // If bus is still not idle, try to clear it
    if(timeout == 0) {
        // Toggle I2C to try to clear the bus
        I2C2CONbits.ON = 0;
        int i;
        for(i = 0; i < 10000; i++); // Small delay
        I2C2CONbits.ON = 1;
    }
}

void I2C2_Start(void) {
    I2C2CONbits.SEN = 1;
    while(I2C2CONbits.SEN);
}

void I2C2_Restart(void) {
    I2C2CONbits.RSEN = 1;
    while(I2C2CONbits.RSEN);
}

void I2C2_Stop(void) {
    I2C2CONbits.PEN = 1;
    while(I2C2CONbits.PEN);
}

bool I2C2_Write(unsigned char data) {
    I2C2TRN = data;
    while(I2C2STATbits.TRSTAT);
    return !I2C2STATbits.ACKSTAT;
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
// DAC Functions (DAC8571)
//============================================================================
void setDAC_Voltage(unsigned int voltage) {
    if(voltage > 1000) voltage = 1000;
    if(voltage < 0) voltage = 0;

    unsigned int dac_value = (voltage * 65535UL) / 1000;

    I2C2_Start();
    if(I2C2_Write(DAC_ADDR << 1)) {
        I2C2_Write(0x10);
        I2C2_Write((dac_value >> 8) & 0xFF);
        I2C2_Write(dac_value & 0xFF);
        char msg[64];
        sprintf(msg, "Set DAC to %u mV\r\n", voltage);
        UART1_WriteString(msg);
    } else {
        UART1_WriteString("DAC Write Failed\r\n");
    }
    I2C2_Stop();
}

unsigned int getDAC_Voltage(void) {
    unsigned int dac_value = 0;
    
    I2C2_Start();
    if(I2C2_Write(DAC_ADDR << 1)) {
        I2C2_Write(0x90);
        I2C2_Restart();
        if(I2C2_Write((DAC_ADDR << 1) | 1)) {
            dac_value = I2C2_Read(1) << 8;
            dac_value |= I2C2_Read(0);
            unsigned int voltage = (dac_value * 1000) / 65535;
            char msg[64];
            sprintf(msg, "Read DAC: %u mV\r\n", voltage);
            UART1_WriteString(msg);
            I2C2_Stop();
            return voltage;
        }
    }
    I2C2_Stop();
    UART1_WriteString("DAC Read Failed\r\n");
    return 0;
}

bool checkDAC_Connection(void) {
    // Immediately send debug info
    UART1_WriteString("Starting DAC Check...\r\n");
    
    bool connected = false;
    bool validDevice = false;
    
    // First, try to clear the I2C bus in case it's stuck
    I2C2CONbits.ON = 0;  // Disable I2C
    int i;
    for(i = 0; i < 10000; i++); // Small delay
    I2C2CONbits.ON = 1;  // Re-enable I2C
    
    // Check I2C bus state register
    char busState[64];
    sprintf(busState, "I2C Bus State: S=%d P=%d TRSTAT=%d\r\n", 
            I2C2STATbits.S, I2C2STATbits.P, I2C2STATbits.TRSTAT);
    UART1_WriteString(busState);
    
    // Start condition
    I2C2_Start();
    UART1_WriteString("I2C Start sent\r\n");
    
    // Send device address with write bit (0)
    connected = I2C2_Write(DAC_ADDR << 1);
    
    char addrMsg[64];
    sprintf(addrMsg, "Address sent (0x%02X), ACK: %d\r\n", DAC_ADDR << 1, connected);
    UART1_WriteString(addrMsg);
    
    // If we get an ACK, try to communicate with the device to verify it's a DAC
    if(connected) {
        // Try to write a register address (0x90 for DAC8571)
        bool regAck = I2C2_Write(0x90);
        sprintf(addrMsg, "Register addr sent (0x90), ACK: %d\r\n", regAck);
        UART1_WriteString(addrMsg);
        
        if(regAck) {
            // Now try to read from the device
            I2C2_Restart();
            UART1_WriteString("I2C Restart sent\r\n");
            
            bool readAck = I2C2_Write((DAC_ADDR << 1) | 0x01); // Read bit set
            sprintf(addrMsg, "Read addr sent (0x%02X), ACK: %d\r\n", (DAC_ADDR << 1) | 0x01, readAck);
            UART1_WriteString(addrMsg);
            
            if(readAck) {
                // Successfully started read operation, should be a valid DAC
                // Read a byte (don't ACK to stop after one byte)
                unsigned char data = I2C2_Read(0);
                sprintf(addrMsg, "Read data: 0x%02X\r\n", data);
                UART1_WriteString(addrMsg);
                
                // If we got this far, it's likely a DAC
                validDevice = true;
            }
        }
    }
    
    // Stop condition
    I2C2_Stop();
    UART1_WriteString("I2C Stop sent\r\n");
    
    // Print final result - only connected if we can both address and read from the device
    UART1_WriteString("DAC Check Result: ");
    UART1_WriteString((connected && validDevice) ? "CONNECTED\r\n" : "NOT CONNECTED\r\n");
    
    // Return true only if we get positive ACKs for all operations
    return (connected && validDevice);
}

//============================================================================
// Command Processing
//============================================================================
void processCommand(const char* cmd) {
    char command[16];
    char param1[16];
    unsigned int value;

    command[0] = param1[0] = '\0';
    sscanf(cmd, "%[^,],%s", command, param1);

    for(int i = 0; command[i]; i++) {
        if(command[i] >= 'a' && command[i] <= 'z') {
            command[i] = command[i] - 32;
        }
    }

    if(strcmp(command, "SETV") == 0) {
        value = atoi(param1);
        if(value > 0 && value <= 1000) {
            setDAC_Voltage(value);
        } else {
            UART1_WriteString("Error: Voltage must be between 1 and 1000 mV\r\n");
        }
    }
    else if(strcmp(command, "GETV?") == 0) {
        getDAC_Voltage();
    }
    else if(strcmp(command, "CHECK") == 0) {
        bool connected = checkDAC_Connection();
        UART1_WriteString("DAC: ");
        UART1_WriteString(connected ? "Connected\r\n" : "Not Connected\r\n");
    }
    else {
        UART1_WriteString("Unknown command. Use: SETV,<value>, GETV?, CHECK\r\n");
    }
}

//============================================================================
// Main
//============================================================================
int main(void) {
    // Basic system initialization
    SYSTEM_Init();
    
    // Delay to allow system to stabilize
    int i;
    for(i = 0; i < 1000000; i++) {
        // Simple delay loop
    }
    
    GPIO_Init();
    UART1_Init();
    
    // Simple printout to verify UART is working
    UART1_WriteString("UART Init OK\r\n");
    
    // Another delay loop
    for(i = 0; i < 1000000; i++) {
        // Simple delay loop
    }
    
    I2C2_Init();
    
    UART1_WriteString("I2C Init OK\r\n");
    
    // Another delay loop
    for(i = 0; i < 1000000; i++) {
        // Simple delay loop
    }

    UART1_WriteString("\r\n--- DAC8571 Test Program Ready ---\r\n");
    
    // First status check immediately
    UART1_WriteString("\r\nChecking DAC Connection...\r\n");
    
    // Check initial DAC connection
    dacConnected = checkDAC_Connection();
    UART1_WriteString("Initial DAC: ");
    UART1_WriteString(dacConnected ? "Connected\r\n" : "Not Connected\r\n");
    
    // Initialize previous DAC state
    bool prevDacState = dacConnected;
    
    // Counter for consecutive readings
    int consecutiveConnected = 0;
    int consecutiveDisconnected = 0;
    
    UART1_WriteString("\r\nStarting main loop - will check DAC every 2 seconds\r\n");
    
    // Variable to track time for 2-second interval
    unsigned long startTime = 0;
    
    while(1) {
        // Simple 2-second fixed delay using software loop
        UART1_WriteString("\r\nWaiting 2 seconds before next check...\r\n");
        
        // Fixed 2-second delay loop
        for(i = 0; i < 20; i++) {  // 20 x 100ms = 2000ms = 2 seconds
            // Process any UART commands during the wait
            for(int j = 0; j < 1000; j++) {
                if(UART1_DataReady()) {
                    char data = UART1_Read();
                    UART1_Write(data);

                    if(data == '\r' || data == '\n') {
                        if(cmdIndex > 0) {
                            cmdBuffer[cmdIndex] = '\0';
                            UART1_WriteString("\r\n");
                            processCommand(cmdBuffer);
                            cmdIndex = 0;
                        }
                    }
                    else if(data == '\b' || data == 0x7F) {
                        if(cmdIndex > 0) {
                            cmdIndex--;
                            UART1_WriteString("\b \b");
                        }
                    }
                    else if(cmdIndex < CMD_SIZE - 1) {
                        cmdBuffer[cmdIndex++] = data;
                    }
                }
                
                // Small delay ~100us
                for(int k = 0; k < 1000; k++);
            }
            
            // Print a marker every 100ms to show progress
            UART1_Write('.');
        }
        
        UART1_WriteString("\r\n");
        
        // Marker to show we're checking DAC status
        UART1_WriteString("\r\n--- Checking DAC Status ---\r\n");
        
        // Check DAC status
        bool currentStatus = checkDAC_Connection();
        
        // Print status
        UART1_WriteString("DAC Status: ");
        UART1_WriteString(currentStatus ? "Connected\r\n" : "Not Connected\r\n");
        
        // Track consecutive readings to filter out noise
        if(currentStatus) {
            consecutiveConnected++;
            consecutiveDisconnected = 0;
            UART1_WriteString("Connected count: ");
            char countStr[16];
            sprintf(countStr, "%d/%d\r\n", consecutiveConnected, CONSECUTIVE_THRESHOLD);
            UART1_WriteString(countStr);
        } else {
            consecutiveDisconnected++;
            consecutiveConnected = 0;
            UART1_WriteString("Disconnected count: ");
            char countStr[16];
            sprintf(countStr, "%d/%d\r\n", consecutiveDisconnected, CONSECUTIVE_THRESHOLD);
            UART1_WriteString(countStr);
        }
        
        // Only update status after multiple consistent readings
        if(consecutiveConnected >= CONSECUTIVE_THRESHOLD && !dacConnected) {
            dacConnected = true;
            UART1_WriteString("\r\n*** DAC NOW CONNECTED ***\r\n");
        } else if(consecutiveDisconnected >= CONSECUTIVE_THRESHOLD && dacConnected) {
            dacConnected = false;
            UART1_WriteString("\r\n*** DAC NOW DISCONNECTED ***\r\n");
        }
    }

    return 0;
}