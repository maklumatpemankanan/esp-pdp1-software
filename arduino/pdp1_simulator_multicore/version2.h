#ifndef VERSION2_H
#define VERSION2_H

#include <Arduino.h>
#include <SPI.h>
#include <map>
#include <string>
#include "cpu.h"

// Hardware Configuration (falls nicht im Main definiert)
#ifndef MCP_CS_PIN
#define MCP_CS_PIN 17
#endif

#define MCP_ADDR     0x20
#define MCP_COL_LOW  0x21

#define LED_ROWS  7
#define SW_ROWS   3
#define COLS      18

// MCP23S17 Instanzen als globale Objekte
MCP23S17 mcpAddr(MCP_CS_PIN, 0x00, 1000000);    // Decoder + COL16-17
MCP23S17 mcpColLow(MCP_CS_PIN, 0x01, 1000000);  // COL0-15

void setDecoderAddress(uint8_t addr, bool ledEnable, bool swEnable) {
    uint8_t addrBits = addr & 0x07;
    
    if (ledEnable) {
        addrBits &= ~0x08;
    }
    if (swEnable) {
        addrBits |= 0x08;
    }
    
    // GPIOA von MCP_ADDR schreiben (Pins 0-7 entsprechen Port A)
    mcpAddr.writePort(MCP23S17_PORTA, addrBits);
}

void configureCOLsAsOutputs() {
    // MCP_COL_LOW: Port A und Port B als Ausgänge
    mcpColLow.portMode(MCP23S17_PORTA, 0xFF);  // 0xFF = alle Pins als Ausgang
    mcpColLow.portMode(MCP23S17_PORTB, 0xFF);
    
    // MCP_ADDR: Port B als Ausgang (COL16-17)
    mcpAddr.portMode(MCP23S17_PORTB, 0xFF);
}

void configureCOLsAsInputs() {
    // MCP_COL_LOW: Port A und Port B als Eingänge mit Pull-ups
    mcpColLow.portMode(MCP23S17_PORTA, 0x00);  // 0x00 = alle Pins als Eingang
    mcpColLow.portMode(MCP23S17_PORTB, 0x00);
    mcpColLow.setPortPullups(MCP23S17_PORTA, 0xFF);  // Pull-ups aktivieren
    mcpColLow.setPortPullups(MCP23S17_PORTB, 0xFF);
    
    // MCP_ADDR: Port B als Eingang mit Pull-ups
    mcpAddr.portMode(MCP23S17_PORTB, 0x00);
    mcpAddr.setPortPullups(MCP23S17_PORTB, 0xFF);
}

class LEDControllerV2 : public ILEDController {
private:
    bool ledMatrix[LED_ROWS][COLS];
    std::map<std::string, std::pair<int, int>> ledNameMap;
    bool showingRandomPattern;
    
    void initLEDMapping() {
        // PC (16 Bit) - GESPIEGELT
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "pc%02d", i);
            ledNameMap[name] = {0, 17 - i};
        }
        
        // MA (16 Bit) - GESPIEGELT
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "ma%02d", i);
            ledNameMap[name] = {1, 17 - i};
        }
        
        // MB (18 Bit) - GESPIEGELT
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "mb%02d", i);
            ledNameMap[name] = {2, 17 - i};
        }
        
        // AC (18 Bit) - GESPIEGELT
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "ac%02d", i);
            ledNameMap[name] = {3, 17 - i};
        }
        
        // IO (18 Bit) - GESPIEGELT
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "io%02d", i);
            ledNameMap[name] = {4, 17 - i};
        }
           
        
        ledNameMap["RUN"] = {5, 0};
        ledNameMap["CYC"] = {5, 1};
        ledNameMap["Df1"] = {5, 2};
        ledNameMap["HSC"] = {5, 3};
        ledNameMap["BC1"] = {5, 4};
        ledNameMap["BC2"] = {5, 5};
        ledNameMap["OV1"] = {5, 6};
        ledNameMap["RIM"] = {5, 7};
        ledNameMap["SBM"] = {5, 8};
        ledNameMap["EXD"] = {5, 9};
        ledNameMap["IOH"] = {5, 10};
        ledNameMap["IOC"] = {5, 11};
        ledNameMap["IOS"] = {5, 12};
        ledNameMap["PWR"] = {5, 15};
        ledNameMap["SSTEP"] = {5, 16};
        ledNameMap["SINSTR"] = {5, 17};
        
        // IR (5 Bit) - GESPIEGELT
        for (int i = 0; i < 5; i++) {
            char name[8];
            sprintf(name, "ir%02d", i);
            ledNameMap[name] = {6, 4 - i};
        }
        
        for (int i = 1; i <= 6; i++) {
            char name[8];
            sprintf(name, "SS%d", i);
            ledNameMap[name] = {6, 5 + i};
        }
        
        for (int i = 1; i <= 6; i++) {
            char name[8];
            sprintf(name, "PF%d", i);
            ledNameMap[name] = {6, 11 + i};
        }
    }
    
    void setLED(const char* name, bool state) {
        auto it = ledNameMap.find(name);
        if (it != ledNameMap.end()) {
            ledMatrix[it->second.first][it->second.second] = state;
        }
    }
    
    void setLED(uint8_t row, uint8_t col, bool state) {
        if (row < LED_ROWS && col < COLS) {
            ledMatrix[row][col] = state;
        }
    }
    
    void updateLEDMatrix() {
        for (uint8_t row = 0; row < LED_ROWS; row++) {
            // Alle Spalten ausschalten
            mcpColLow.writePort(MCP23S17_PORTA, 0xFF);
            mcpColLow.writePort(MCP23S17_PORTB, 0xFF);
            mcpAddr.writePort(MCP23S17_PORTB, 0xFF);
            
            delayMicroseconds(10);
            
            // Zeile aktivieren
            setDecoderAddress(row, true, false);
            
            delayMicroseconds(5);
            
            // Spaltendaten vorbereiten (invertiert: 0 = LED an)
            uint32_t colData = 0x3FFFF;
            
            for (uint8_t col = 0; col < COLS; col++) {
                if (ledMatrix[row][col]) {
                    colData &= ~(1 << col);
                }
            }
            
            // Spaltendaten schreiben
            mcpColLow.writePort(MCP23S17_PORTA, colData & 0xFF);         // COL0-7
            mcpColLow.writePort(MCP23S17_PORTB, (colData >> 8) & 0xFF);  // COL8-15
            
            uint8_t col16_17 = (colData >> 16) & 0x03;
            col16_17 = ((col16_17 & 1) << 1) | ((col16_17 & 2) >> 1);  // Swap bit 0 und 1
            mcpAddr.writePort(MCP23S17_PORTB, col16_17);  // COL16-17
            
            delayMicroseconds(400);
        }
        
        // Alle LEDs ausschalten
        mcpColLow.writePort(MCP23S17_PORTA, 0xFF);
        mcpColLow.writePort(MCP23S17_PORTB, 0xFF);
        mcpAddr.writePort(MCP23S17_PORTB, 0xFF);
    }
    
public:
    LEDControllerV2() {
        memset(ledMatrix, 0, sizeof(ledMatrix));
        showingRandomPattern = false;
    }
    
    void begin() override {
        // SPI bereits im Hauptprogramm initialisiert
        delay(100);
        
        // MCP23S17 Chips initialisieren
        if (!mcpAddr.begin()) {
            Serial.println("Error: MCP_ADDR (0x00) not initialised!");
        }
        if (!mcpColLow.begin()) {
            Serial.println("Error: MCP_COL_LOW (0x01)  not initialised!");
        }
        
        delay(50);
        
        // MCP_ADDR: Port A = Decoder (Ausgang), Port B = COL16-17 (Ausgang)
        mcpAddr.portMode(MCP23S17_PORTA, 0xFF);  // Alle Pins als Ausgang
        mcpAddr.portMode(MCP23S17_PORTB, 0xFF);
        
        // Spalten als Ausgänge konfigurieren
        configureCOLsAsOutputs();
        
        // LED-Mapping initialisieren
        initLEDMapping();
        
        Serial.println("LED Controller V2 initialised (PiDP-1 Matrix)");
        Serial.println("  MCP 0x00: Port A=Decoder, Port B=COL16-17");
        Serial.println("  MCP 0x01: Port A=COL0-7, Port B=COL8-15");
    }
    
    void updateDisplay(uint32_t ac, uint32_t io, uint16_t pc, uint16_t ma, 
                      uint32_t mb, uint32_t instr, bool ov, uint8_t pf,
                      uint8_t senseSw, bool power, bool run, bool step,
                      bool extend = false) override {
        
        if (!power) {
            showingRandomPattern = false;
            allOff();
            return;
        }
        
        if (showingRandomPattern) {
            return;
        }
        
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "pc%02d", i);
            setLED(name, (pc >> i) & 1);
        }
        
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "ma%02d", i);
            setLED(name, (ma >> i) & 1);
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "mb%02d", i);
            setLED(name, (mb >> i) & 1);
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "ac%02d", i);
            setLED(name, (ac >> i) & 1);
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "io%02d", i);
            setLED(name, (io >> i) & 1);
        }
        
        for (int i = 0; i < 5; i++) {
            char name[8];
            sprintf(name, "ir%02d", i);
            setLED(name, (instr >> (13 + i)) & 1);
        }
        
        for (int i = 0; i < 6; i++) {
            char name[8];
            sprintf(name, "SS%d", i + 1);
            setLED(name, (senseSw >> i) & 1);
        }
        
        for (int i = 0; i < 6; i++) {
            char name[8];
            sprintf(name, "PF%d", i + 1);
            setLED(name, (pf >> i) & 1);
        }
        
        setLED("RUN", run);
        setLED("PWR", power);
        setLED("SSTEP", step);
        setLED("OV1", ov);
        setLED("EXD", extend);  // Extend LED
        
        updateLEDMatrix();
    }
    
    void allOff() override {
        memset(ledMatrix, 0, sizeof(ledMatrix));
        mcpColLow.writePort(MCP23S17_PORTA, 0xFF);
        mcpColLow.writePort(MCP23S17_PORTB, 0xFF);
        mcpAddr.writePort(MCP23S17_PORTB, 0xFF);
    }
    
    void testPattern() override {
        Serial.println("LED Test Pattern V2 ...");
        
        for (uint8_t row = 0; row < LED_ROWS; row++) {
            for (uint8_t col = 0; col < COLS; col++) {
                setLED(row, col, true);
                updateLEDMatrix();
                delay(20);
                setLED(row, col, false);
            }
        }
        
        Serial.println("LED Test finished");
    }
    
    void showRandomPattern() override {
        showingRandomPattern = true;
        
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "pc%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 16; i++) {
            char name[8];
            sprintf(name, "ma%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "mb%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "ac%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 18; i++) {
            char name[8];
            sprintf(name, "io%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 5; i++) {
            char name[8];
            sprintf(name, "ir%02d", i);
            setLED(name, random(0, 2));
        }
        
        for (int i = 0; i < 6; i++) {
            char name[8];
            sprintf(name, "PF%d", i + 1);
            setLED(name, random(0, 2));
        }
        
        setLED("PWR", true);
        setLED("RUN", false);
        setLED("SSTEP", false);
        setLED("OV1", random(0, 2));
    }
    
    void clearRandomPattern() override {
        showingRandomPattern = false;
    }
    
    void refresh() override {
        updateLEDMatrix();
    }
    
    void forceRefresh() {
        updateLEDMatrix();
    }
};

class SwitchControllerV2 : public ISwitchController {
private:
    bool switchMatrix[SW_ROWS][COLS];
    std::map<std::string, std::pair<int, int>> switchNameMap;
    
    void initSwitchMapping() {
        switchNameMap["EXT"] = {0, 0};
        switchNameMap["PWR"] = {0, 1};
        // for (int i = 2; i <= 17; i++) {
        //     char name[8];
        //     sprintf(name, "TA%02d", 15 - i);
        //     switchNameMap[name] = {0, i};
        // }
        
        switchNameMap["TA02"] = { 0, 17 };
        switchNameMap["TA03"] = { 0, 16 };
        switchNameMap["TA04"] = { 0, 15 };
        
        switchNameMap["TA05"] = { 0, 14 };
        switchNameMap["TA06"] = { 0, 13 };
        switchNameMap["TA07"] = { 0, 12 };
        
        switchNameMap["TA08"] = { 0, 11 };
        switchNameMap["TA09"] = { 0, 10 };
        switchNameMap["TA10"] = { 0, 9 };
        
        switchNameMap["TA11"] = { 0, 8 };
        switchNameMap["TA12"] = { 0, 7 };
        switchNameMap["TA13"] = { 0, 6 };
        
        switchNameMap["TA14"] = { 0, 5 };
        switchNameMap["TA15"] = { 0, 4 };
        switchNameMap["TA16"] = { 0, 3 };
        


        switchNameMap["TW00"] = { 1, 17 };
        switchNameMap["TW01"] = { 1, 16 };
        switchNameMap["TW02"] = { 1, 15 };
        
        switchNameMap["TW03"] = { 1, 14 };
        switchNameMap["TW04"] = { 1, 13 };
        switchNameMap["TW05"] = { 1, 12 };
        
        switchNameMap["TW06"] = { 1, 11 };
        switchNameMap["TW07"] = { 1, 10 };
        switchNameMap["TW08"] = { 1, 9 };
        
        switchNameMap["TW09"] = { 1, 8 };
        switchNameMap["TW10"] = { 1, 7 };
        switchNameMap["TW11"] = { 1, 6 };
        
        switchNameMap["TW12"] = { 1, 5 };
        switchNameMap["TW13"] = { 1, 4 };
        switchNameMap["TW14"] = { 1, 3 };
        
        switchNameMap["TW15"] = { 1, 2 };
        switchNameMap["TW16"] = { 1, 1 };
        switchNameMap["TW17"] = { 1, 0 };
        

        // for (int i = 0; i <= 17; i++) {
        //     char name[8];
        //     sprintf(name, "TW%02d", 17 - i);
        //     switchNameMap[name] = {1, i};
        // }
        
        switchNameMap["SSTEP"] = {2, 0};
        switchNameMap["SINST"] = {2, 1};
        for (int i = 1; i <= 6; i++) {
            char name[8];
            sprintf(name, "SW%d", i);
            switchNameMap[name] = {2, i + 1};
        }
        switchNameMap["START1"] = {2, 8};
        switchNameMap["START2"] = {2, 9};
        switchNameMap["STOP"] = {2, 10};
        switchNameMap["CONT"] = {2, 11};
        switchNameMap["EXAMINE"] = {2, 12};
        switchNameMap["DEPOSIT"] = {2, 13};
        switchNameMap["READIN"] = {2, 14};
        switchNameMap["READER1"] = {2, 15};
        switchNameMap["READER2"] = {2, 16};
        switchNameMap["FEED"] = {2, 17};
    }
    
    bool getSwitch(const char* name) {
        auto it = switchNameMap.find(name);
        if (it != switchNameMap.end()) {
            return switchMatrix[it->second.first][it->second.second];
        }
        return false;
    }
    
public:
    SwitchControllerV2() {
        memset(switchMatrix, 0, sizeof(switchMatrix));
    }
    
    void begin() override {
        initSwitchMapping();
        Serial.println("Switch Controller V2 initialised (PiDP-1 Matrix)");
    }
    
    void update() override {
        configureCOLsAsInputs();
        delayMicroseconds(50);
        
        for (uint8_t row = 0; row < SW_ROWS; row++) {
            setDecoderAddress(row, false, true);
            delayMicroseconds(100);
            
            // Spalten lesen
            uint8_t colA = mcpColLow.readPort(MCP23S17_PORTA);
            uint8_t colB = mcpColLow.readPort(MCP23S17_PORTB);
            uint8_t colC = mcpAddr.readPort(MCP23S17_PORTB);

            // Bit 0 und 1 von colC tauschen - HINZUFÜGEN
            colC = ((colC & 1) << 1) | ((colC & 2) >> 1);  // Swap bit 0 und 1
            

            uint32_t allCols = colA | (colB << 8) | ((colC & 0x03) << 16);
            
            for (uint8_t col = 0; col < COLS; col++) {
                switchMatrix[row][col] = !(allCols & (1 << col));
            }
        }
        
        configureCOLsAsOutputs();
        
        mcpColLow.writePort(MCP23S17_PORTA, 0xFF);
        mcpColLow.writePort(MCP23S17_PORTB, 0xFF);
        mcpAddr.writePort(MCP23S17_PORTB, 0xFF);
    }
    
    uint16_t getAddressSwitches() override {
        uint16_t addr = 0;
        for (int i = 2; i <= 17; i++) {
            char name[8];
            sprintf(name, "TA%02d", i);
            if (getSwitch(name)) {
                addr |= (1 << (i - 2));
            }
        }
        return addr;
    }
    
    uint32_t getTestWord() override {
        uint32_t tw = 0;
        for (int i = 0; i <= 17; i++) {
            char name[8];
            sprintf(name, "TW%02d", i);
            if (getSwitch(name)) {
                tw |= (1 << i);
            }
        }
        return tw & WORD_MASK;
        //return tw;
    }
    
    uint8_t getSenseSwitches() override {
        uint8_t sw = 0;
        for (int i = 1; i <= 6; i++) {
            char name[8];
            sprintf(name, "SW%d", i);
            if (getSwitch(name)) {
                sw |= (1 << (i - 1));
            }
        }
        return sw;
    }
    
    bool getExtendSwitch() override  { return getSwitch("EXT"); }
    bool getStartDown() override     { return getSwitch("START1"); }
    bool getStartUp() override       { return getSwitch("START2"); }
    bool getStop() override          { return getSwitch("STOP"); }
    bool getContinue() override      { return getSwitch("CONT"); }
    bool getExamine() override       { return getSwitch("EXAMINE"); }
    bool getDeposit() override       { return getSwitch("DEPOSIT"); }
    bool getReadIn() override        { return getSwitch("READIN"); }
    bool getPower() override         { return getSwitch("PWR"); }
    bool getSingleStep() override    { return getSwitch("SSTEP"); }
    bool getSingleInstr() override   { return getSwitch("SINST"); }
    
    bool getStartDownPressed() override   { return getSwitch("START1"); }
    bool getStartUpPressed() override     { return getSwitch("START2"); }
    bool getStopPressed() override        { return getSwitch("STOP"); }
    bool getContinuePressed() override    { return getSwitch("CONT"); }
    bool getExaminePressed() override     { return getSwitch("EXAMINE"); }
    bool getDepositPressed() override     { return getSwitch("DEPOSIT"); }
    bool getReadInPressed() override      { return getSwitch("READIN"); }
    bool getSingleStepPressed() override  { return getSwitch("SSTEP"); }
    bool getSingleInstrPressed() override { return getSwitch("SINST"); }
    
    void printStatus() override {
        Serial.println("\n=== Switch Status V2 ===");
        Serial.printf("Address: %04o  Test Word: %06o  Sense: %02o\n",
                     getAddressSwitches(), getTestWord(), getSenseSwitches());
        Serial.println("=================================================\n");
    }
};

#endif
