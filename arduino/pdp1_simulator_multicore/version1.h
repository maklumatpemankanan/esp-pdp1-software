#ifndef VERSION1_H
#define VERSION1_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "cpu.h"

// Hardware Configuration (falls nicht im Main definiert)
#ifndef MCP_CS_PIN
#define MCP_CS_PIN 17
#endif

// I2C Configuration
#define I2C_SDA 21
#define I2C_SCL 22
#define PCF_INT_PIN 27

// TCA9548A I2C Multiplexer
#define TCA9548A_ADDR 0x70
#define TCA9548A_CHANNEL 1

// PCF8574 I2C Addresses
#define PCF_ADDR_0x20 0x20
#define PCF_ADDR_0x21 0x21
#define PCF_ADDR_0x22 0x22
#define PCF_ADDR_0x23 0x23
#define PCF_ADDR_0x24 0x24
#define PCF_ADDR_0x25 0x25
#define PCF_ADDR_0x26 0x26

// Global interrupt flag
volatile bool DRAM_ATTR g_switchInterruptFlag = false;

void IRAM_ATTR switchISR() {
    g_switchInterruptFlag = true;
}

// PCF8574 Switch Controller
class SwitchControllerV1 : public ISwitchController {
private:
    uint8_t switchState[7];
    uint8_t lastSwitchState[7];
    unsigned long lastDebounceTime[7][8];
    bool debouncedState[7][8];
    const unsigned long debounceDelay = 50;
    
    void selectTCAChannel(uint8_t channel) {
        if (channel > 7) return;
        Wire.beginTransmission(TCA9548A_ADDR);
        Wire.write(1 << channel);
        Wire.endTransmission();
    }
    
    uint8_t readPCF8574(uint8_t address) {
        selectTCAChannel(TCA9548A_CHANNEL);
        Wire.requestFrom(address, (uint8_t)1);
        if (Wire.available()) {
            return Wire.read();
        }
        return 0xFF;
    }
    
public:
    SwitchControllerV1() {
        memset(switchState, 0xFF, sizeof(switchState));
        memset(lastSwitchState, 0xFF, sizeof(lastSwitchState));
        memset(lastDebounceTime, 0, sizeof(lastDebounceTime));
        memset(debouncedState, 1, sizeof(debouncedState));
        g_switchInterruptFlag = false;
    }
    
    void begin() override {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(100000);
        
        selectTCAChannel(TCA9548A_CHANNEL);
        
        pinMode(PCF_INT_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PCF_INT_PIN), switchISR, FALLING);
        
        Serial.println("Switch Controller V1 initialised (7x PCF8574)");
        Serial.printf("TCA9548A Channel %d activated\n", TCA9548A_CHANNEL);
        
        for (uint8_t addr = 0x20; addr <= 0x26; addr++) {
            selectTCAChannel(TCA9548A_CHANNEL);
            Wire.beginTransmission(addr);
            Wire.write(0xFF);
            uint8_t error = Wire.endTransmission();
            if (error != 0) {
                Serial.printf("WARNING: PCF8574 on Address 0x%02X no Response\n", addr);
            } else {
                Serial.printf("PCF8574 on Address 0x%02X initialised\n", addr);
            }
        }
        
        update();
        g_switchInterruptFlag = false;
    }
    
    void update() override {
        unsigned long currentTime = millis();
        
        // Control Switches immer lesen
        for (uint8_t i = 5; i <= 6; i++) {
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
        
        // if (g_switchInterruptFlag) {
        //     g_switchInterruptFlag = false;
        //     //stop switch überprüfen 
        //     if(isPressed(0x25,3)){
        //         //cpu stop
        //         Serial.println("Stop interrupt");
        //         cpu.stop();
        //     }
        // }
        
        // Periodisches Lesen aller anderen Chips
        static unsigned long lastFullUpdate = 0;
        if (currentTime - lastFullUpdate > 100) {
            lastFullUpdate = currentTime;
            
            for (uint8_t i = 0; i < 5; i++) {
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
    
    uint16_t getAddressSwitches() override {
        uint16_t addr = 0;
        
        addr |= ((switchState[1] >> 7) & 1) << 0;
        addr |= ((switchState[1] >> 6) & 1) << 1;
        addr |= ((switchState[1] >> 5) & 1) << 2;
        addr |= ((switchState[1] >> 4) & 1) << 3;
        addr |= ((switchState[1] >> 3) & 1) << 4;
        addr |= ((switchState[1] >> 2) & 1) << 5;
        addr |= ((switchState[1] >> 1) & 1) << 6;
        addr |= ((switchState[1] >> 0) & 1) << 7;
        
        addr |= ((switchState[0] >> 7) & 1) << 8;
        addr |= ((switchState[0] >> 6) & 1) << 9;
        addr |= ((switchState[0] >> 5) & 1) << 10;
        addr |= ((switchState[0] >> 4) & 1) << 11;
        addr |= ((switchState[0] >> 3) & 1) << 12;
        addr |= ((switchState[0] >> 2) & 1) << 13;
        addr |= ((switchState[0] >> 1) & 1) << 14;
        addr |= ((switchState[0] >> 0) & 1) << 15;
        
        return addr;
    }
    
    uint32_t getTestWord() override {
        uint32_t tw = 0;
        
        tw |= ((switchState[2] >> 0) & 1) << 17;
        tw |= ((switchState[2] >> 1) & 1) << 16;
        tw |= ((switchState[2] >> 2) & 1) << 15;
        tw |= ((switchState[2] >> 3) & 1) << 14;
        tw |= ((switchState[2] >> 4) & 1) << 13;
        tw |= ((switchState[2] >> 5) & 1) << 12;
        tw |= ((switchState[2] >> 6) & 1) << 11;
        tw |= ((switchState[2] >> 7) & 1) << 10;
        
        tw |= ((switchState[3] >> 0) & 1) << 9;
        tw |= ((switchState[3] >> 1) & 1) << 8;
        tw |= ((switchState[3] >> 2) & 1) << 7;
        tw |= ((switchState[3] >> 3) & 1) << 6;
        tw |= ((switchState[3] >> 4) & 1) << 5;
        tw |= ((switchState[3] >> 5) & 1) << 4;
        tw |= ((switchState[3] >> 6) & 1) << 3;
        tw |= ((switchState[3] >> 7) & 1) << 2;
        
        tw |= ((switchState[4] >> 1) & 1) << 1;
        tw |= ((switchState[4] >> 0) & 1) << 0;
        
        return tw & WORD_MASK;
    }
    
    uint8_t getSenseSwitches() override {
        uint8_t sw = 0;
        
        // sw |= ((switchState[4] >> 7) & 1) << 0;
        // sw |= ((switchState[4] >> 6) & 1) << 1;
        // sw |= ((switchState[4] >> 5) & 1) << 2;
        // sw |= ((switchState[4] >> 4) & 1) << 3;
        // sw |= ((switchState[4] >> 3) & 1) << 4;
        // sw |= ((switchState[4] >> 2) & 1) << 5;

        sw |= ((switchState[4] >> 2) & 1) << 0;
        sw |= ((switchState[4] >> 3) & 1) << 1;
        sw |= ((switchState[4] >> 4) & 1) << 2;
        sw |= ((switchState[4] >> 5) & 1) << 3;
        sw |= ((switchState[4] >> 6) & 1) << 4;
        sw |= ((switchState[4] >> 7) & 1) << 5;


        return sw;
    }
    
    bool getExtendSwitch() override  { return isPressed(0x25, 0); }
    bool getStartDown() override     { return isPressed(0x25, 1); }
    bool getStartUp() override       { return isPressed(0x25, 2); }
    bool getStop() override          { return isPressed(0x25, 3); }
    bool getContinue() override      { return isPressed(0x25, 4); }
    bool getExamine() override       { return isPressed(0x25, 5); }
    bool getDeposit() override       { return isPressed(0x25, 6); }
    bool getReadIn() override        { return isPressed(0x25, 7); }
    bool getPower() override         { return isPressed(0x26, 0); }
    bool getSingleStep() override    { return isPressed(0x26, 1); }
    bool getSingleInstr() override   { return isPressed(0x26, 2); }
    
    bool getStartDownPressed() override   { return wasPressed(0x25, 1); }
    bool getStartUpPressed() override     { return wasPressed(0x25, 2); }
    bool getStopPressed() override        { return wasPressed(0x25, 3); }
    bool getContinuePressed() override    { return wasPressed(0x25, 4); }
    bool getExaminePressed() override     { return wasPressed(0x25, 5); }
    bool getDepositPressed() override     { return wasPressed(0x25, 6); }
    bool getReadInPressed() override      { return wasPressed(0x25, 7); }
    bool getSingleStepPressed() override  { return wasPressed(0x26, 1); }
    bool getSingleInstrPressed() override { return wasPressed(0x26, 2); }
    
    void printStatus() override {
        Serial.println("\n=== Switch Status ===");
        Serial.printf("Address Switches: %04o (oktal) = %d (dezimal)\n", 
                     getAddressSwitches(), getAddressSwitches());
        Serial.printf("Test Word: %06o (oktal)\n", getTestWord());
        Serial.printf("Sense Switches: %02o (oktal)\n", getSenseSwitches());
        Serial.println("=====================\n");
    }
};

// MCP23S17 LED Controller - AKTUALISIERT für eigene Bibliothek
class LEDControllerV1 : public ILEDController {
private:
    MCP23S17* mcpChips[8];  // Array von MCP23S17-Objekten
    uint8_t csPin;
    
    struct LEDMap {
        const char* reg;
        uint8_t bit;
        uint8_t chip;
        char port;
        uint8_t pin;
    };
    
    static const LEDMap ledMapping[];
    static const int ledMappingSize;
    
    void setLED(uint8_t chip, char port, uint8_t pin, bool state) {
        if (chip >= 8) return;
        
        // Pin-Nummer berechnen: Port A = 0-7, Port B = 8-15
        uint8_t mcpPin = (port == 'B') ? (pin + 8) : pin;
        
        // digitalWrite der Bibliothek verwenden
        mcpChips[chip]->digitalWrite(mcpPin, state ? HIGH : LOW);
    }
    
public:
    LEDControllerV1(SPIClass* spiInstance, uint8_t cs) : csPin(cs) {
        // MCP23S17-Objekte erstellen (ein Objekt pro Chip-Adresse)
        for (uint8_t i = 0; i < 8; i++) {
            mcpChips[i] = new MCP23S17(cs, i, 1000000);  // 1 MHz SPI
        }
    }
    
    ~LEDControllerV1() {
        // Speicher freigeben
        for (uint8_t i = 0; i < 8; i++) {
            delete mcpChips[i];
        }
    }
    
    void begin() override {
        // Alle MCP23S17 Chips initialisieren
        for (uint8_t chip = 0; chip < 8; chip++) {
            if (!mcpChips[chip]->begin()) {
                Serial.printf("Warning: MCP23S17 Chip %d can't be initialised!\n", chip);
            }
            
            // Alle Pins als Ausgänge konfigurieren (Port A und Port B)
            for (uint8_t pin = 0; pin < 16; pin++) {
                mcpChips[chip]->pinMode(pin, MCP23S17_OUTPUT);
            }
            
            // Alle Ausgänge auf LOW setzen
            mcpChips[chip]->writeGPIO(0x0000);
        }
        
        Serial.println("LED Controller V1 initialised (8x MCP23S17)");
    }
    
    void updateDisplay(uint32_t ac, uint32_t io, uint16_t pc, uint16_t ma, 
                      uint32_t mb, uint32_t instr, bool ov, uint8_t pf,
                      uint8_t senseSw, bool power, bool run, bool step) override {
        
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
    
    void allOff() override {
        for (uint8_t chip = 0; chip < 8; chip++) {
            mcpChips[chip]->writeGPIO(0x0000);
        }
    }
    
    void testPattern() override {
        Serial.println("LED Test Pattern V1...");
        
        for (uint8_t chip = 0; chip < 8; chip++) {
            Serial.printf("Testing Chip %d\n", chip);
            
            // Port A (Pins 0-7)
            for (uint8_t pin = 0; pin < 8; pin++) {
                setLED(chip, 'A', pin, true);
                delay(50);
                setLED(chip, 'A', pin, false);
            }
            
            // Port B (Pins 8-15 in der Bibliothek)
            for (uint8_t pin = 0; pin < 8; pin++) {
                setLED(chip, 'B', pin, true);
                delay(50);
                setLED(chip, 'B', pin, false);
            }
        }
        
        Serial.println("LED Test abgeschlossen");
    }
    
    void showRandomPattern() override {
        // Version 1: Zufallsmuster wird über updateDisplay() mit Zufallswerten angezeigt
        // Diese Methode muss nichts tun - das Zufallsmuster wird von der CPU generiert
    }
};

// Static member definition - AUSSERHALB der Klasse
const LEDControllerV1::LEDMap LEDControllerV1::ledMapping[] = {
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

    // {"SW", 5, 6, 'A', 0}, {"SW", 4, 6, 'A', 1}, {"SW", 3, 6, 'A', 2},
    // {"SW", 2, 6, 'A', 3}, {"SW", 1, 6, 'A', 4}, {"SW", 0, 6, 'A', 5},

    {"SW", 0, 6, 'A', 0}, {"SW", 1, 6, 'A', 1}, {"SW", 2, 6, 'A', 2},
    {"SW", 3, 6, 'A', 3}, {"SW", 4, 6, 'A', 4}, {"SW", 5, 6, 'A', 5},


    // {"PF", 0, 6, 'B', 0}, {"PF", 1, 6, 'B', 1}, {"PF", 2, 6, 'B', 2},
    // {"PF", 3, 6, 'B', 3}, {"PF", 4, 6, 'B', 4}, {"PF", 5, 6, 'B', 5},

    {"PF", 0, 6, 'B', 0}, {"PF", 1, 6, 'B', 1}, {"PF", 2, 6, 'B', 2},
    {"PF", 3, 6, 'B', 3}, {"PF", 4, 6, 'B', 4}, {"PF", 5, 6, 'B', 5},


    {"Power", 0, 7, 'A', 0}, {"Step", 1, 7, 'A', 1}, {"Run", 3, 7, 'A', 3},
    {"Overflow", 9, 7, 'B', 1}
};

const int LEDControllerV1::ledMappingSize = sizeof(LEDControllerV1::ledMapping) / sizeof(LEDControllerV1::LEDMap);

#endif // VERSION1_H
