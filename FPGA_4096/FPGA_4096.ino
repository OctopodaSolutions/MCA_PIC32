// Buffer size for FPGA spectrum data
#define SPECTRUM_DATA_SIZE 4096 // Match PIC32's 4K channels
#define CHUNK_SIZE 64           // Send data in 64-byte chunks

// FPGA command defines (match PIC32)
#define FPGA_START_CMD  0x01
#define FPGA_STOP_CMD   0x02
#define FPGA_RESET_CMD  0x03
#define FPGA_START_BIT  0xDA
#define FPGA_END_BIT    0xDE

// Sequence number for packet tracking
static uint8_t packetSeqNum = 0;

// Structure to store FPGA settings (mirroring PIC32's Settings_t)
struct Settings_t {
    uint8_t fineGain;
    uint16_t threshold;
    uint16_t riseTime;
    uint16_t flatTime;
    uint16_t poleZeros;
    uint16_t digitalBlr;
    uint8_t pileupReject;
    uint8_t polarity;
    uint8_t dwellTime; // MCA mode only
};

// Current FPGA settings
static Settings_t currentSettings = {0};

// Function to print raw input data in hexadecimal
void printRawInputData(uint8_t* buffer, uint8_t len) {
    Serial.print("Raw input data received: ");
    for (uint8_t i = 0; i < len; i++) {
        Serial.print("0x");
        if (buffer[i] < 0x10) Serial.print("0"); // Pad with zero for single-digit hex
        Serial.print(buffer[i], HEX);
        if (i < len - 1) Serial.print(", ");
    }
    Serial.println();
}

// Function to send spectrum data packet with random data
void sendSpectrumData() {
    uint8_t randomData[SPECTRUM_DATA_SIZE]; // Buffer for 4096 random bytes

    // Generate 4096 bytes of random data
    for (uint16_t i = 0; i < SPECTRUM_DATA_SIZE; i++) {
        randomData[i] = random(0, 256); // Random byte (0x00 to 0xFF)
    }

    // Step 1: Send start byte
    Serial2.write(FPGA_START_BIT); // 0xDA
    Serial2.flush();
    Serial.println("Sent start byte: 0xDA");
    delay(50); // 50ms delay to allow PIC32 to process

    // Step 2: Send sequence number
    Serial2.write(packetSeqNum);
    Serial2.flush();
    Serial.print("Sent sequence number: 0x");
    Serial.println(packetSeqNum, HEX);
    delay(20); // 20ms delay

    // Step 3: Send 4096 bytes of data in chunks
    for (uint16_t i = 0; i < SPECTRUM_DATA_SIZE; i += CHUNK_SIZE) {
        uint16_t bytesToSend = min(CHUNK_SIZE, SPECTRUM_DATA_SIZE - i);
        Serial2.write(&randomData[i], bytesToSend);
        Serial2.flush();
        Serial.print("Sent data chunk ");
        Serial.print(i / CHUNK_SIZE + 1);
        Serial.print(" (");
        Serial.print(bytesToSend);
        Serial.println(" bytes)");
        delay(20); // 20ms delay between chunks
    }

    // Step 4: Send end byte
    Serial2.write(FPGA_END_BIT); // 0xDE
    Serial2.flush();
    Serial.println("Sent end byte: 0xDE");

    // Debug output for entire packet
    Serial.print("Sent complete packet (Seq: ");
    Serial.print(packetSeqNum);
    Serial.print(", first two bytes: 0x");
    Serial.print(randomData[0], HEX);
    Serial.print(", 0x");
    Serial.println(randomData[1], HEX);
    packetSeqNum++; // Increment sequence number
    delay(2000); // 2s delay to allow PIC32 processing
}

// Function to process received FPGA settings (11-byte command)
void processSettingsCommand(uint8_t* cmd, uint8_t len) {
    if (len != 11 || cmd[0] != FPGA_START_BIT || cmd[10] != FPGA_END_BIT) {
        Serial.println("Invalid settings command format");
        return;
    }

    // Print raw input data
    printRawInputData(cmd, len);

    // Parse 11-byte command (matches PIC32's fpgaCmd format)
    currentSettings.fineGain = cmd[1];
    currentSettings.threshold = ((uint16_t)cmd[2] << 8) | cmd[3];
    currentSettings.riseTime = ((uint16_t)cmd[4] << 2) | ((cmd[5] >> 6) & 0x03);
    currentSettings.flatTime = ((uint16_t)(cmd[5] & 0x3F) << 4) | ((cmd[6] >> 4) & 0x0F);
    currentSettings.poleZeros = ((uint16_t)(cmd[6] & 0x0F) << 4) | ((cmd[7] >> 4) & 0x0F);
    currentSettings.digitalBlr = ((uint16_t)(cmd[7] & 0x0F) << 2) | ((cmd[8] >> 6) & 0x03);
    currentSettings.pileupReject = (cmd[8] >> 5) & 0x01;
    currentSettings.polarity = (cmd[8] >> 4) & 0x01;
    currentSettings.dwellTime = cmd[9];

 
    Serial.println("Received FPGA settings:");
    Serial.print("Fine Gain: "); Serial.println(currentSettings.fineGain);
    Serial.print("Threshold: "); Serial.println(currentSettings.threshold);
    Serial.print("Rise Time: "); Serial.println(currentSettings.riseTime);
    Serial.print("Flat Time: "); Serial.println(currentSettings.flatTime);
    Serial.print("Pole Zeros: "); Serial.println(currentSettings.poleZeros);
    Serial.print("Digital BLR: "); Serial.println(currentSettings.digitalBlr);
    Serial.print("Pileup Reject: "); Serial.println(currentSettings.pileupReject);
    Serial.print("Polarity: "); Serial.println(currentSettings.polarity);
    Serial.print("Dwell Time: "); Serial.println(currentSettings.dwellTime);

 
    Serial.println("Settings updated");
}

void setup() {

    randomSeed(analogRead(A0)); 
    Serial.begin(115200);
    Serial2.begin(115200);
    Serial.println("Arduino Mega: Serial2 initialized for PIC communication");
    delay(1000);
}

void loop() {
    static bool expectingCommand = false;
    static bool expectingEnd = false;
    static uint8_t lastCommand = 0;
    static bool isAcquisitionRunning = false;
    static unsigned long lastDataSent = 0;
    const unsigned long dataInterval = 5000; // 5s interval for packets

    static uint8_t settingsBuffer[11]; // Buffer for 11-byte settings command
    static uint8_t settingsIndex = 0;  // Index for settings command parsing

  
    if (Serial2.available() > 0) {
        uint8_t byte = Serial2.read();

        // Check for start of a command
        if (!expectingCommand && byte == FPGA_START_BIT) {
            expectingCommand = true;
            settingsIndex = 0;
            settingsBuffer[settingsIndex++] = byte;
            Serial.println("Received start byte (0xDA)");
            return;
        }

        // Process command bytes
        if (expectingCommand && !expectingEnd) {
            settingsBuffer[settingsIndex++] = byte;

            // Check if it's a 3-byte command (START, STOP, RESET)
            if (settingsIndex == 2) {
                lastCommand = byte;
                expectingEnd = true;
                switch (byte) {
                    case FPGA_START_CMD:
                        Serial.println("Received START (0x01)");
                        isAcquisitionRunning = true;
                        lastDataSent = 0;
                        break;
                    case FPGA_STOP_CMD:
                        Serial.println("Received STOP (0x02)");
                        isAcquisitionRunning = false;
                        break;
                    case FPGA_RESET_CMD:
                        Serial.println("Received RESET (0x03)");
                        isAcquisitionRunning = false;
                        break;
                    default:
                        // Not a 3-byte command, continue collecting for 11-byte settings
                        expectingEnd = false;
                        break;
                }
            }
            // Check for 11-byte settings command
            else if (settingsIndex == 11) {
                if (byte == FPGA_END_BIT) {
                    Serial.println("Received 11-byte settings command");
                    processSettingsCommand(settingsBuffer, settingsIndex);
                    expectingCommand = false;
                    settingsIndex = 0;
                } else {
                    Serial.println("Invalid settings command: missing end byte");
                    expectingCommand = false;
                    settingsIndex = 0;
                }
            }
            return;
        }

        // Check for end byte
        if (expectingEnd && byte == FPGA_END_BIT) {
            expectingCommand = false;
            expectingEnd = false;
            settingsBuffer[settingsIndex++] = byte;
            Serial.println("Received end byte (0xDE), processing command");

            // Print raw input data for 3-byte command
            printRawInputData(settingsBuffer, settingsIndex);

            delay(2000); // 2s delay to ensure PIC32 is ready

            switch (lastCommand) {
                case FPGA_START_CMD:
                    sendSpectrumData();
                    Serial.println("Sent spectrum data for START command");
                    break;
                case FPGA_STOP_CMD:
                    Serial.println("Processed STOP command (no spectrum data sent)");
                    break;
                case FPGA_RESET_CMD:
                    Serial.println("Processed RESET command (no spectrum data sent)");
                    break;
                default:
                    Serial.print("Unknown command: 0x");
                    Serial.println(lastCommand, HEX);
                    break;
            }
            settingsIndex = 0;
        } else if (expectingEnd) {
            Serial.print("Expected end byte (0xDE), got: 0x");
            Serial.println(byte, HEX);
            expectingCommand = false;
            expectingEnd = false;
            settingsIndex = 0;
        }
    }

    // Send spectrum data every 5 seconds if acquisition is running
    if (isAcquisitionRunning && (millis() - lastDataSent >= dataInterval)) {
        sendSpectrumData();
        lastDataSent = millis();
    }
}