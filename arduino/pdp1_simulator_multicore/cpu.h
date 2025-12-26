#ifndef CPU_H
#define CPU_H

#include <Arduino.h>
#include <SD.h>

// PDP-1 Architecture Constants
#define WORD_MASK 0777777
#define SIGN_BIT  0400000
#define ADDR_MASK 07777
#define MEMORY_SIZE 4096

#define OP_MASK   0760000
#define I_BIT     0010000
#define Y_MASK    0007777

class ISwitchController;

// Forward declarations for hardware abstraction
class ILEDController {
public:
    virtual void begin() = 0;
    virtual void updateDisplay(uint32_t ac, uint32_t io, uint16_t pc, uint16_t ma, 
                              uint32_t mb, uint32_t instr, bool ov, uint8_t pf,
                              uint8_t senseSw, bool power, bool run, bool step) = 0;
    virtual void allOff() = 0;
    virtual void testPattern() = 0;
    virtual void showRandomPattern() { }  // Optional - nur Version 2 nutzt das
    virtual void clearRandomPattern() { }  // Optional - nur Version 2 nutzt das
    virtual void refresh() { }  // Optional - nur Version 2 braucht kontinuierliches Refresh
    virtual ~ILEDController() {}
};

class ISwitchController {
public:
    virtual void begin() = 0;
    virtual void update() = 0;
    virtual uint16_t getAddressSwitches() = 0;
    virtual uint32_t getTestWord() = 0;
    virtual uint8_t getSenseSwitches() = 0;
    virtual bool getExtendSwitch() = 0;
    virtual bool getStartDown() = 0;
    virtual bool getStartUp() = 0;
    virtual bool getStop() = 0;
    virtual bool getContinue() = 0;
    virtual bool getExamine() = 0;
    virtual bool getDeposit() = 0;
    virtual bool getReadIn() = 0;
    virtual bool getPower() = 0;
    virtual bool getSingleStep() = 0;
    virtual bool getSingleInstr() = 0;
    virtual bool getStartDownPressed() = 0;
    virtual bool getStartUpPressed() = 0;
    virtual bool getStopPressed() = 0;
    virtual bool getContinuePressed() = 0;
    virtual bool getExaminePressed() = 0;
    virtual bool getDepositPressed() = 0;
    virtual bool getReadInPressed() = 0;
    virtual bool getSingleStepPressed() = 0;
    virtual bool getSingleInstrPressed() = 0;
    virtual void printStatus() = 0;
    virtual ~ISwitchController() {}
};

// Forward declaration für CPU
class PDP1;

// ============================================================================
// Paper Tape Stream - Virtuelles Paper Tape für RIM-Loader
// ============================================================================
class PaperTapeStream {
private:
    const uint8_t* data;
    size_t length;
    size_t position;
    
public:
    PaperTapeStream(const uint8_t* rimData, size_t len) 
        : data(rimData), length(len), position(0) {}
    
    // Liest das nächste 18-Bit Wort vom Tape
    // Filtert nur Bytes mit Bit 7 gesetzt (gültige RIM-Daten)
    uint32_t readWord() {
        uint32_t word = 0;
        int bitsRead = 0;
        
        // Wir brauchen 3 Bytes mit Bit 7 gesetzt (jedes trägt 6 Bit)
        while (bitsRead < 3 && position < length) {
            uint8_t byte = data[position++];
            
            // Nur Bytes mit Bit 7 = 1 sind gültig
            if (byte & 0x80) {
                word = (word << 6) | (byte & 0x3F);
                bitsRead++;
            }
        }
        
        return word & WORD_MASK;  // 18 Bit mask
    }
    
    bool hasMore() const {
        return position < length;
    }
    
    void reset() {
        position = 0;
    }
    
    size_t getPosition() const {
        return position;
    }
};

#ifdef WEBSERVER_SUPPORT
    extern bool isWebTapeMounted();
    extern void sendReaderPosition(size_t position);
#endif

// ============================================================================
// RIM Format Loader - Authentischer PDP-1 RIM-Loader
// ============================================================================
class RIMLoader {
private:
    static ISwitchController* switches;
    static PaperTapeStream* currentTape;  // Für SD-Karte
    static PaperTapeStream* webTape;      // NEU: Für Browser
    static PDP1* cpu;
    
    static const uint16_t RIM_LOADER_START = 07751;
    static const uint16_t RIM_LOADER_LENGTH = 43;
    
    static bool processRIMData(const uint8_t* rimData, size_t length, 
                              uint32_t* memory, uint16_t& startPC);

public:
    static void setSwitchController(ISwitchController* sw) {
        switches = sw;
    }
    
    static void setCPU(PDP1* cpuInstance) {
        cpu = cpuInstance;
    }
    
    // Bestehende Funktionen
    static bool loadFromSD(const char* filename, uint32_t* memory, uint16_t& startPC);
    static bool loadFromArray(const uint8_t* data, size_t length, 
                             uint32_t* memory, uint16_t& startPC);
    
    

    static uint32_t readPaperBinary() {
        // if (currentTape == nullptr || !currentTape->hasMore()) {
        //     Serial.println("  RPB: Tape leer!");
        //     return 0;
        // }
        if (currentTape == nullptr || !currentTape->hasMore()) {
        // Aufräumen
            if (currentTape != nullptr) {
                delete currentTape;
                currentTape = nullptr;
            }
            Serial.println("  RPB: Tape empty!");
            return 0;
        }
        uint32_t word = currentTape->readWord();

        #ifdef WEBSERVER_SUPPORT
            // Position-Update für Tape-Animation im Browser
            sendReaderPosition(currentTape->getPosition());
        #endif

        return word;
    }

    
    // Hilfsfunktionen
    static String getRIMFileFromFolder(uint8_t folderNumber);
    static void listSDFiles();
};

// class RIMLoader {
// private:
//     static ISwitchController* switches;
//     static PaperTapeStream* currentTape;  // Für SD-Karte
//     static PaperTapeStream* webTape;      // NEU: Für Browser
//     static PDP1* cpu;
    
//     static const uint16_t RIM_LOADER_START = 07751;  // Oktal
//     static const uint16_t RIM_LOADER_LENGTH = 43;     // 43 Worte
    
//     // Gemeinsame RIM-Verarbeitungslogik (für SD und Webserver)
//     static bool processRIMData(const uint8_t* rimData, size_t length, 
//                               uint32_t* memory, uint16_t& startPC);

// public:
//     static void setSwitchController(ISwitchController* sw) {
//         switches = sw;
//     }
    
//     static void setCPU(PDP1* cpuInstance) {
//         cpu = cpuInstance;
//     }
    
//     // Laden von SD-Karte
//     static bool loadFromSD(const char* filename, uint32_t* memory, uint16_t& startPC);
    
//     // Laden von Byte-Array (für Webserver)
//     static bool loadFromArray(const uint8_t* data, size_t length, 
//                              uint32_t* memory, uint16_t& startPC);
    
//     // Wird von CPU bei Opcode 730002 (rpb - Read Paper Binary) aufgerufen
//     // NEU: Automatische Source-Auswahl
//     static bool loadFromAutoSource(uint32_t* memory, uint16_t& startPC);
    
//     // ANGEPASST: RPB prüft jetzt Web-Tape zuerst
//     static uint32_t readPaperBinary() {
//         #ifdef WEBSERVER_SUPPORT
//             // Prüfe zuerst ob Web-Tape gemountet ist
//             if (isWebTapeMounted()) {
//                 return readPaperBinaryFromWeb();
//             }
//         #endif
        
//         // Fallback: SD-Karte
//         if (currentTape == nullptr || !currentTape->hasMore()) {
//             Serial.println("  RPB: Tape leer!");
//             return 0;
//         }
 
    
//     // Hilfsfunktionen
//     static String getRIMFileFromFolder(uint8_t folderNumber);
//     static void listSDFiles();
//     }
// };



// ============================================================================
// PDP-1 CPU
// ============================================================================
class PDP1 {
private:
    uint32_t AC;
    uint32_t IO;
    uint16_t PC;
    uint16_t MA;
    uint32_t MB;
    bool OV;
    bool PF[7];
    
    uint32_t memory[MEMORY_SIZE];
    
    bool running;
    bool halted;
    uint32_t cycles;
    
    String typewriter_buffer;
    ILEDController* leds;
    ISwitchController* switches;
    
    uint16_t examineAddress;
    bool powerOn;
    bool showRandomLEDs;
    bool stepModeStop;

    volatile bool* externalStopFlag;

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

    char fiodecToAscii(uint8_t fiodec) {
        fiodec &= 63;
        
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
            case 16:  return '0';
            case 17:  return '/';
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
        if (ch >= 'a' && ch <= 'i') return 49 + (ch - 'a');
        if (ch >= 'A' && ch <= 'I') return 49 + (ch - 'A');
        if (ch >= 'j' && ch <= 'r') return 33 + (ch - 'j');
        if (ch >= 'J' && ch <= 'R') return 33 + (ch - 'J');
        if (ch >= 's' && ch <= 'z') return 18 + (ch - 's');
        if (ch >= 'S' && ch <= 'Z') return 18 + (ch - 'S');
        
        if (ch == '0') return 16;
        if (ch >= '1' && ch <= '9') return (ch - '0');
        
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

    void executeMemoryReference(uint32_t instruction, uint8_t opcode, bool indirect, uint16_t Y);
    void executeOperate(uint32_t instruction);
    void executeSkip(uint32_t instruction);
    void executeShift(uint32_t instruction);
    void executeIOT(uint32_t instruction);

public:
    PDP1() {
        leds = nullptr;
        switches = nullptr;
        examineAddress = 0;
        powerOn = false;
        showRandomLEDs = false;
        stepModeStop = false;
        externalStopFlag = nullptr;  // NEU für Multicore!
        reset();
    }

    // Speicher-Zugriff für RIMLoader
    uint32_t* getMemory() { return memory; }

    void attachStopFlag(volatile bool* flag) {
        externalStopFlag = flag;
    }

    void setPF(uint8_t flag, bool state) {
        if (flag >= 1 && flag <= 6) {
            PF[flag] = state;
        }
    }

    void setProgramFlags(uint8_t flags) {
        PF[1] = (flags >> 5) & 1;  // Bit 5 -> PF1
        PF[2] = (flags >> 4) & 1;  // Bit 4 -> PF2
        PF[3] = (flags >> 3) & 1;  // Bit 3 -> PF3
        PF[4] = (flags >> 2) & 1;  // Bit 2 -> PF4
        PF[5] = (flags >> 1) & 1;  // Bit 1 -> PF5
        PF[6] = (flags >> 0) & 1;  // Bit 0 -> PF6
    }

    void attachLEDs(ILEDController* ledController) {
        leds = ledController;
    }
    
    void attachSwitches(ISwitchController* switchController) {
        switches = switchController;
    }
    
    void stop(){
        running = false;
        halted = false;
        Serial.println("haltet.");
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
        showRandomLEDs = false;
        updateLEDs();
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
        //if (value == WORD_MASK) value = 0;
        
        MA = addr;
        MB = value;
        memory[addr] = value;
    }
    
    void updateLEDs() {
        if (leds) {
            if (showRandomLEDs) {
                //Serial.println("  -> showRandomLEDs active, returning");  // DEBUG
                return;
            }
           
            //Serial.printf("  -> AC=%06o IO=%06o PC=%04o\n", AC, IO, PC);  // DEBUG
            
            uint8_t pfBits = 0;
            for (int i = 1; i <= 6; i++) {
                if (PF[i]) pfBits |= (1 << (i-1));
            }
            
            uint32_t currentInstr = memory[PC] & WORD_MASK;
            
            uint8_t senseSwitches = 0;
            if (switches) {
                senseSwitches = switches->getSenseSwitches();
            }
            
            bool stepMode = switches ? switches->getSingleStep() : false;
            
            leds->updateDisplay(AC, IO, PC, MA, MB, currentInstr, OV, pfBits,
                              senseSwitches, powerOn, running, stepMode);
        }

        if (!leds) {
            Serial.println("ERROR: leds is NULL!");
        return;
    }
    }
    
    void handleSwitches();
    void executeInstruction();
    void step();
    
    // CPU-Zugriffsmethoden für RIM-Loader
    void setPC(uint16_t pc) { PC = pc & ADDR_MASK; }
    void setAC(uint32_t ac) { AC = ac & WORD_MASK; }
    void setIO(uint32_t io) { IO = io & WORD_MASK; }
    void setState(bool run) { running = run; halted = !run; }
    uint16_t getPC() const { return PC; }
    uint32_t getAC() const { return AC; }
    bool getState() const { return running && !halted; }
    
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
    
    void loadLEDTestProgram();
};

// Static member initialization
ISwitchController* RIMLoader::switches = nullptr;
PaperTapeStream* RIMLoader::currentTape = nullptr;
PaperTapeStream* RIMLoader::webTape = nullptr;  // NEU!
PDP1* RIMLoader::cpu = nullptr;
// Implementation of complex methods

// ============================================================================
// RIMLoader Implementation
// ============================================================================

bool RIMLoader::processRIMData(const uint8_t* rimData, size_t length, 
                              uint32_t* memory, uint16_t& startPC) {
    // // Paper Tape Stream erstellen
    // PaperTapeStream tape(rimData, length);
    // currentTape = &tape;
    
    // NEU: Auf dem Heap allozieren - bleibt gültig nach return!
    if (currentTape != nullptr) {
        delete currentTape;  // Altes Tape aufräumen
    }
    currentTape = new PaperTapeStream(rimData, length);

    // ====================================================================
    // PHASE 1: Hardware RIM-Mode
    // ====================================================================
    //Serial.println("=== PHASE 1: Hardware RIM-Mode ===");
    Serial.println("Load RIM-Loader Code...\n");
    
    int wordsLoaded = 0;
    
    while (currentTape->hasMore()) {
        uint32_t firstWord = currentTape->readWord();
        uint8_t opcode = (firstWord >> 12) & 077;
        uint16_t addr = firstWord & ADDR_MASK;
        
        // Ende-Marker gefunden → RIM-Loader komplett
        if (firstWord == 0607751) {
            Serial.println("\nRIM-Loader complete - End-Marker 607751 found");
            break;
        }
        
        if (!currentTape->hasMore()) {
            Serial.println("Error: incomplete Word-pair!");
            delete currentTape;
            currentTape = nullptr;
            return false;
        }
        
        uint32_t secondWord = currentTape->readWord();
        
        if (opcode == 032 || opcode == 060) {
            //Serial.printf("  [%05o] = %06o\n", addr, secondWord);
            memory[addr] = secondWord;
            wordsLoaded++;
        }
        else {
            Serial.printf("Warning: unexpected Opcode %02o\n", opcode);
        }
    }
    
    Serial.printf("\nRIM-Loader: %d Words loaded\n", wordsLoaded);
    
    // ====================================================================
    // PHASE 2: CPU starten - cpuTask übernimmt!
    // ====================================================================
    Serial.println("\n=== PHASE 2: CPU starts from Memory-Loaction 7751 ===\n");
    
    // if (cpu == nullptr) {
    //     Serial.println("FEHLER: CPU nicht initialisiert!");
    //     currentTape = nullptr;
    //     return false;
    // }
    
    // CPU vorbereiten und starten
    cpu->setPC(RIM_LOADER_START);  // 7751
    cpu->setAC(0);
    cpu->setIO(0);
    cpu->run();  // Startet die CPU - cpuTask auf Core 1 übernimmt!
    Serial.println("CPU startet, wait for Core 1...");
    delay(10);  // Kurz warten damit Core 1 das Flag sieht
    // WICHTIG: currentTape NICHT auf nullptr setzen!
    // Der RIM-Loader braucht das Tape noch für RPB!
    
    startPC = RIM_LOADER_START;
    return true;
}
/*
// Gemeinsame RIM-Verarbeitungslogik (für SD und Webserver)
bool RIMLoader::processRIMData(const uint8_t* rimData, size_t length, 
                              uint32_t* memory, uint16_t& startPC) {
    // Paper Tape Stream erstellen
    PaperTapeStream tape(rimData, length);
    currentTape = &tape;
    
    // ====================================================================
    // PHASE 1: Hardware RIM-Mode
    // Die ersten Worte (RIM-Loader) interpretieren und laden
    // RIM-Format: Immer paarweise - Adresse/JMP + Daten
    // Ende: Wenn erstes Wort = 607751 (kommt nur am Ende vor)
    // ====================================================================
    Serial.println("=== PHASE 1: Hardware RIM-Mode ===");
    Serial.println("Lade RIM-Loader Code...\n");
    
    int wordsLoaded = 0;
    bool inRIMLoader = true;
    
    while (tape.hasMore() && inRIMLoader) {
        // Lese erstes Wort (Adresse oder JMP)
        uint32_t firstWord = tape.readWord();
        uint8_t opcode = (firstWord >> 12) & 077;
        uint16_t addr = firstWord & ADDR_MASK;
        
        // Prüfe ob das erste Wort 607751 ist - dann sind wir am Ende
        if (firstWord == 0607751) {
            Serial.println("\nRIM-Loader komplett - Ende-Marker 607751 gefunden");
            inRIMLoader = false;
            break;  // NICHT speichern - steht schon bei 7775!
        }
        
        // Lese zweites Wort (Daten)
        if (!tape.hasMore()) {
            Serial.println("FEHLER: Unvollständiges Wort-Paar!");
            break;
        }
        uint32_t secondWord = tape.readWord();
        
        // Speichere Daten an der Adresse
        if (opcode == 032 || opcode == 060) {  // DIO oder JMP
            Serial.printf("  [%05o] = %06o\n", addr, secondWord);
            memory[addr] = secondWord;
            wordsLoaded++;
        }
        else {
            Serial.printf("WARNUNG: Unerwarteter Opcode %02o (Wort=%06o) - ignoriere\n", 
                         opcode, firstWord);
        }
    }
    
    Serial.printf("\nRIM-Loader: %d Worte geladen\n", wordsLoaded);
    
    // ====================================================================
    // PHASE 2: CPU führt RIM-Loader aus
    // Ab jetzt läuft der RIM-Loader auf der CPU und liest den Rest vom Tape
    // ====================================================================
    Serial.println("\n=== PHASE 2: CPU übernimmt (RIM-Loader läuft) ===\n");
    
    extern volatile bool g_rimLoadingActive;
    g_rimLoadingActive = true;  // Core 1 soll warten


    if (cpu == nullptr) {
        Serial.println("FEHLER: CPU nicht initialisiert!");
        return false;
    }
    
    // CPU vorbereiten
    cpu->setPC(RIM_LOADER_START);
    cpu->setAC(0);
    cpu->setIO(0);
    cpu->setState(true);  // Running
    
    // CPU laufen lassen
    int maxInstructions = 100000;  // Sicherheits-Timeout
    int instructionCount = 0;
    
    while (cpu->getState() && instructionCount < maxInstructions) {
        cpu->executeInstruction();
        instructionCount++;

        // if (instructionCount % 100 == 0) { 
        //     //cpu->updateLEDs();
        //     yield();
        // }

        // Bei HLT (760400) stoppen
        uint32_t currentInstr = memory[cpu->getPC()] & WORD_MASK;
        if (currentInstr == 0760400) {
            Serial.println("  HLT erreicht");
            break;
        }
    }
    
    if (instructionCount >= maxInstructions) {
        Serial.println("\nFEHLER: RIM-Loader Timeout!");
        return false;
    }
    
    Serial.printf("\n=== Programm fertig nach %d Instruktionen ===\n", instructionCount);
    Serial.printf("PC steht jetzt bei: %05o\n", cpu->getPC());
    Serial.printf("AC = %06o\n", cpu->getAC());
    
    // StartPC ist dort wo der PC jetzt steht
    startPC = cpu->getPC();
    
    //cpu->setState(false);   //cpu stoppen nach load

    currentTape = nullptr;
    return true;
}
*/

// Laden von SD-Karte
bool RIMLoader::loadFromSD(const char* filename, uint32_t* memory, uint16_t& startPC) {
    uint8_t senseValue = 0;
    if (switches != nullptr) {
        senseValue = switches->getSenseSwitches();
    }
    
    Serial.printf("Load from Folder %d: %s\n", senseValue, filename);
    
    File file = SD.open(filename);
    if (!file) {
        Serial.printf("Error: File %s not found!\n", filename);
        return false;
    }

    Serial.printf("Load RIM-Datei: %s (%d bytes)\n\n", filename, file.size());
    
    // RIM-Datei in Byte-Array lesen
    size_t fileSize = file.size();
    uint8_t* rimData = new uint8_t[fileSize];
    file.read(rimData, fileSize);
    file.close();
    
    // Gemeinsame Verarbeitungslogik nutzen
    bool result = processRIMData(rimData, fileSize, memory, startPC);
    
    delete[] rimData;
    return result;
}

// Laden von Byte-Array (für Webserver)
bool RIMLoader::loadFromArray(const uint8_t* data, size_t length, 
                             uint32_t* memory, uint16_t& startPC) {
    if (data == nullptr || length == 0) {
        Serial.println("Error: no Files for loading!");
        return false;
    }
    
    Serial.printf("Load RIM-Datei from Memory (%d bytes)\n\n", length);
    
    // Gemeinsame Verarbeitungslogik nutzen
    return processRIMData(data, length, memory, startPC);
}

// Hilfsfunktionen
String RIMLoader::getRIMFileFromFolder(uint8_t folderNumber) {
    char folderPath[16];
    sprintf(folderPath, "/%d", folderNumber);
    
    File dir = SD.open(folderPath);
    if (!dir) {
        Serial.printf("Folder '%s' not found\n", folderPath);
        return "";
    }
    
    if (!dir.isDirectory()) {
        Serial.printf("'%s' is no Folder\n", folderPath);
        dir.close();
        return "";
    }
    
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
    Serial.printf("no .rim File foun in Folder '%s' found\n", folderPath);
    return "";
}


void RIMLoader::listSDFiles() {
    Serial.println("\n=== SD-Card Folders ===");
    
    int totalCount = 0;
    
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
                        Serial.printf("\nFolder %d:\n", folder);
                        foundFiles = true;
                    }
                    Serial.printf("  %s\n", name.c_str());
                    totalCount++;
                }
            }
            entry.close();
        }
        dir.close();
    }
    
    Serial.printf("\nSummary: %d RIM-Files found.\n\n", totalCount);
}

#include "cpu_impl.h"

#endif // CPU_H
