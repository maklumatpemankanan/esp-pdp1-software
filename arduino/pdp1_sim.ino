/*
 * PDP-1 Simulator für ESP32 mit SD-Karte und LED-Anzeige
 * Implementiert die vollständige 18-bit PDP-1 Architektur
 * 
 * Hardware-Anforderungen:
 * - ESP32 (mindestens 520KB RAM)
 * - SD-Karte (SPI-Modus)
 * - 8x MCP23S17 Port-Expander für LED-Anzeige
 * 
 * SPI Verkabelung:
 * - MOSI -> GPIO 23
 * - MISO -> GPIO 19
 * - SCK  -> GPIO 18
 * - SD CS   -> GPIO 5
 * - MCP CS  -> GPIO 15 (alle 8 Chips parallel, Adressierung via Hardware-Pins)
 * 
 * Kompilierung: Arduino IDE oder PlatformIO mit ESP32 Board
 * Bibliotheken: SD.h, SPI.h (in ESP32 Core enthalten)
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>

// ============================================================================
// PDP-1 Architektur Konstanten (MUSS VOR allen Klassen stehen)
// ============================================================================

#define WORD_MASK 0777777      // 18-bit Wortmaske (oktal)
#define SIGN_BIT  0400000      // Bit 0 (Vorzeichenbit)
#define ADDR_MASK 07777        // 12-bit Adressmaske
#define MEMORY_SIZE 4096       // 4K Worte (0000-7777 oktal)

// Instruction Format Masken
#define OP_MASK   0760000      // Bits 0-4 (Operation)
#define I_BIT     0010000      // Bit 5 (Indirect)
#define Y_MASK    0007777      // Bits 6-17 (Adresse/Operand)

// ============================================================================
// Hardware Konfiguration
// ============================================================================

#define SD_CS_PIN 16
#define MCP_CS_PIN 17

// I2C Pins (ESP32 Standard)
#define I2C_SDA 21
#define I2C_SCL 22
#define PCF_INT_PIN 27  // Interrupt Pin für PCF8574 (0x25, 0x26)

// TCA9548A I2C Multiplexer
#define TCA9548A_ADDR 0x70
#define TCA9548A_CHANNEL 1  // PCF8574 sind an Kanal 1

// MCP23S17 Register
#define MCP_IODIRA   0x00
#define MCP_IODIRB   0x01
#define MCP_GPIOA    0x12
#define MCP_GPIOB    0x13

// MCP23S17 Opcodes
#define MCP_WRITE_CMD 0x40
#define MCP_READ_CMD  0x41

// PCF8574 I2C Adressen
#define PCF_ADDR_0x20 0x20  // Address Switches 16-9
#define PCF_ADDR_0x21 0x21  // Address Switches 8-1
#define PCF_ADDR_0x22 0x22  // Test Word 18-11
#define PCF_ADDR_0x23 0x23  // Test Word 10-3
#define PCF_ADDR_0x24 0x24  // Test Word 2-1, Sense Switches 1-6
#define PCF_ADDR_0x25 0x25  // Control Switches
#define PCF_ADDR_0x26 0x26  // Control Switches 2

// ============================================================================
// PCF8574 Switch Controller (I2C) mit Interrupt
// ============================================================================

// Globale Interrupt-Flag Variable (muss außerhalb der Klasse sein für IRAM)
volatile bool DRAM_ATTR g_switchInterruptFlag = false;

// ISR muss außerhalb der Klasse definiert werden
void IRAM_ATTR switchISR() {
    g_switchInterruptFlag = true;
}

class SwitchController {
private:
    // Switch state cache
    uint8_t switchState[7];
    uint8_t lastSwitchState[7];
    
    // Debounce timing
    unsigned long lastDebounceTime[7][8];
    bool debouncedState[7][8];
    const unsigned long debounceDelay = 50;
    
public:
    SwitchController() {
        memset(switchState, 0xFF, sizeof(switchState));
        memset(lastSwitchState, 0xFF, sizeof(lastSwitchState));
        memset(lastDebounceTime, 0, sizeof(lastDebounceTime));
        memset(debouncedState, 1, sizeof(debouncedState));
        g_switchInterruptFlag = false;
    }
    
    static void IRAM_ATTR handleInterrupt() {
        g_switchInterruptFlag = true;
    }
    
    void begin() {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        
        // TCA9548A auf Kanal 1 umschalten (für PCF8574)
        selectTCAChannel(TCA9548A_CHANNEL);
        
        // Interrupt Pin konfigurieren (Active LOW)
        pinMode(PCF_INT_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PCF_INT_PIN), switchISR, FALLING);
        
        Serial.println("Switch Controller initialisiert (7x PCF8574)");
        Serial.printf("TCA9548A Kanal %d aktiviert\n", TCA9548A_CHANNEL);
        Serial.printf("Interrupt Pin: GPIO%d (für Control Switches 0x25, 0x26)\n", PCF_INT_PIN);
        
        // Initialisiere alle PCF8574 als Inputs
        for (uint8_t addr = 0x20; addr <= 0x26; addr++) {
            selectTCAChannel(TCA9548A_CHANNEL);
            Wire.beginTransmission(addr);
            Wire.write(0xFF);  // Alle Pins als Input mit Pull-ups
            uint8_t error = Wire.endTransmission();
            if (error != 0) {
                Serial.printf("WARNUNG: PCF8574 bei 0x%02X antwortet nicht (Error: %d)\n", addr, error);
            } else {
                Serial.printf("PCF8574 bei 0x%02X initialisiert\n", addr);
            }
        }
        
        // Erste Lesung (löscht Interrupt)
        update();
        g_switchInterruptFlag = false;
    }
    
    void selectTCAChannel(uint8_t channel) {
        if (channel > 7) return;
        
        Wire.beginTransmission(TCA9548A_ADDR);
        Wire.write(1 << channel);  // Bit-Maske für Kanal
        Wire.endTransmission();
    }
    
    uint8_t readPCF8574(uint8_t address) {
        selectTCAChannel(TCA9548A_CHANNEL);  // Stelle sicher, dass Kanal aktiv ist
        Wire.requestFrom(address, (uint8_t)1);
        if (Wire.available()) {
            return Wire.read();
        }
        return 0xFF;
    }
    
    void update() {
        unsigned long currentTime = millis();
        
        // IMMER Control Switches lesen (0x25, 0x26)
        for (uint8_t i = 5; i <= 6; i++) {
            uint8_t addr = 0x20 + i;
            uint8_t reading = readPCF8574(addr);
            switchState[i] = reading;
            
            // Debounce
            for (uint8_t pin = 0; pin < 8; pin++) {
                bool currentState = (reading >> pin) & 1;
                bool lastState = (lastSwitchState[i] >> pin) & 1;
                
                if (currentState != lastState) {
                    lastDebounceTime[i][pin] = currentTime;
                }
                
                if ((currentTime - lastDebounceTime[i][pin]) > debounceDelay) {
                    debouncedState[i][pin] = currentState;
                }
            }
            
            lastSwitchState[i] = reading;
        }
        
        // Interrupt-Flag löschen (falls gesetzt)
        if (g_switchInterruptFlag) {
            g_switchInterruptFlag = false;
        }
        
        // Periodisches Lesen aller anderen Chips (Address, Test Word, Sense)
        static unsigned long lastFullUpdate = 0;
        if (currentTime - lastFullUpdate > 100) {  // Alle 100ms
            lastFullUpdate = currentTime;
            
            for (uint8_t i = 0; i < 5; i++) {  // Chips 0x20-0x24
                uint8_t addr = 0x20 + i;
                uint8_t reading = readPCF8574(addr);
                switchState[i] = reading;
                
                for (uint8_t pin = 0; pin < 8; pin++) {
                    bool currentState = (reading >> pin) & 1;
                    bool lastState = (lastSwitchState[i] >> pin) & 1;
                    
                    if (currentState != lastState) {
                        lastDebounceTime[i][pin] = currentTime;
                    }
                    
                    if ((currentTime - lastDebounceTime[i][pin]) > debounceDelay) {
                        debouncedState[i][pin] = currentState;
                    }
                }
                
                lastSwitchState[i] = reading;
            }
        }
    }
    
    bool isPressed(uint8_t address, uint8_t pin) {
        if (address < 0x20 || address > 0x26) return false;
        uint8_t index = address - 0x20;
        if (pin > 7) return false;
        return debouncedState[index][pin];
    }
    
    bool wasPressed(uint8_t address, uint8_t pin) {
        if (address < 0x20 || address > 0x26) return false;
        if (pin > 7) return false;
        uint8_t index = address - 0x20;
        
        bool current = debouncedState[index][pin];
        bool last = (lastSwitchState[index] >> pin) & 1;
        return current && !last;
    }
    
    bool hasInterrupt() {
        return g_switchInterruptFlag;
    }
    
    // Convenience Functions
/*
    uint16_t getAddressSwitches() {
        uint16_t addr = 0;
        addr |= (switchState[0] & 0xFF) << 8;
        addr |= (switchState[1] & 0xFF);
        return addr;
    }
*/
    uint16_t getAddressSwitches() {
    uint16_t addr = 0;
    
    // PCF 0x21 (Index 1) - Bits 0-7 (niederwertige Bits)
    // Pins sind in umgekehrter Reihenfolge: Pin 7→Bit 0, Pin 6→Bit 1, etc.
    addr |= ((switchState[1] >> 7) & 1) << 0;   // Pin 7 → Bit 0
    addr |= ((switchState[1] >> 6) & 1) << 1;   // Pin 6 → Bit 1
    addr |= ((switchState[1] >> 5) & 1) << 2;   // Pin 5 → Bit 2
    addr |= ((switchState[1] >> 4) & 1) << 3;   // Pin 4 → Bit 3
    addr |= ((switchState[1] >> 3) & 1) << 4;   // Pin 3 → Bit 4
    addr |= ((switchState[1] >> 2) & 1) << 5;   // Pin 2 → Bit 5
    addr |= ((switchState[1] >> 1) & 1) << 6;   // Pin 1 → Bit 6
    addr |= ((switchState[1] >> 0) & 1) << 7;   // Pin 0 → Bit 7
    
    // PCF 0x20 (Index 0) - Bits 8-15 (höherwertige Bits)
    // Auch hier: Pin 7→Bit 8, Pin 6→Bit 9, etc.
    addr |= ((switchState[0] >> 7) & 1) << 8;   // Pin 7 → Bit 8
    addr |= ((switchState[0] >> 6) & 1) << 9;   // Pin 6 → Bit 9
    addr |= ((switchState[0] >> 5) & 1) << 10;  // Pin 5 → Bit 10
    addr |= ((switchState[0] >> 4) & 1) << 11;  // Pin 4 → Bit 11
    addr |= ((switchState[0] >> 3) & 1) << 12;  // Pin 3 → Bit 12
    addr |= ((switchState[0] >> 2) & 1) << 13;  // Pin 2 → Bit 13
    addr |= ((switchState[0] >> 1) & 1) << 14;  // Pin 1 → Bit 14
    addr |= ((switchState[0] >> 0) & 1) << 15;  // Pin 0 → Bit 15
    
    return addr;
}
/*
    uint32_t getTestWord() {
        uint32_t tw = 0;
        tw |= ((uint32_t)(switchState[2] & 0xFF)) << 10;
        tw |= ((uint32_t)(switchState[3] & 0xFF)) << 2;
        tw |= ((uint32_t)(switchState[4] & 0x03));
        return tw & WORD_MASK;
    }
  */

    uint32_t getTestWord() {
    uint32_t tw = 0;
    
    // PCF 0x22 (Index 2) - Bits 17-10 (höchstwertige Bits, absteigend)
    tw |= ((switchState[2] >> 0) & 1) << 17;  // Pin 0 → Bit 17
    tw |= ((switchState[2] >> 1) & 1) << 16;  // Pin 1 → Bit 16
    tw |= ((switchState[2] >> 2) & 1) << 15;  // Pin 2 → Bit 15
    tw |= ((switchState[2] >> 3) & 1) << 14;  // Pin 3 → Bit 14
    tw |= ((switchState[2] >> 4) & 1) << 13;  // Pin 4 → Bit 13
    tw |= ((switchState[2] >> 5) & 1) << 12;  // Pin 5 → Bit 12
    tw |= ((switchState[2] >> 6) & 1) << 11;  // Pin 6 → Bit 11
    tw |= ((switchState[2] >> 7) & 1) << 10;  // Pin 7 → Bit 10
    
    // PCF 0x23 (Index 3) - Bits 9-2
    tw |= ((switchState[3] >> 0) & 1) << 9;   // Pin 0 → Bit 9
    tw |= ((switchState[3] >> 1) & 1) << 8;   // Pin 1 → Bit 8
    tw |= ((switchState[3] >> 2) & 1) << 7;   // Pin 2 → Bit 7
    tw |= ((switchState[3] >> 3) & 1) << 6;   // Pin 3 → Bit 6
    tw |= ((switchState[3] >> 4) & 1) << 5;   // Pin 4 → Bit 5
    tw |= ((switchState[3] >> 5) & 1) << 4;   // Pin 5 → Bit 4
    tw |= ((switchState[3] >> 6) & 1) << 3;   // Pin 6 → Bit 3
    tw |= ((switchState[3] >> 7) & 1) << 2;   // Pin 7 → Bit 2
    
    // PCF 0x24 (Index 4) - Bits 0-1 (niederwertigste Bits)
    tw |= ((switchState[4] >> 0) & 1) << 0;   // Pin 0 → Bit 0
    tw |= ((switchState[4] >> 1) & 1) << 1;   // Pin 1 → Bit 1
    
    return tw & WORD_MASK;  // Mask auf 18 Bits
}

    uint8_t getSenseSwitches() {
        //return (switchState[4] >> 2) & 0x3F;
        uint8_t raw = (switchState[4] >> 2) & 0x3F;
          
          // Bits umkehren: Schalter 0 (links) → Bit 5, Schalter 5 (rechts) → Bit 0
          uint8_t reversed = 0;
          for (int i = 0; i < 6; i++) {
              if (raw & (1 << i)) {
                  reversed |= (1 << (5 - i));
              }
          }
          
          return reversed;

    }
    
    // Control Switches (0x25)
    bool getExtendSwitch()  { return isPressed(0x25, 0); }
    bool getStartDown()     { return isPressed(0x25, 1); }
    bool getStartUp()       { return isPressed(0x25, 2); }
    //bool getStop()          { return isPressed(0x25, 3); }
    bool getStop() {
      //update();  // Stelle sicher dass aktueller Wert gelesen wird
      return (switchState[5] >> 3) & 1;  // Direkter Zugriff ohne Debounce
    }

    bool getContinue()      { return isPressed(0x25, 4); }
    bool getExamine()       { return isPressed(0x25, 5); }
    bool getDeposit()       { return isPressed(0x25, 6); }
    bool getReadIn()        { return isPressed(0x25, 7); }
    
    // Control Switches 2 (0x26)
    bool getPower()         { return isPressed(0x26, 0); }
    bool getSingleStep()    { return isPressed(0x26, 1); }
    bool getSingleInstr()   { return isPressed(0x26, 2); }
    bool getReaderDown()    { return isPressed(0x26, 3); }
    bool getReaderUp()      { return isPressed(0x26, 4); }
    bool getTapeFeedDown()  { return isPressed(0x26, 5); }
    bool getTapeFeedUp()    { return isPressed(0x26, 6); }
    
    // Edge Detection
    bool getStartDownPressed()   { return wasPressed(0x25, 1); }
    bool getStartUpPressed()     { return wasPressed(0x25, 2); }
    bool getStopPressed()        { return wasPressed(0x25, 3); }
    bool getContinuePressed()    { return wasPressed(0x25, 4); }
    bool getExaminePressed()     { return wasPressed(0x25, 5); }
    bool getDepositPressed()     { return wasPressed(0x25, 6); }
    bool getReadInPressed()      { return wasPressed(0x25, 7); }
    bool getSingleStepPressed()  { return wasPressed(0x26, 1); }
    bool getSingleInstrPressed() { return wasPressed(0x26, 2); }
    
    void printStatus() {
        Serial.println("\n=== Switch Status ===");
        Serial.printf("Address Switches: %04o (oktal) = %d (dezimal)\n", 
                     getAddressSwitches(), getAddressSwitches());
        Serial.printf("Test Word: %06o (oktal)\n", getTestWord());
        Serial.printf("Sense Switches: %02o (oktal)\n", getSenseSwitches());
        
        Serial.println("\nControl Switches:");
        Serial.printf("  Extend: %d  Start: %d/%d  Stop: %d  Continue: %d\n",
                     getExtendSwitch(), getStartDown(), getStartUp(), 
                     getStop(), getContinue());
        Serial.printf("  Examine: %d  Deposit: %d  Read In: %d\n",
                     getExamine(), getDeposit(), getReadIn());
        Serial.printf("  Power: %d  Single Step: %d  Single Instr: %d\n",
                     getPower(), getSingleStep(), getSingleInstr());
        Serial.printf("Interrupt Status: %s\n", g_switchInterruptFlag ? "PENDING" : "Clear");
        
        // Debug: Zeige Raw-Werte
        Serial.println("\nRaw PCF8574 Werte (Hex):");
        for (uint8_t i = 0; i < 7; i++) {
            Serial.printf("  0x%02X: 0x%02X (binär: ", 0x20 + i, switchState[i]);
            for (int b = 7; b >= 0; b--) {
                Serial.print((switchState[i] >> b) & 1);
            }
            Serial.println(")");
        }
        Serial.println("=====================\n");
    }
};

// ============================================================================
// PDP-1 Architektur Konstanten
// ============================================================================

#define WORD_MASK 0777777
#define SIGN_BIT  0400000
#define ADDR_MASK 07777
#define MEMORY_SIZE 4096

#define OP_MASK   0760000
#define I_BIT     0010000
#define Y_MASK    0007777

// ============================================================================
// MCP23S17 LED Controller
// ============================================================================

class LEDController {
private:
    SPIClass* spi;
    uint8_t csPin;
    
    // Cache für Port-Zustände (8 Chips x 2 Ports)
    uint8_t portCache[8][2];  // [chip][port A=0, B=1]
    
    // LED Mapping aus CSV
    struct LEDMap {
        const char* reg;
        uint8_t bit;
        uint8_t chip;
        char port;      // 'A' or 'B'
        uint8_t pin;
    };
    
    static const LEDMap ledMapping[];
    static const int ledMappingSize;
    
public:
    LEDController(SPIClass* spiInstance, uint8_t cs) : spi(spiInstance), csPin(cs) {
        memset(portCache, 0, sizeof(portCache));
    }
    
    void begin() {
        pinMode(csPin, OUTPUT);
        digitalWrite(csPin, HIGH);
        
        // Initialisiere alle 8 MCP23S17 Chips
        for (uint8_t chip = 0; chip < 8; chip++) {
            // WICHTIG: IOCON.HAEN (Hardware Address Enable) setzen
            // Ohne HAEN reagieren alle Chips auf Adresse 0!
            writeRegister(chip, 0x0A, 0x08);  // IOCON Register, HAEN = 1 (Bit 3)
            
            // Setze alle Pins als Output
            writeRegister(chip, MCP_IODIRA, 0x00);
            writeRegister(chip, MCP_IODIRB, 0x00);
            
            // Lösche alle LEDs
            writeRegister(chip, MCP_GPIOA, 0x00);
            writeRegister(chip, MCP_GPIOB, 0x00);
        }
        
        Serial.println("LED Controller initialisiert (8x MCP23S17)");
    }
    
    void writeRegister(uint8_t chip, uint8_t reg, uint8_t value) {
        digitalWrite(csPin, LOW);
        spi->transfer(MCP_WRITE_CMD | ((chip & 0x07) << 1));
        spi->transfer(reg);
        spi->transfer(value);
        digitalWrite(csPin, HIGH);
    }
    
    uint8_t readRegister(uint8_t chip, uint8_t reg) {
        digitalWrite(csPin, LOW);
        spi->transfer(MCP_READ_CMD | ((chip & 0x07) << 1));
        spi->transfer(reg);
        uint8_t value = spi->transfer(0x00);
        digitalWrite(csPin, HIGH);
        return value;
    }
    
    void setLED(uint8_t chip, char port, uint8_t pin, bool state) {
        uint8_t portIndex = (port == 'B') ? 1 : 0;
        
        if (state) {
            portCache[chip][portIndex] |= (1 << pin);
        } else {
            portCache[chip][portIndex] &= ~(1 << pin);
        }
        
        uint8_t reg = (port == 'B') ? MCP_GPIOB : MCP_GPIOA;
        writeRegister(chip, reg, portCache[chip][portIndex]);
    }
    
    void updateDisplay(uint32_t ac, uint32_t io, uint16_t pc, uint16_t ma, 
                      uint32_t mb, uint32_t instr, bool ov, uint8_t pf,
                      uint8_t senseSw, bool power, bool run, bool step) {
        
        // Durchlaufe alle LED-Mappings
        for (int i = 0; i < ledMappingSize; i++) {
            const LEDMap& led = ledMapping[i];
            bool state = false;
            
            if (strcmp(led.reg, "AC") == 0) {
                state = (ac >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "IO") == 0) {
                state = (io >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "PC") == 0) {
                state = (pc >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "MA") == 0) {
                state = (ma >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "MB") == 0) {
                state = (mb >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "INSTR") == 0) {
                state = (instr >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "PF") == 0) {
                state = (pf >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "SW") == 0) {
                state = (senseSw >> led.bit) & 1;
            }
            else if (strcmp(led.reg, "Overflow") == 0) {
                state = ov;
            }
            else if (strcmp(led.reg, "Power") == 0) {
                state = power;
            }
            else if (strcmp(led.reg, "Run") == 0) {
                state = run;
            }
            else if (strcmp(led.reg, "Step") == 0) {
                state = step;
            }
            
            setLED(led.chip, led.port, led.pin, state);
        }
    }
    
    void allOff() {
        for (uint8_t chip = 0; chip < 8; chip++) {
            writeRegister(chip, MCP_GPIOA, 0x00);
            writeRegister(chip, MCP_GPIOB, 0x00);
            portCache[chip][0] = 0;
            portCache[chip][1] = 0;
        }
    }
    
    void testPattern() {
        Serial.println("LED Test Pattern...");
        
        // Test jedes Chip einzeln
        for (uint8_t chip = 0; chip < 8; chip++) {
            Serial.printf("Testing Chip %d\n", chip);
            
            // Port A
            for (uint8_t pin = 0; pin < 8; pin++) {
                setLED(chip, 'A', pin, true);
                delay(50);
                setLED(chip, 'A', pin, false);
            }
            
            // Port B
            for (uint8_t pin = 0; pin < 8; pin++) {
                setLED(chip, 'B', pin, true);
                delay(50);
                setLED(chip, 'B', pin, false);
            }
        }
        
        Serial.println("LED Test abgeschlossen");
    }
    
    void dumpPortCache() {
        Serial.println("\n=== MCP23S17 Port Cache ===");
        for (uint8_t chip = 0; chip < 8; chip++) {
            Serial.printf("Chip %d: Port A=0x%02X (", chip, portCache[chip][0]);
            for (int b = 7; b >= 0; b--) {
                Serial.print((portCache[chip][0] >> b) & 1);
            }
            Serial.printf(") Port B=0x%02X (", portCache[chip][1]);
            for (int b = 7; b >= 0; b--) {
                Serial.print((portCache[chip][1] >> b) & 1);
            }
            Serial.println(")");
        }
        Serial.println("===========================\n");
    }
};

// LED Mapping Tabelle (aus deiner CSV)
const LEDController::LEDMap LEDController::ledMapping[] = {
    {"AC", 0, 1, 'A', 0}, {"AC", 1, 1, 'A', 1}, {"AC", 2, 1, 'A', 2}, {"AC", 3, 1, 'A', 3},
    {"AC", 4, 1, 'A', 4}, {"AC", 5, 1, 'A', 5}, {"AC", 6, 1, 'A', 6}, {"AC", 7, 1, 'A', 7},
    {"AC", 8, 1, 'B', 0}, {"AC", 9, 1, 'B', 1}, {"AC", 10, 1, 'B', 2}, {"AC", 11, 1, 'B', 3},
    {"AC", 12, 1, 'B', 4}, {"AC", 13, 1, 'B', 5}, {"AC", 14, 1, 'B', 6}, {"AC", 15, 1, 'B', 7},
    {"AC", 16, 3, 'A', 2}, {"AC", 17, 3, 'A', 3},
    
    {"IO", 0, 0, 'A', 0}, {"IO", 1, 0, 'A', 1}, {"IO", 2, 0, 'A', 2}, {"IO", 3, 0, 'A', 3},
    {"IO", 4, 0, 'A', 4}, {"IO", 5, 0, 'A', 5}, {"IO", 6, 0, 'A', 6}, {"IO", 7, 0, 'A', 7},
    {"IO", 8, 0, 'B', 0}, {"IO", 9, 0, 'B', 1}, {"IO", 10, 0, 'B', 2}, {"IO", 11, 0, 'B', 3},
    {"IO", 12, 0, 'B', 4}, {"IO", 13, 0, 'B', 5}, {"IO", 14, 0, 'B', 6}, {"IO", 15, 0, 'B', 7},
    {"IO", 16, 3, 'A', 4}, {"IO", 17, 3, 'A', 5},
    
    {"PC", 0, 5, 'A', 0}, {"PC", 1, 5, 'A', 1}, {"PC", 2, 5, 'A', 2}, {"PC", 3, 5, 'A', 3},
    {"PC", 4, 5, 'A', 4}, {"PC", 5, 5, 'A', 5}, {"PC", 6, 5, 'A', 6}, {"PC", 7, 5, 'A', 7},
    {"PC", 8, 5, 'B', 0}, {"PC", 9, 5, 'B', 1}, {"PC", 10, 5, 'B', 2}, {"PC", 11, 5, 'B', 3},
    {"PC", 12, 5, 'B', 4}, {"PC", 13, 5, 'B', 5}, {"PC", 14, 5, 'B', 6}, {"PC", 15, 5, 'B', 7},
    
    {"MA", 0, 4, 'A', 0}, {"MA", 1, 4, 'A', 1}, {"MA", 2, 4, 'A', 2}, {"MA", 3, 4, 'A', 3},
    {"MA", 4, 4, 'A', 4}, {"MA", 5, 4, 'A', 5}, {"MA", 6, 4, 'A', 6}, {"MA", 7, 4, 'A', 7},
    {"MA", 8, 4, 'B', 0}, {"MA", 9, 4, 'B', 1}, {"MA", 10, 4, 'B', 2}, {"MA", 11, 4, 'B', 3},
    {"MA", 12, 4, 'B', 4}, {"MA", 13, 4, 'B', 5}, {"MA", 14, 4, 'B', 6}, {"MA", 15, 4, 'B', 7},
    
    {"MB", 0, 2, 'A', 0}, {"MB", 1, 2, 'A', 1}, {"MB", 2, 2, 'A', 2}, {"MB", 3, 2, 'A', 3},
    {"MB", 4, 2, 'A', 4}, {"MB", 5, 2, 'A', 5}, {"MB", 6, 2, 'A', 6}, {"MB", 7, 2, 'A', 7},
    {"MB", 8, 2, 'B', 0}, {"MB", 9, 2, 'B', 1}, {"MB", 10, 2, 'B', 2}, {"MB", 11, 2, 'B', 3},
    {"MB", 12, 2, 'B', 4}, {"MB", 13, 2, 'B', 5}, {"MB", 14, 2, 'B', 6}, {"MB", 15, 2, 'B', 7},
    {"MB", 16, 3, 'A', 0}, {"MB", 17, 3, 'A', 1},
    
    {"INSTR", 0, 3, 'B', 0}, {"INSTR", 1, 3, 'B', 1}, {"INSTR", 2, 3, 'B', 2},
    {"INSTR", 3, 3, 'B', 3}, {"INSTR", 4, 3, 'B', 4},

    {"SW", 5, 6, 'A', 0}, {"SW", 4, 6, 'A', 1}, {"SW", 3, 6, 'A', 2},
    {"SW", 2, 6, 'A', 3}, {"SW", 1, 6, 'A', 4}, {"SW", 0, 6, 'A', 5},
    
    {"PF", 0, 6, 'B', 0}, {"PF", 1, 6, 'B', 1}, {"PF", 2, 6, 'B', 2},
    {"PF", 3, 6, 'B', 3}, {"PF", 4, 6, 'B', 4}, {"PF", 5, 6, 'B', 5},

    {"Power", 0, 7, 'A', 0}, {"Step", 1, 7, 'A', 1}, {"Run", 3, 7, 'A', 3},
    {"Overflow", 9, 7, 'B', 1}
};

const int LEDController::ledMappingSize = sizeof(LEDController::ledMapping) / sizeof(LEDController::LEDMap);

// ============================================================================
// RIM Format Loader
// ============================================================================
class RIMLoader {
public:
    static bool loadFromSD(const char* filename, uint32_t* memory, uint16_t& startPC) {
        String filepath = "/";
        filepath += filename;
        
        File file = SD.open(filepath.c_str(), FILE_READ);
        if (!file) {
            Serial.printf("Fehler: Datei '%s' nicht gefunden\n", filepath.c_str());
            return false;
        }
        
        Serial.printf("Lade RIM-Datei: %s (%d bytes)\n\n", filepath.c_str(), file.size());
        
        uint8_t bytes[3];
        int byteIndex = 0;
        bool inData = false;
        bool inRIMLoader = true;
        bool foundUserProgram = false;
        int userWordCount = 0;
        
        uint16_t loadAddr = 0;
        uint16_t endAddr = 0;
        int wordsLoaded = 0;
        
        while (file.available()) {
            uint8_t byte = file.read();
            uint8_t data = byte & 0x3F;
            
            if (!inData && data == 0) continue;
            
            if (!inData && data != 0) {
                inData = true;
                Serial.println("RIM-Loader wird geladen...\n");
            }
            
            if (inData) {
                bytes[byteIndex++] = data;
                
                if (byteIndex == 3) {
                    uint32_t word = ((uint32_t)bytes[0] << 12) | 
                                   ((uint32_t)bytes[1] << 6) | 
                                   bytes[2];
                    word &= WORD_MASK;
                    
                    if (inRIMLoader) {
                        if (word == 0607751) {
                            Serial.println("RIM-Loader komplett\n");
                            inRIMLoader = false;
                            
                            // Re-Sync: Suche Byte 32
                            while (file.available()) {
                                byte = file.read();
                                data = byte & 0x3F;
                                if (data == 032) {
                                    Serial.println("User-Programm gefunden\n");
                                    bytes[0] = data;
                                    byteIndex = 1;
                                    foundUserProgram = true;
                                    break;
                                }
                            }
                            continue;
                        }
                        
                        // RIM-Loader in Speicher laden
                        if (word & SIGN_BIT) {
                            loadAddr = word & ADDR_MASK;
                        } else {
                            if (loadAddr < MEMORY_SIZE) {
                                memory[loadAddr] = word;
                                loadAddr = (loadAddr + 1) & ADDR_MASK;
                            }
                        }
                    }
                    else if (foundUserProgram) {
                        userWordCount++;
                        
                        // Wort 1: Startadresse (ohne Bit 0 gesetzt!)
                        if (userWordCount == 1) {
                            startPC = word & ADDR_MASK;
                            loadAddr = startPC;
                            Serial.printf(">>> Programmstart: %04o <<<\n", startPC);
                        }
                        // Wort 2: End-Adresse (ohne Bit 0 gesetzt!)
                        else if (userWordCount == 2) {
                            endAddr = word & ADDR_MASK;
                            Serial.printf(">>> Programm-Ende: %04o <<<\n\n", endAddr);
                        }
                        // Ab Wort 3: Programm-Daten
                        else {
                            if (loadAddr < MEMORY_SIZE) {
                                memory[loadAddr] = word;
                                Serial.printf("  [%04o] = %06o\n", loadAddr, word);
                                wordsLoaded++;
                                loadAddr = (loadAddr + 1) & ADDR_MASK;
                            }
                        }
                    }
                    
                    byteIndex = 0;
                }
            }
        }
        
        file.close();
        
        if (endAddr > 0 && loadAddr != endAddr) {
            Serial.printf("\nWARNUNG: End-Adresse Mismatch! Erwartet: %04o, Ist: %04o\n", 
                         endAddr, loadAddr);
        } else if (endAddr > 0) {
            Serial.printf("\n✓ End-Adresse korrekt: %04o\n", endAddr);
        }
        
        Serial.printf("\n=== RIM-Laden abgeschlossen ===\n");
        Serial.printf("Worte geladen: %d\n", wordsLoaded);
        Serial.printf("Programmstart: %04o (oktal)\n\n", startPC);
        
        return wordsLoaded > 0;
    }
    
        static String getRIMFileFromFolder(uint8_t folderNumber) {
        char folderPath[16];
        sprintf(folderPath, "/%d", folderNumber);
        
        File dir = SD.open(folderPath);
        if (!dir) {
            Serial.printf("Ordner '%s' nicht gefunden\n", folderPath);
            return "";
        }
        
        if (!dir.isDirectory()) {
            Serial.printf("'%s' ist kein Ordner\n", folderPath);
            dir.close();
            return "";
        }
        
        // Suche erste .rim Datei im Ordner
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            
            if (!entry.isDirectory()) {
                String name = entry.name();
                if (name.endsWith(".rim") || name.endsWith(".RIM")) {
                    String fullPath = String(folderPath) + "/" + name;
                    entry.close();
                    dir.close();
                    return fullPath;
                }
            }
            entry.close();
        }
        
        dir.close();
        Serial.printf("Keine .rim Datei in Ordner '%s' gefunden\n", folderPath);
        return "";
    }

    static void listSDFiles() {
        Serial.println("\n=== SD-Karten Dateien (Ordner-Struktur) ===");
        
        int totalCount = 0;
        
        // Durchlaufe Ordner 0-12
        for (int folder = 0; folder <= 12; folder++) {
            char folderPath[16];
            sprintf(folderPath, "/%d", folder);
            
            File dir = SD.open(folderPath);
            if (!dir || !dir.isDirectory()) {
                continue;
            }
            
            bool foundFiles = false;
            
            while (true) {
                File entry = dir.openNextFile();
                if (!entry) break;
                
                if (!entry.isDirectory()) {
                    String name = entry.name();
                    if (name.endsWith(".rim") || name.endsWith(".RIM")) {
                        if (!foundFiles) {
                            Serial.printf("\nOrdner %d (Sense Switches: ", folder);
                            // Zeige Binär-Muster der Sense Switches
                            for (int bit = 5; bit >= 0; bit--) {
                                Serial.print((folder >> bit) & 1);
                            }
                            Serial.println("):");
                            foundFiles = true;
                        }
                        Serial.printf("  %s/%s (%d bytes)\n", 
                                     folderPath, name.c_str(), entry.size());
                        totalCount++;
                    }
                }
                entry.close();
            }
            dir.close();
        }
        
        if (totalCount == 0) {
            Serial.println("  (keine .rim Dateien gefunden)");
        } else {
            Serial.printf("\nGesamt: %d .rim Dateien\n", totalCount);
        }
        Serial.println("===========================================\n");
    }
};
// ============================================================================
// PDP-1 CPU State
// ============================================================================

class PDP1 {
private:
    uint32_t AC, IO;
    uint16_t PC;
    uint16_t MA;  // Memory Address Register (für LED-Anzeige)
    uint32_t MB;  // Memory Buffer Register (für LED-Anzeige)
    bool OV;
    bool PF[7];
    
    uint32_t memory[MEMORY_SIZE];
    
    bool running;
    bool halted;
    uint32_t cycles;
    
    String typewriter_buffer;
    LEDController* leds;
    SwitchController* switches;        // <-- Diese Zeile hinzufügen
    
    // Console mode state
    uint16_t examineAddress;           // <-- Diese Zeile hinzufügen
    bool powerOn;  
    bool showRandomLEDs;  // Flag für zufälliges LED-Muster
    bool stepModeStop;  // Flag für Step-Mode Stop

public:
    PDP1() {
        leds = nullptr;
        switches = nullptr;
        examineAddress = 0;
        powerOn = false;
        showRandomLEDs = false;  // Initialisierung
        stepModeStop = false;
        reset();
    }
    
    void attachLEDs(LEDController* ledController) {
        leds = ledController;
    }
    
    void attachSwitches(SwitchController* switchController) {
        switches = switchController;
    }
    
    void handleSwitches() {
    if (!switches) return;
    
    switches->update();
    
    // Power Switch
    if (switches->getPower() && !powerOn) {
        powerOn = true;
        Serial.println("Power ON");
        // Zufälliges LED-Muster beim Einschalten (dauerhaft bis erste Aktualisierung)
        if (leds) {
            uint32_t randomAC = random(0, 0777777);
            uint32_t randomIO = random(0, 0777777);
            uint16_t randomPC = random(0, 07777);
            uint16_t randomMA = random(0, 07777);
            uint32_t randomMB = random(0, 0777777);
            uint32_t randomInstr = random(0, 037) << 13;
            uint8_t randomPF = random(0, 64);
            
            //Serial.println("\n=== Power-On: Random LED Pattern ===");
            //Serial.printf("Random AC=%06o IO=%06o PC=%04o MA=%04o MB=%06o\n",
            //             randomAC, randomIO, randomPC, randomMA, randomMB);
            
            leds->updateDisplay(randomAC, randomIO, randomPC, randomMA, randomMB,
                              randomInstr, false, randomPF, 0, true, false, false);
            showRandomLEDs = true;  // Flag setzen

        }
        return;

    } else if (!switches->getPower() && powerOn) {
        powerOn = false;
        running = false;
        showRandomLEDs = false;
        Serial.println("Power OFF");
        reset();
        if (leds) leds->allOff();
        return;
    }
    
    if (!powerOn) return;
    
    // Single Step Mode Status
    bool singleStepMode = switches->getSingleStep();
    
    // Stop Button
    if (switches->getStopPressed()) {
        running = false;
        halted = true;
        showRandomLEDs = false;
        Serial.println("STOP pressed");
    }
    
    // Start Down
    if (switches->getStartDownPressed()) {
        if (singleStepMode) {
            step();
            Serial.printf("STEP: PC=%04o AC=%06o\n", PC, AC);
            stepModeStop = false;  // Flag zurücksetzen
        } else {
            running = true;
            halted = false;
            Serial.printf("START from PC=%04o\n", PC);
        }
    }
    
    // Start Up
    if (switches->getStartUpPressed()) {
        PC = switches->getAddressSwitches() & ADDR_MASK;
        if (singleStepMode) {
            step();
            Serial.printf("STEP from %04o: PC=%04o AC=%06o\n", 
                          switches->getAddressSwitches() & ADDR_MASK, PC, AC);
            stepModeStop = false;
        } else {
            running = true;
            halted = false;
            Serial.printf("START from Address Switches: %04o\n", PC);
        }
    }
    
    // Continue
    if (switches->getContinuePressed()) {
        if (singleStepMode) {
            if (halted) halted = false;
            step();
            Serial.printf("STEP: PC=%04o AC=%06o\n", PC, AC);
            stepModeStop = false;
        } else {
            if (halted) {
                running = true;
                halted = false;
                Serial.println("CONTINUE");
            }
        }
    }
    
    // Examine
    if (switches->getExaminePressed()) {
        showRandomLEDs = false;
        examineAddress = switches->getAddressSwitches() & ADDR_MASK;
        MA = examineAddress;
        MB = readMemory(examineAddress);
        Serial.printf("EXAMINE: Addr=%04o Data=%06o\n", examineAddress, MB);
        //updateLEDs();
    }
    
    // Deposit
    if (switches->getDepositPressed()) {
        showRandomLEDs = false;
        uint16_t addr = switches->getAddressSwitches() & ADDR_MASK;
        uint32_t data = switches->getTestWord();
        writeMemory(addr, data);
        Serial.printf("DEPOSIT: Addr=%04o Data=%06o\n", addr, data);
        //updateLEDs();
    }
    
    // Read In - lädt RIM aus Ordner basierend auf Sense Switches
    if (switches->getReadInPressed()) {
        uint8_t senseSwitches = switches->getSenseSwitches();
        uint8_t folderNumber = senseSwitches & 0x0F;  // Nur untere 4 Bits = 0-15
        
        // Begrenze auf 0-12
        if (folderNumber > 12) {
            Serial.printf("READ IN: Ungültiger Ordner %d (Sense Switches: %02o)\n", 
                         folderNumber, senseSwitches);
            return;
        }
        
        Serial.printf("\nREAD IN: Lade aus Ordner %d (Sense Switches: %02o)\n", 
                     folderNumber, senseSwitches);
        
        String rimFile = RIMLoader::getRIMFileFromFolder(folderNumber);
        
        if (rimFile.length() > 0) {
            reset();
            uint16_t startPC = 0;
            if (RIMLoader::loadFromSD(rimFile.c_str(), memory, startPC)) {
                PC = startPC;
                Serial.printf("Programm geladen und bereit bei PC=%04o\n", PC);
                updateLEDs();
                showRandomLEDs = false;
            }
        }
    }
    
    // Single Instruction (alternativer Step-Mode für einzelne Instruktionen)
    if (switches->getSingleInstrPressed() && !running) {
        showRandomLEDs = false;
        step();
        Serial.printf("SINGLE INSTR: PC=%04o AC=%06o\n", PC, AC);
    }

    if (!showRandomLEDs) {
        updateLEDs();  // Nur updaten wenn kein Zufallsmuster aktiv
    }    
    //updateLEDs();
}
    
    void reset() {
        AC = 0;
        IO = 0;
        PC = 0;
        MA = 0;
        MB = 0;
        OV = false;
        memset(PF, 0, sizeof(PF));
        memset(memory, 0, sizeof(memory));
        running = false;
        halted = false;
        cycles = 0;
        typewriter_buffer = "";
        examineAddress = 0;

    
          updateLEDs();  // Dann auf echte Werte (alle 0) zurücksetzen
    }
    
    uint32_t readMemory(uint16_t addr) {
        addr &= ADDR_MASK;
        MA = addr;
        MB = memory[addr] & WORD_MASK;
        return MB;
    }
    
    void writeMemory(uint16_t addr, uint32_t value) {
        addr &= ADDR_MASK;
        value &= WORD_MASK;
        if (value == WORD_MASK) value = 0;
        
        MA = addr;
        MB = value;
        memory[addr] = value;
    }
    
    void updateLEDs() {
        if (leds) {
            uint8_t pfBits = 0;
            for (int i = 1; i <= 6; i++) {
                if (PF[i]) pfBits |= (1 << (i-1));
            }
            
            uint32_t currentInstr = memory[PC] & WORD_MASK;
            
            // Sense Switches von Hardware lesen (falls verfügbar)
            uint8_t senseSwitches = 0;
            if (switches) {
                senseSwitches = switches->getSenseSwitches();
            }
            
            bool stepMode = switches ? switches->getSingleStep() : false;
                 //Serial.printf("LED Update: PF=%02x SW=%02x\n", pfBits, senseSwitches);   
            leds->updateDisplay(AC, IO, PC, MA, MB, currentInstr, OV, pfBits,
                              senseSwitches, powerOn, running, stepMode);  // Step-LED immer aus, wird manuell gesteuert
        }
    }
    
    int32_t onesCompToSigned(uint32_t value) {
        value &= WORD_MASK;
        if (value & SIGN_BIT) {
            return -(int32_t)(value ^ WORD_MASK);
        }
        return (int32_t)value;
    }
    
    uint32_t signedToOnesComp(int32_t value) {
        if (value < 0) {
            return ((-value) ^ WORD_MASK) & WORD_MASK;
        }
        return value & WORD_MASK;
    }
    
    uint16_t getEffectiveAddress(uint32_t instruction) {
        uint16_t Y = instruction & Y_MASK;
       uint8_t opcode = (instruction >> 13) & 037;
    bool indirect = instruction & I_BIT;
    uint16_t address = instruction & Y_MASK;
    
    Serial.printf("EXEC: PC=%04o, Instr=%06o, Op=%02o, I=%d, Addr=%04o\n",
                  PC, instruction, opcode, indirect, address);
     
        if (indirect) {
            Y = readMemory(Y) & ADDR_MASK;
        }
        
        return Y;
    }
    void executeInstruction() {
    uint32_t instruction = readMemory(PC);
    
    // PDP-1 Opcode-Extraktion: Bits 0-5 enthalten Opcode + I-bit
    uint8_t opField = (instruction >> 12) & 077;  // Bits 0-5
    bool indirect = (opField & 1);                 // Bit 5 = I-bit
    uint8_t opcode = opField & 076;                // Bits 0-4 = Opcode (ohne I-bit)
    uint16_t Y = instruction & 07777;              // Bits 6-17 = Adresse
    
    PC = (PC + 1) & ADDR_MASK;
    cycles++;
    
    // Memory Reference Instructions (00-56 oktal, gerade Zahlen)
    if (opcode <= 056 && (opcode & 1) == 0) {
        executeMemoryReference(instruction, opcode, indirect, Y);
    }
    // JMP (60)
    else if (opcode == 060) {
        if (indirect) Y = readMemory(Y) & ADDR_MASK;
        PC = Y;
    }
    // JSP (62)
    else if (opcode == 062) {
        if (indirect) Y = readMemory(Y) & ADDR_MASK;
        AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
        PC = Y;
    }
    // Skip Group (64)
    else if (opcode == 064) {
        executeSkip(instruction);
    }
    // Shift Group (66, 67)
    else if (opcode == 066 || opcode == 067) {
        executeShift(instruction);
    }
    // LAW (70, 71)
    else if (opcode == 070) {
        AC = indirect ? (Y ^ WORD_MASK) : Y;
    }
    // IOT (72)
    else if (opcode == 072) {
        executeIOT(instruction);
    }
    // Operate Group (76)
    else if (opcode == 076) {
        executeOperate(instruction);
    }
    
    updateLEDs();
}

void executeMemoryReference(uint32_t instruction, uint8_t opcode, bool indirect, uint16_t Y) {
    // Indirekte Adressierung
    if (indirect) {
        Y = readMemory(Y) & ADDR_MASK;
    }
    
    uint32_t memValue;
    int32_t result;
    
    switch(opcode) {
        case 00:  // AND
            AC &= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 02:  // IOR
            AC |= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 04:  // XOR
            AC ^= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 06:  // XCT - Execute
            {
                uint16_t savedPC = PC;
                uint32_t targetInstr = readMemory(Y);
                PC = Y;
                executeInstruction();
                PC = savedPC;
            }
            break;
            
        case 010:  // (Spare - Computer will halt)
            halted = true;
            running = false;
            Serial.println("HALT: Spare opcode 010");
            break;
            
        case 012:  // (Spare - Computer will halt)
            halted = true;
            running = false;
            Serial.println("HALT: Spare opcode 012");
            break;
            
        case 014:  // (Spare - Computer will halt)
            halted = true;
            running = false;
            Serial.println("HALT: Spare opcode 014");
            break;
            
        case 016:  // CAL - Call subroutine at 100
            writeMemory(0100, AC);
            AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
            PC = 0101;
            break;
            
        case 017:  // JDA - Jump and Deposit AC
            writeMemory(Y, AC);
            AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
            PC = (Y + 1) & ADDR_MASK;
            break;
            
        case 020:  // LAC - Load AC
            AC = readMemory(Y);
            break;
            
        case 022:  // LIO - Load IO
            IO = readMemory(Y);
            break;
            
        case 024:  // DAC - Deposit AC
            writeMemory(Y, AC);
            break;
            
        case 026:  // DAP - Deposit Address Part
            memValue = readMemory(Y);
            memValue = (memValue & 0770000) | (AC & 0007777);
            writeMemory(Y, memValue);
            break;
            
        case 030:  // DIP - Deposit Instruction Part
            memValue = readMemory(Y);
            memValue = (memValue & 0007777) | (AC & 0770000);
            writeMemory(Y, memValue);
            break;
            
        case 032:  // DIO - Deposit IO
            writeMemory(Y, IO);
            break;
            
        case 034:  // DZM - Deposit Zero in Memory
            writeMemory(Y, 0);
            break;
            
        case 036:  // (Spare - Computer will halt)
            halted = true;
            running = false;
            Serial.println("HALT: Spare opcode 036");
            break;
            
        case 040:  // ADD
            result = onesCompToSigned(AC) + onesCompToSigned(readMemory(Y));
            if (result > 0377777 || result < -0377777) OV = true;
            AC = signedToOnesComp(result);
            break;
            
        case 042:  // SUB
            result = onesCompToSigned(AC) - onesCompToSigned(readMemory(Y));
            if (result > 0377777 || result < -0377777) OV = true;
            AC = signedToOnesComp(result);
            break;
            
        case 044:  // IDX - Index
            memValue = readMemory(Y);
            memValue = (memValue + 1) & WORD_MASK;
            writeMemory(Y, memValue);
            AC = memValue;
            break;
            
        case 046:  // ISP - Index and Skip if Positive
            memValue = readMemory(Y);
            memValue = (memValue + 1) & WORD_MASK;
            writeMemory(Y, memValue);
            AC = memValue;
            if ((memValue & SIGN_BIT) == 0 && memValue != 0) {
                PC = (PC + 1) & ADDR_MASK;
            }
            break;
            
        case 050:  // SAD - Skip if AC Different
            if (AC != readMemory(Y)) {
                PC = (PC + 1) & ADDR_MASK;
            }
            break;
            
        case 052:  // SAS - Skip if AC Same
            if (AC == readMemory(Y)) {
                PC = (PC + 1) & ADDR_MASK;
            }
            break;
            
        case 054:  // MUL - Multiply
            {
                // AC * Memory → AC:IO (36-bit signed result)
                int32_t multiplicand = onesCompToSigned(AC);
                int32_t multiplier = onesCompToSigned(readMemory(Y));
                int64_t product = (int64_t)multiplicand * (int64_t)multiplier;
                
                // Vorzeichen in beiden Registern
                bool negative = (product < 0);
                if (negative) product = -product;
                
                // 36-bit Ergebnis aufteilen:
                // Bits 0-17 → IO, Bits 18-35 → AC
                IO = (product & WORD_MASK);
                AC = ((product >> 18) & WORD_MASK);
                
                // Vorzeichen setzen
                if (negative) {
                    AC |= SIGN_BIT;
                    IO |= SIGN_BIT;
                }
                
                Serial.printf("MUL: %d * %d = %lld (AC=%06o IO=%06o)\n",
                             multiplicand, multiplier, (negative ? -product : product), AC, IO);
            }
            break;
            
        case 056:  // DIV - Divide
            {
                // AC:IO / Memory → AC (quotient), IO (remainder)
                // Dividend ist in AC (high) und IO (low)
                int64_t dividend = ((int64_t)(AC & 0377777) << 18) | (IO & WORD_MASK);
                int32_t divisor = onesCompToSigned(readMemory(Y));
                
                // Vorzeichen verarbeiten
                bool dividendNeg = (AC & SIGN_BIT) != 0;
                bool divisorNeg = (divisor < 0);
                
                if (dividendNeg) dividend = -dividend;
                if (divisorNeg) divisor = -divisor;
                
                // Division durch Null prüfen
                if (divisor == 0) {
                    Serial.println("DIV: Division by zero - OVERFLOW");
                    OV = true;
                    PC = (PC + 1) & ADDR_MASK;  // Skip next instruction on overflow
                    break;
                }
                
                // Overflow prüfen: |dividend_high| >= |divisor|
                int32_t dividendHigh = (dividend >> 18) & 0377777;
                if (dividendHigh >= divisor) {
                    Serial.println("DIV: Overflow - dividend too large");
                    OV = true;
                    PC = (PC + 1) & ADDR_MASK;  // Skip next instruction
                    break;
                }
                
                // Division durchführen
                int64_t quotient = dividend / divisor;
                int64_t remainder = dividend % divisor;
                
                // Vorzeichen des Ergebnisses
                bool quotientNeg = (dividendNeg != divisorNeg);
                bool remainderNeg = dividendNeg;
                
                if (quotientNeg) quotient = -quotient;
                if (remainderNeg) remainder = -remainder;
                
                AC = signedToOnesComp(quotient);
                IO = signedToOnesComp(remainder);
                
                Serial.printf("DIV: %lld / %d = %lld remainder %lld (AC=%06o IO=%06o)\n",
                             (dividendNeg ? -dividend : dividend), 
                             (divisorNeg ? -divisor : divisor),
                             (quotientNeg ? -quotient : quotient),
                             (remainderNeg ? -remainder : remainder),
                             AC, IO);
            }
            break;

        default:
            Serial.printf("Unbekannter Memory Opcode: %02o\n", opcode);
            break;
    }
}
    
    void executeAugmented(uint32_t instruction, uint8_t opcode) {
        uint16_t Y = instruction & Y_MASK;
        bool indirect = instruction & I_BIT;
        
        if (opcode == 060) {
            AC = indirect ? (Y ^ WORD_MASK) : Y;
            return;
        }
        
        if (opcode == 064) {
            executeOperate(instruction);
            return;
        }
        
        if (opcode == 066) {
            executeSkip(instruction);
            return;
        }
        
        if (opcode == 066 || opcode == 067 || opcode == 076 || opcode == 077) {
            executeShift(instruction);
            return;
        }
    }
    
void executeOperate(uint32_t instruction) {
    // Operate Group (Opcode 76)
    // Bits werden durch Subtraktion von 760000 isoliert
    uint32_t bits = instruction & 07777;  // Bits 6-17 (ohne Opcode)
    
    if (bits & 0200) {  // CLA - Clear AC
        AC = 0;
    }
    
    if (bits & 01000) {  // CMA - Complement AC
        AC = AC ^ WORD_MASK;
    }
    
    if (bits & 04000) {  // CLI - Clear IO
        IO = 0;
    }
    
    if (bits & 0400) {  // HLT - Halt
        halted = true;
        running = false;
        Serial.println("\n*** PDP-1 HALTED ***");
        Serial.printf("Final AC=%06o IO=%06o PC=%04o Cycles=%lu\n\n", AC, IO, PC, cycles);
    }
    
    if (bits & 0100) {  // LAP - Load AC with PC
        AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
    }
    
    if (bits & 02200) {  // LAT - Load AC from Test Word
        if (switches) {
            AC = switches->getTestWord();
        }
    }
    
    // Flag operations
    int flagNum = bits & 07;
    
    if (bits & 0020) {  // CLF - Clear Flag (Bit 12)
        if (flagNum == 7) {
            memset(PF, 0, sizeof(PF));
        } else if (flagNum >= 1 && flagNum <= 6) {
            PF[flagNum] = false;
        }
    }
    
    if (bits & 0010) {  // STF - Set Flag (Bit 13)
        if (flagNum == 7) {
            memset(PF, 1, sizeof(PF));
        } else if (flagNum >= 1 && flagNum <= 6) {
            PF[flagNum] = true;
        }
    }
}
    
 void executeSkip(uint32_t instruction) {
    bool shouldSkip = false;
    bool invert = instruction & I_BIT;  // Bit 5
    uint32_t bits = instruction & 07777;  // Bits 6-17
    
    if (bits & 0100) {  // SZA - Skip if AC = 0
        shouldSkip |= (AC == 0);
    }
    
    if (bits & 0200) {  // SPA - Skip if AC >= 0 (plus)
        shouldSkip |= ((AC & SIGN_BIT) == 0);
    }
    
    if (bits & 0400) {  // SMA - Skip if AC < 0 (minus)
        shouldSkip |= (AC & SIGN_BIT);
    }
    
    if (bits & 02000) {  // SPI - Skip if IO >= 0 (plus)
        shouldSkip |= ((IO & SIGN_BIT) == 0);
    }
    
    if (bits & 01000) {  // SZO - Skip if overflow = 0 and clear OV
        shouldSkip |= !OV;
        OV = false;
    }
    
    if (bits & 0007) {  // SZF - Skip if program flag = 0
        int flagNum = bits & 07;
        if (flagNum == 7) {
            // Skip if all flags are 0
            bool allClear = true;
            for (int i = 1; i <= 6; i++) {
                if (PF[i]) allClear = false;
            }
            shouldSkip |= allClear;
        } else if (flagNum >= 1 && flagNum <= 6) {
            shouldSkip |= !PF[flagNum];
        }
    }
    
    if (bits & 0070) {  // SZS - Skip if sense switch = 0
        int switchNum = (bits >> 3) & 07;
        if (switches) {
            uint8_t senseSw = switches->getSenseSwitches();
            if (switchNum == 7) {
                shouldSkip |= (senseSw == 0);
            } else if (switchNum >= 1 && switchNum <= 6) {
                shouldSkip |= ((senseSw >> (switchNum - 1)) & 1) == 0;
            }
        }
    }
    
    if (invert) shouldSkip = !shouldSkip;
    if (shouldSkip) PC = (PC + 1) & ADDR_MASK;
}
    
    void executeShift(uint32_t instruction) {
    int count = 0;
    for (int i = 0; i < 9; i++) {
        if (instruction & (1 << i)) count++;
    }
    
    uint8_t opField = (instruction >> 12) & 077;  // Bits 0-5
    uint8_t baseOp = opField & 076;               // Ohne I-bit
    
    // 66x = left, 67x = right (Bit 0 des Opcodes!)
    bool left = (baseOp == 066);                  // 66 = left operations
    bool arith = (baseOp == 067 || baseOp == 065); // 67/65 = arithmetic
    uint8_t reg = (instruction >> 9) & 03;        // Bits 7-8
    
    //Serial.printf("SHIFT: instr=%06o, baseOp=%02o, count=%d, left=%d, arith=%d, reg=%d, AC=%06o, IO=%06o\n",instruction, baseOp, count, left, arith, reg, AC, IO);
    
    for (int i = 0; i < count; i++) {
        if (left) {
            if (reg == 1) {
                bool msb = AC & SIGN_BIT;
                AC = (AC << 1) & WORD_MASK;
                if (!arith) AC |= (msb ? 1 : 0);
            } else if (reg == 2) {
                bool msb = IO & SIGN_BIT;
                IO = (IO << 1) & WORD_MASK;
                if (!arith) IO |= (msb ? 1 : 0);
            } else if (reg == 3) {
                bool ac_msb = AC & SIGN_BIT;
                bool io_msb = IO & SIGN_BIT;
                AC = (AC << 1) & WORD_MASK;
                AC |= (io_msb ? 1 : 0);
                IO = (IO << 1) & WORD_MASK;
                IO |= (ac_msb ? 1 : 0);
            }
        } else {
            if (reg == 1) {
                bool lsb = AC & 1;
                AC = AC >> 1;
                if (!arith) AC |= (lsb ? SIGN_BIT : 0);
            } else if (reg == 2) {
                bool lsb = IO & 1;
                IO = IO >> 1;
                if (!arith) IO |= (lsb ? SIGN_BIT : 0);
            } else if (reg == 3) {
                bool ac_lsb = AC & 1;
                bool io_lsb = IO & 1;
                IO = IO >> 1;
                IO |= (ac_lsb ? SIGN_BIT : 0);
                AC = AC >> 1;
                AC |= (io_lsb ? SIGN_BIT : 0);
            }
        }
    }
    
    //Serial.printf("  -> After shift: AC=%06o, IO=%06o\n", AC, IO);
}
    
    void executeIOT(uint32_t instruction) {
        uint8_t device = instruction & 077;
        
        switch(device) {
            case 003:
                {
                    uint8_t fiodec = IO & 077;  // Untere 6 Bits
                    //Serial.printf("[TYO: IO=%06o, fiodec=%03o=%d]", IO, fiodec, fiodec);
                    char ch = fiodecToAscii(fiodec);
                    Serial.print(ch);
                    typewriter_buffer += ch;
                }
                break;
            case 004:
                if (Serial.available()) {
                    char ch = Serial.read();
                    IO = asciiToFiodec(ch) << 12;
                    PF[1] = true;
                }
                break;
        }
    }
    
    char fiodecToAscii(uint8_t fiodec) {
    fiodec &= 63;  // 6 Bits
    
    // Buchstaben a-z
    if (fiodec >= 49 && fiodec <= 57) return 'a' + (fiodec - 49);
    if (fiodec >= 33 && fiodec <= 41) return 'j' + (fiodec - 33);
    if (fiodec >= 18 && fiodec <= 25) return 's' + (fiodec - 18);
    
    switch(fiodec) {
        case 0:   return ' ';
        case 1:   return '1';
        case 2:   return '2';
        case 3:   return '3';
        case 4:   return '4';
        case 5:   return '5';
        case 6:   return '6';
        case 7:   return '7';
        case 8:   return '8';
        case 9:   return '9';
        case 16:  return '0';     // 20 oktal
        case 17:  return '/';     // 21 oktal
        case 27:  return ',';
        case 30:  return ',';
        case 32:  return '_';
        case 44:  return '+';
        case 45:  return ']';
        case 47:  return ')';
        case 59:  return '(';
        case 61:  return '\b';
        case 63:  return '\n';
        default:  return '?';
    }
}

uint8_t asciiToFiodec(char ch) {
    // Buchstaben
    if (ch >= 'a' && ch <= 'i') return 49 + (ch - 'a');
    if (ch >= 'A' && ch <= 'I') return 49 + (ch - 'A');
    if (ch >= 'j' && ch <= 'r') return 33 + (ch - 'j');
    if (ch >= 'J' && ch <= 'R') return 33 + (ch - 'J');
    if (ch >= 's' && ch <= 'z') return 18 + (ch - 's');
    if (ch >= 'S' && ch <= 'Z') return 18 + (ch - 'S');
    
    // Ziffern
    if (ch == '0') return 16;  // 20 oktal
    if (ch >= '1' && ch <= '9') return (ch - '0');
    
    // Sonderzeichen
    switch(ch) {
        case ' ':  return 0;
        case '/':  return 17;
        case '=':  return 27;
        case ',':  return 30;
        case '+':  return 44;
        case '-':  return 44;
        case ')':  return 47;
        case '(':  return 59;
        case '\b': return 61;
        case '\r':
        case '\n': return 63;
        default:   return 0;
    }
}

    bool loadRIM(const char* filename) {
        reset();
        uint16_t startPC = 0;
        bool success = RIMLoader::loadFromSD(filename, memory, startPC);
        if (success) {
            PC = startPC;
            updateLEDs();
        }
        return success;
    }
    
    void run() {
        running = true;
        halted = false;
        Serial.println("PDP-1 Running...");
        updateLEDs();
    }
    
    void step() {
        if (!halted) {
            executeInstruction();
        }
        // Prüfe STOP-Schalter direkt nach jeder Instruktion
        if (switches && switches->getStop()) {
            running = false;
            halted = true;
            Serial.println("\nSTOP während Ausführung");
        }
              
        // Im Step-Mode nach JEDER Instruktion stoppen
        if (switches && switches->getSingleStep()) {
            running = false;
            stepModeStop = true;
        }  


    }
    
    bool isRunning() {
        return running && !halted;
    }
    
    void printStatus() {
        Serial.printf("PC=%04o AC=%06o IO=%06o OV=%d Cycles=%lu\n",
                     PC, AC, IO, OV, cycles);
    }
    
    void dumpMemory(uint16_t start, uint16_t end) {
        for (uint16_t addr = start; addr <= end; addr++) {
            if ((addr & 07) == 0) {
                Serial.printf("\n%04o: ", addr);
            }
            Serial.printf("%06o ", readMemory(addr));
        }
        Serial.println();
    }
    
    void loadLEDTestProgram() {
        // LED Test Programm: Füllt und leert AC/IO bitweise
        const uint32_t ledTest[] = {
            // Start: 400
            0600000,  // 400: CLA - AC löschen
            0601000,  // 401: CLI - IO löschen
            
            // Fülle AC bitweise
            0640001,  // 402: LAW 1 - Lade 1
            0260450,  // 403: DAC 450 (bit) - Speichere
            
            0050450,  // 404: LAC 450 (fill1) - Lade Bit-Muster
            0671001,  // 405: RCL 1S - Rotiere 1 Bit links
            0260450,  // 406: DAC 450 - Speichere
            
            0050450,  // 407: LAC 450 - Lade für Anzeige
            0340435,  // 410: JSP 435 (delay) - Verzögerung
            
            0050450,  // 411: LAC 450 - Lade Bit-Muster
            0150452,  // 412: SUB 452 (allone) - Vergleich mit 777777
            0640100,  // 413: SZA - Skip wenn gleich
            0000404,  // 414: JMP 404 (fill1) - Weiter füllen
            
            // Fülle IO bitweise
            0640001,  // 415: LAW 1 - Lade 1
            0260450,  // 416: DAC 450 (bit) - Speichere
            
            0050450,  // 417: LAC 450 (fill2) - Lade Bit-Muster
            0671001,  // 420: RCL 1S - Rotiere 1 Bit links
            0260450,  // 421: DAC 450 - Speichere
            
            0160450,  // 422: LIO 450 - Lade in IO für Anzeige
            0340435,  // 423: JSP 435 (delay) - Verzögerung
            
            0050450,  // 424: LAC 450 - Lade Bit-Muster
            0150452,  // 425: SUB 452 (allone) - Vergleich
            0640100,  // 426: SZA - Skip wenn gleich
            0000417,  // 427: JMP 417 (fill2) - Weiter füllen
            
            // Beide Register voll - Halte Zustand
            0777777,  // 430: LAW 777777 - Alle Bits in AC
            0160452,  // 431: LIO 452 (allone) - Alle Bits in IO
            0340435,  // 432: JSP 435 (delay)
            0340435,  // 433: JSP 435 (delay)
            0340435,  // 434: JSP 435 (delay)
            
            // Delay Subroutine (435-442)
            0260435,  // 435: DAC 435 (delay) - Speichere Return
            0650000,  // 436: LAW 10000 - Lade Zähler (oktal)
            0260451,  // 437: DAC 451 (count) - Speichere
            0040451,  // 440: ISP 451 (dloop) - Inkrement, skip wenn positiv
            0000440,  // 441: JMP 440 (dloop) - Loop
            0010435,  // 442: JMP I 435 (delay) - Return
            
            // Leere AC bitweise
            0777777,  // 443: LAW 777777 - Alle Bits
            0260450,  // 444: DAC 450 (bit) - Speichere
            
            0050450,  // 445: LAC 450 (empty1) - Lade Bit-Muster
            0661001,  // 446: RCR 1S - Rotiere 1 Bit rechts
            0260450,  // 447: DAC 450 - Speichere
            
            0050450,  // 450: LAC 450 - Lade für Anzeige
            0340435,  // 451: JSP 435 (delay) - Verzögerung
            
            0050450,  // 452: LAC 450 - Lade Bit-Muster
            0640100,  // 453: SZA - Skip wenn Null
            0000445,  // 454: JMP 445 (empty1) - Weiter leeren
            
            // Leere IO bitweise
            0777777,  // 455: LAW 777777 - Alle Bits
            0260450,  // 456: DAC 450 (bit) - Speichere
            
            0050450,  // 457: LAC 450 (empty2) - Lade Bit-Muster
            0661001,  // 460: RCR 1S - Rotiere 1 Bit rechts
            0260450,  // 461: DAC 450 - Speichere
            
            0160450,  // 462: LIO 450 - Lade in IO für Anzeige
            0340435,  // 463: JSP 435 (delay) - Verzögerung
            
            0050450,  // 464: LAC 450 - Lade Bit-Muster
            0640100,  // 465: SZA - Skip wenn Null
            0000457,  // 466: JMP 457 (empty2) - Weiter leeren
            
            // Beide Register leer - Halte Zustand
            0600000,  // 467: CLA - AC löschen
            0601000,  // 470: CLI - IO löschen
            0340435,  // 471: JSP 435 (delay)
            0340435,  // 472: JSP 435 (delay)
            0340435,  // 473: JSP 435 (delay)
            
            // Wiederhole von vorn
            0000400,  // 474: JMP 400 (start) - Endlosschleife
            
            // Daten (ab 450)
            0000000,  // 450: bit - Bit-Muster Variable
            0000000,  // 451: count - Delay-Zähler
            0777777   // 452: allone - Alle 18 Bits gesetzt
        };
        
        reset();
        
        // Lade Programm ab Adresse 0400 (oktal)
        for (size_t i = 0; i < sizeof(ledTest)/sizeof(ledTest[0]); i++) {
            writeMemory(0400 + i, ledTest[i]);
        }
        
        PC = 0400;  // Setze PC auf Startadresse
        updateLEDs();
        
        Serial.println("\n*** LED Test Programm geladen ***");
        Serial.println("Programm startet bei Adresse 0400");
        Serial.println("Funktion: Füllt AC/IO bitweise, dann leert sie");
        Serial.println("Benutze 'r' zum Starten oder START-Schalter\n");
    }
};

// ============================================================================
// ESP32 Main
// ============================================================================

PDP1 cpu;
LEDController leds(&SPI, MCP_CS_PIN);
SwitchController switches;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("VERSION: 2024-DEBUG-BITREVERSE");

    Serial.println("\n============================================");
    Serial.println("PDP-1 Simulator für ESP32");
    Serial.println("mit LED-Anzeige, Schaltern und SD-Karte");
    Serial.println("============================================\n");
    
    // I2C initialisieren (für Schalter)
    Serial.println("Initialisiere I2C Bus (Schalter)...");
    switches.begin();
    cpu.attachSwitches(&switches);
    
    // SPI initialisieren
    SPI.begin();
    SPI.setFrequency(1000000);
    
    // LED Controller initialisieren
    Serial.println("Initialisiere LED Controller...");
    leds.begin();
    cpu.attachLEDs(&leds);
    
    // LED Test
    /*
    Serial.println("Starte LED Test...");
    leds.testPattern();
    delay(1000);
    leds.allOff();
    */
    // SD-Karte initialisieren
    Serial.println("\nInitialisiere SD-Karte...");
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("WARNUNG: SD-Karte nicht gefunden!");
        Serial.println("Prüfe Verkabelung:");
        Serial.println("  CS   -> GPIO 5");
        Serial.println("  MOSI -> GPIO 23");
        Serial.println("  MISO -> GPIO 19");
        Serial.println("  SCK  -> GPIO 18");
        Serial.println("\nSimulator läuft ohne SD-Karte.\n");
    } else {
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD-Karte gefunden: %llu MB\n", cardSize);
        RIMLoader::listSDFiles();
    }
    
    printHelp();
    
    Serial.println("\n*** Hardware-Schalter sind aktiv ***");
    Serial.println("Benutze die physischen Schalter für:");
    Serial.println("  - Power ON/OFF");
    Serial.println("  - Start/Stop");
    Serial.println("  - Examine/Deposit");
    Serial.println("  - Single Step\n");
}

void printHelp() {
    Serial.println("\n=== Serial Kommandos ===");
    Serial.println("l <datei.rim> - Lade RIM-Datei von SD-Karte");
    Serial.println("f             - Zeige Dateien auf SD-Karte");
    Serial.println("m             - Lade LED Test Programm");
    Serial.println("r             - Run (kontinuierlich)");
    Serial.println("s             - Single Step");
    Serial.println("d <start>     - Memory Dump ab Adresse (oktal)");
    Serial.println("p             - Status ausgeben");
    Serial.println("w             - Schalter-Status anzeigen");
    Serial.println("t             - LED Test Pattern");
    Serial.println("o             - Alle LEDs ausschalten");
    Serial.println("x             - CPU Reset");
    Serial.println("h             - Diese Hilfe");
    Serial.println("========================\n");
}

void loop() {
    // Hardware-Schalter kontinuierlich abfragen
    cpu.handleSwitches();
    
    // Serial Kommandos verarbeiten
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input.length() == 0) return;
        
        char cmd = input.charAt(0);
        
        switch(cmd) {
            case 'l':
            case 'L':
                {
                    int spacePos = input.indexOf(' ');
                    if (spacePos > 0) {
                        String filename = input.substring(spacePos + 1);
                        filename.trim();
                        Serial.printf("\nLade: %s\n", filename.c_str());
                        if (cpu.loadRIM(filename.c_str())) {
                            Serial.println("RIM-Datei erfolgreich geladen!");
                            cpu.printStatus();
                        } else {
                            Serial.println("Fehler beim Laden der RIM-Datei!");
                        }
                    } else {
                        Serial.println("Verwendung: l <dateiname.rim>");
                    }
                }
                break;
                
            case 'f':
            case 'F':
                RIMLoader::listSDFiles();
                break;
                
            case 'm':
            case 'M':
                cpu.loadLEDTestProgram();
                break;
                
            case 'r':
            case 'R':
                cpu.run();
                break;
                
            case 's':
            case 'S':
                cpu.step();
                cpu.printStatus();
                break;
                
            case 'd':
            case 'D':
                {
                    int spacePos = input.indexOf(' ');
                    uint16_t start = 0;
                    if (spacePos > 0) {
                        String addrStr = input.substring(spacePos + 1);
                        start = strtol(addrStr.c_str(), NULL, 8);
                    }
                    Serial.printf("\nMemory Dump ab %04o:\n", start);
                    cpu.dumpMemory(start, start + 077);
                    Serial.println();
                }
                break;
                
            case 'p':
            case 'P':
                cpu.printStatus();
                Serial.printf("Power State: %s\n", switches.getPower() ? "ON" : "OFF");
                Serial.printf("Sense Switches: 0x%02X\n", switches.getSenseSwitches());
                break;
                
            case 'w':
            case 'W':
                switches.printStatus();
                break;
                
            case 't':
            case 'T':
                Serial.println("Starte LED Test Pattern...");
                leds.testPattern();
                Serial.println("LED Test abgeschlossen");
                break;
                
            case 'o':
            case 'O':
                leds.allOff();
                Serial.println("Alle LEDs ausgeschaltet");
                delay(500);
                cpu.updateLEDs();
                Serial.println("LED-Status von CPU wiederhergestellt");
                leds.dumpPortCache();
                break;
                
            case 'x':
            case 'X':
                cpu.reset();
                Serial.println("CPU Reset");
                break;
                
            case 'h':
            case 'H':
                printHelp();
                break;
                
            default:
                Serial.println("Unbekanntes Kommando. 'h' für Hilfe.");
                break;
        }
    }
    
    // CPU Execution
    if (cpu.isRunning()) {
        // Prüfe ob Step-Mode aktiv ist
        bool stepMode = switches.getSingleStep();
        
        if (stepMode) {
            // Step-Mode: nur eine Instruktion
            cpu.step();
        } else {
            // Normal-Mode: Batch-Ausführung für Geschwindigkeit
            for (int i = 0; i < 1000; i++) {
                cpu.step();
                if (!cpu.isRunning()) break;
            }
        }
    }
    
    yield();
}
