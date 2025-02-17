// Configuration bits
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

#include <xc.h>
#include <sys/attribs.h>
#include <stdbool.h>


// Constants
#define SYSCLK  60000000L
#define PBCLK   60000000L
#define I2C_CLOCK_FREQ 100000    // 100kHz I2C clock
#define UART_BAUD 115200

// Function prototypes
void SYSTEM_Init(void);
void UART_Init(void);
void I2C_Init(void);
void I2C_ScanBus(void);
void UART_WriteString(const char* str);
void UART_WriteHex(unsigned char value);

// Simple delay function
void delay_ms(unsigned int ms) {
    unsigned int i;
    for(i = 0; i < (ms * (SYSCLK/2000)); i++) {
        asm("nop");
    }
}

// Initialize system clock and peripherals
void SYSTEM_Init(void) {
    // Disable interrupts
    __builtin_disable_interrupts();
    
    // Unlock system
    SYSKEY = 0;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    // Configure oscillator
    OSCCONbits.NOSC = 0x0001;    // FRCPLL
    OSCCONbits.FRCDIV = 0;       // FRC divider = 1
    OSCCONbits.PBDIV = 0;        // Peripheral bus clock = SYSCLK
    
    // Wait for clock switch and PLL lock
    while(OSCCONbits.OSWEN != 0);
    while(OSCCONbits.SLOCK != 1);
    
    // Lock system
    SYSKEY = 0x33333333;
    
    // Enable interrupts
    __builtin_enable_interrupts();
}

// Initialize UART for debug output
void UART_Init(void) {
    // Configure UART pins
    ANSELA &= ~(1 << 0);    // Set RA0 as digital (TX)
    ANSELA &= ~(1 << 4);    // Set RA4 as digital (RX)
    TRISAbits.TRISA0 = 0;   // TX as output
    TRISAbits.TRISA4 = 1;   // RX as input
    
    // Unlock PPS
    SYSKEY = 0;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;
    
    U1RXR = 0b0010;         // Map U1RX to RA4
    RPA0R = 0b0001;         // Map U1TX to RA0
    
    SYSKEY = 0x33333333;    // Lock PPS
    
    // Configure UART
    U1MODEbits.ON = 0;      // Disable UART
    U1MODE = 0;             // Clear mode register
    U1STA = 0;              // Clear status register
    U1BRG = ((PBCLK / (16 * UART_BAUD)) - 1);
    
    U1MODEbits.BRGH = 0;    // Standard Speed mode
    U1MODEbits.PDSEL = 0;   // 8-bit data, no parity
    U1MODEbits.STSEL = 0;   // 1 stop bit
    
    U1STAbits.UTXEN = 1;    // Enable transmit
    U1STAbits.URXEN = 1;    // Enable receive
    U1MODEbits.ON = 1;      // Enable UART
    
    delay_ms(1);
}

// Initialize I2C
void I2C_Init(void) {
    // Disable I2C before configuration
    I2C1CONbits.ON = 0;
    
    // Configure I2C pins
//    ANSELBbits.ANSB8 = 0;   // Set RB8 as digital (SCL)
//    ANSELBbits.ANSB9 = 0;   // Set RB9 as digital (SDA)
//    TRISBbits.TRISB8 = 1;   // SCL as input
//    TRISBbits.TRISB9 = 1;   // SDA as input
    TRISBbits.TRISB3 = 0;    // SCL2
    TRISBbits.TRISB2 = 0;    // SDA2
    
    // Set I2C baud rate
    I2C1BRG = (PBCLK / (2 * I2C_CLOCK_FREQ)) - 2;
    
    // Configure I2C
    I2C1CONbits.SIDL = 0;    // Continue in idle mode
    I2C1CONbits.DISSLW = 1;  // Disable slew rate control
    I2C1CONbits.ACKDT = 0;   // Send ACK during acknowledge
    
    // Enable I2C
    I2C1CONbits.ON = 1;
}

// Send I2C start condition
bool I2C_Start(void) {
    // Check if I2C is idle
    if(I2C1CONbits.SEN || I2C1CONbits.PEN || I2C1CONbits.RSEN || 
       I2C1CONbits.RCEN || I2C1CONbits.ACKEN) {
        return false;
    }
    
    I2C1CONbits.SEN = 1;            // Send start condition
    while(I2C1CONbits.SEN == 1);    // Wait for start to complete
    return true;
}

// Send I2C stop condition
void I2C_Stop(void) {
    I2C1CONbits.PEN = 1;            // Send stop condition
    while(I2C1CONbits.PEN == 1);    // Wait for stop to complete
}

// Write byte to I2C bus
bool I2C_WriteByte(unsigned char data) {
    I2C1TRN = data;                     // Load data into transmit register
    while(I2C1STATbits.TRSTAT == 1);    // Wait for transmission
    return (I2C1STATbits.ACKSTAT == 0); // Return ACK status
}

// Scan I2C bus for devices
void I2C_ScanBus(void) {
    bool deviceFound = false;
    unsigned char address;
    
    UART_WriteString("Starting I2C bus scan...\r\n");
    
    // Scan all possible 7-bit addresses (0x08-0x77)
    for(address = 0x08; address < 0x78; address++) {
        if(I2C_Start()) {
            if(I2C_WriteByte((address << 1) | 0)) {
                UART_WriteString("Device found at address: ");
                UART_WriteHex(address);
                UART_WriteString("\r\n");
                deviceFound = true;
            }
            I2C_Stop();
        }
        delay_ms(5);  // Small delay between scans
    }
    
    if(!deviceFound) {
        UART_WriteString("No I2C devices found\r\n");
    }
    
    UART_WriteString("Scan complete\r\n\n");
}

// Write string to UART
void UART_WriteString(const char* str) {
    while(*str != '\0') {
        while(U1STAbits.UTXBF == 1);  // Wait if buffer is full
        U1TXREG = *str++;             // Send character
    }
}

// Write hex value to UART
void UART_WriteHex(unsigned char value) {
    char hexChars[] = "0123456789ABCDEF";
    char hexStr[5];
    
    hexStr[0] = '0';
    hexStr[1] = 'x';
    hexStr[2] = hexChars[(value >> 4) & 0x0F];
    hexStr[3] = hexChars[value & 0x0F];
    hexStr[4] = '\0';
    
    UART_WriteString(hexStr);
}

int main(void) {
    // Initialize system
    SYSTEM_Init();
    UART_Init();
    I2C_Init();
    
    delay_ms(500);  // Startup delay
    
    UART_WriteString("PIC32MX I2C Scanner\r\n");
    UART_WriteString("================\r\n\n");
    
    while(1) {
        I2C_ScanBus();       // Scan for I2C devices
        delay_ms(2000);      // Wait 2 seconds between scans
    }
    
    return 0;
}