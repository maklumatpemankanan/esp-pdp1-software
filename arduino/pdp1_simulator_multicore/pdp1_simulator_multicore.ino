/***************************************************************
PDP1 Simulator for ESP32 Microcontroller - MULTICORE VERSION
(c) by Matthias Barthel

Version1 - works with the esp-pdp1 from my github
Version2 - works with my esp32-pidp1 adapter

Both Versions now run on DUAL CORE with instant stop button response
****************************************************************/

/*

*/

/*
MULTICORE ARCHITECTURE:
- Core 0: UI, WebSocket, Switches, Display, Stop-Button handling
- Core 1: PDP-1 CPU execution loop
- Communication via volatile flags and mutex
*/

//Choose your Version you want to use
//#define USE_VERSION1  // MCP23S17 + PCF8574
#define USE_VERSION2  // PiDP-1 Matrix

//uncomment to activate the backplane
#define BACKPLANE_SUPPORT

//uncomment to activate the webserver
#define WEBSERVER_SUPPORT

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <MCP23S17.h>

// Hardware-Pins (vor both hardware-versions)
#define SD_CS_PIN 16    // SD-Karte CS Pin 
#define MCP_CS_PIN 17  // MCP23S17 CS Pin 

// ============================================================================
// MULTICORE GLOBALS
// ============================================================================
TaskHandle_t cpuTaskHandle = NULL;
SemaphoreHandle_t cpuMutex = NULL;

// Volatile flags für Inter-Core-Kommunikation
volatile bool DRAM_ATTR g_cpuShouldStop = false;    // Stop-Request von Core 0
volatile bool DRAM_ATTR g_cpuIsRunning = false;     // CPU-Status für Core 0
volatile uint32_t g_instructionsExecuted = 0;        // Performance Counter
volatile bool DRAM_ATTR g_rimLoadingActive = false;  // NEU

#ifdef BACKPLANE_SUPPORT
    #include "backplane.h"
    volatile bool DRAM_ATTR g_backplaneInterruptFlag = false;
    
    void IRAM_ATTR backplaneISR() {
        g_backplaneInterruptFlag = true;
    }
#endif

// CPU Core einbinden
#include "cpu.h"

#ifdef WEBSERVER_SUPPORT
    #include "webserver.h"
#endif

// Hardware-Version einbinden
#ifdef USE_VERSION1
    #include "version1.h"
    LEDControllerV1 leds(&SPI, MCP_CS_PIN);
    SwitchControllerV1 switches;
    #define VERSION_NAME "Version 1 (MCP23S17 + PCF8574) - MULTICORE"
#elif defined(USE_VERSION2)
    #define SD_MISO  13  
    #define SD_MOSI  12
    #define SD_SCK   14
    SPIClass spiSD(HSPI);
    #include "version2.h"
    LEDControllerV2 leds;
    SwitchControllerV2 switches;
    #define VERSION_NAME "Version 2 (PiDP-1 Matrix) - MULTICORE"
#else
    #error "please activate Version1 or Version2!"
#endif

// CPU Instanz
PDP1 cpu;

// ============================================================================
// CPU TASK - Läuft auf CORE 1
// ============================================================================

void cpuTask(void* parameter) {
    static unsigned long lastLEDUpdate = 0;
    const unsigned long LED_UPDATE_INTERVAL = 16;  // 16ms = ~60 Hz
    
    Serial.println("[CPU TASK] Started on Core 1");
    
    for (;;) {
        
        if (g_rimLoadingActive) {
            delay(10);
            continue;
        }
        // ====================================================================
        // PHASE 1: CPU-State abfragen (mit Mutex)
        // ====================================================================
        if (xSemaphoreTake(cpuMutex, portMAX_DELAY) == pdTRUE) {
            
            // Stop-Signal verarbeiten
            if (g_cpuShouldStop && g_cpuIsRunning) {
                cpu.stop();
                g_cpuIsRunning = false;
                g_cpuShouldStop = false;
                Serial.println("[CPU TASK] Stopped by signal");
                
                // LEDs nach Stop SOFORT aktualisieren!
                cpu.updateLEDs();
                
                xSemaphoreGive(cpuMutex);
                continue;  // Zurück zum Anfang
            }
            
            // Prüfe ob CPU laufen soll
            bool shouldRun = cpu.isRunning();
            
            // ================================================================
            // LED-UPDATE HIER AUF CORE 1! (IMMER, auch wenn CPU gestoppt!)
            // ================================================================
            if (millis() - lastLEDUpdate >= LED_UPDATE_INTERVAL) {
                cpu.updateLEDs();
                lastLEDUpdate = millis();
            }
            
            xSemaphoreGive(cpuMutex);
            
            // ================================================================
            // PHASE 2: CPU-Execution (wenn running)
            // ================================================================
            if (shouldRun) {
                g_cpuIsRunning = true;
                
                // Mutex für Instruction-Execution
                if (xSemaphoreTake(cpuMutex, 1) == pdTRUE) {
                    
                    // Stop-Check VOR Execution
                    if (!g_cpuShouldStop) {
                        cpu.step();
                        g_instructionsExecuted++;
                    }
                    
                    // Prüfe ob CPU sich selbst gestoppt hat (HLT, etc.)
                    if (!cpu.isRunning()) {
                        g_cpuIsRunning = false;
                    }
                    
                    xSemaphoreGive(cpuMutex);
                    
                } else {
                    // Mutex timeout - CPU busy, kurz warten
                    delay(1);
                }
                
            } else {
                // ============================================================
                // CPU IDLE - nicht running
                // ============================================================
                g_cpuIsRunning = false;
                delay(10);  // Länger warten wenn CPU idle
            }
            
        } else {
            // Mutex-Fehler (sollte nicht passieren bei portMAX_DELAY)
            Serial.println("[CPU TASK] ERROR: Mutex timeout!");
            delay(100);
        }
        
        // Task-Scheduler: Anderen Tasks CPU-Zeit geben
        taskYIELD();
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n============================================");
    Serial.println("PDP-1 Simulator");
    Serial.printf("Hardware: %s\n", VERSION_NAME);
    Serial.println("============================================\n");
    
    // Mutex erstellen BEVOR andere Inits
    cpuMutex = xSemaphoreCreateMutex();
    if (cpuMutex == NULL) {
        Serial.println("FEHLER: can't create Mutex!");
        while(1) delay(1000);
    }
    
    // SPI initialisieren
    SPI.begin();
    SPI.setFrequency(1000000);

    #ifdef USE_VERSION2
        spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_PIN);
        spiSD.setFrequency(1000000);
    #endif
    
    // Switch Controller initialisieren
    Serial.println("Init Switch Controller...");
    switches.begin();
    cpu.attachSwitches(&switches);
    
    RIMLoader::setSwitchController(&switches);
    RIMLoader::setCPU(&cpu);

    // LED Controller initialisieren
    Serial.println("Init LED Controller...");
    leds.begin();
    cpu.attachLEDs(&leds);
    
    #ifdef BACKPLANE_SUPPORT
        bkp_mcp_init();
        pinMode(BKP_INT, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(BKP_INT), backplaneISR, FALLING);
        Serial.println("Backplane Interrupt configured");
    #endif
    
    // SD-Karte initialisieren
    #ifdef USE_VERSION1
        delay(1000);
        Serial.println("\nInit SD-Card...");
        if (!SD.begin(SD_CS_PIN)) {
            Serial.println("WARNING: SD-Card not found!");
            Serial.println("Simulator runs without SD-Card support.\n");
        } else {
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD-Card found: %llu MB\n", cardSize);
            RIMLoader::listSDFiles();
        }
    #elif defined(USE_VERSION2)
        Serial.println("\nInit SD-Card...");
        if (!SD.begin(SD_CS_PIN, spiSD)) {
            Serial.println("WARNING: SD-Card not found!");
            Serial.println("Simulator runs without SD-Card support.\n");
        } else {
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD-Carte found: %llu MB\n", cardSize);
            RIMLoader::listSDFiles();
        }
    #endif
    
    // CPU-Task auf Core 1 starten
    xTaskCreatePinnedToCore(
        cpuTask,           // Task-Funktion
        "CPU_Task",        // Name
        8192,              // Stack-Größe
        NULL,              // Parameter
        1,                 // Priorität
        &cpuTaskHandle,    // Task-Handle
        1                  // Core 1
    );
    
    Serial.println("CPU Task startet on Core 1");
    
    #ifdef WEBSERVER_SUPPORT
        setup_wifi();
        delay(100);
        setupWebserver();
        Serial.println("Webserver startet");
    #endif

    
    Serial.println("\n=== Simulator ready ===\n");
    printHelp();
}

void printHelp() {
    Serial.println("\n=== Commands ===");
    Serial.println("l <filename>  - load RIM-file");
    Serial.println("f             - list Files from SD-Card");
    Serial.println("m             - start LED Test");
    Serial.println("r             - start CPU");
    Serial.println("s             - Stepmode");
    Serial.println("d [adresse]   - Memory Dump (oktal, z.B. 'd 10000' für Bank 1)");
    Serial.println("p             - CPU State");
    Serial.println("w             - Switch State");
    Serial.println("t             - LED Test");
    Serial.println("o             - LEDs off");
    Serial.println("x             - Reset CPU");
    Serial.println("e             - Toggle Extend Mode (Memory Extension)");
    Serial.println("i             - Performance Info");
    Serial.println("h             - Help");
    #ifdef BACKPLANE_SUPPORT
    Serial.println("b             - Backplane Test");
    #endif
    #ifdef WEBSERVER_SUPPORT
    Serial.println("a             - Display Test");
    #endif
    Serial.println("========================\n");
}

// ============================================================================
// MAIN LOOP - Läuft auf CORE 0
// ============================================================================
void loop() {

    // ========================================================================
    // VERSION 2: Matrix Refresh (muss auf Core 0 bleiben wegen SPI-Sharing)
    // ========================================================================
    #ifdef USE_VERSION2
        static unsigned long lastMatrixRefresh = 0;
        static const unsigned long MATRIX_REFRESH_INTERVAL = 8;  // 8ms = ~120 Hz
        
        if (millis() - lastMatrixRefresh >= MATRIX_REFRESH_INTERVAL) {
            leds.forceRefresh();
            lastMatrixRefresh = millis();
        }
    #endif

    #ifdef WEBSERVER_SUPPORT
        // WebSocket Cleanup (seltener!)
        static unsigned long lastWSCleanup = 0;
        if (millis() - lastWSCleanup > 10000) {  // Alle 10 Sekunden
            ws.cleanupClients();
            lastWSCleanup = millis();
        }

        static unsigned long lastSend = 0;
        if (millis() - lastSend >= 33) {
            if (!displayBuffer.empty() && displayConnected && ws.count()) {
                sendDisplayPointsBatch();
            }

        // PUNCH BATCH SENDEN (NEU!)
        if (!punchBuffer.empty() && ws.count()) {
            sendPunchDataBatch();
        }
            lastSend = millis();
        }
    #endif

    #ifdef BACKPLANE_SUPPORT
        if (g_backplaneInterruptFlag) {
            g_backplaneInterruptFlag = false;
            uint8_t flags = bkp_read_programflags();
            if (xSemaphoreTake(cpuMutex, 10) == pdTRUE) {
                cpu.setProgramFlags(flags);
                xSemaphoreGive(cpuMutex);
            }
        }   
    #endif

    // ========================================================================
    // STOP-BUTTON HANDLING - HÖCHSTE PRIORITÄT
    // ========================================================================
    #ifdef USE_VERSION1
        if (g_switchInterruptFlag) {
            g_switchInterruptFlag = false;
            // Prüfe Stop-Taste
            // if (switches.isPressed(0x25,7)){
            //     Serial.println("[DEBUG] Readin");
            // }    


            if (switches.isPressed(0x25, 3)) {
                //Serial.println("[CORE 0] STOP interrupt triggered");
                g_cpuShouldStop = true;  // Signal an Core 1
                // Warte kurz auf Bestätigung
                for (int i = 0; i < 10 && g_cpuIsRunning; i++) {
                    delay(1);
                }
            }
        }
    #endif   

    // Hardware-Schalter verarbeiten (mit Mutex-Schutz)
    if (xSemaphoreTake(cpuMutex, 5) == pdTRUE) {
        cpu.handleSwitches();
        xSemaphoreGive(cpuMutex);
    }
    
    // Serial Kommandos verarbeiten
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input.length() == 0) return;
        
        char cmd = input.charAt(0);
        
        // Mutex für alle CPU-Operationen
        if (xSemaphoreTake(cpuMutex, 100) == pdTRUE) {
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
                                Serial.println("RIM-File loaded sucessfully!");
                                cpu.printStatus();
                            } else {
                                Serial.println("Error while loading Rim-File!");
                            }
                        } else {
                            Serial.println("Usage: l <filename.rim>");
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
                    Serial.println("CPU running on Core 1");
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
                        Serial.printf("\nMemory Dump from %04o:\n", start);
                        cpu.dumpMemory(start, start + 077);
                        Serial.println();
                    }
                    break;
                    
                case 'p':
                case 'P':
                    cpu.printStatus();
                    break;
                    
                case 'w':
                case 'W':
                    switches.printStatus();
                    break;
                    
                case 't':
                case 'T':
                    Serial.println("Start LED Test Pattern...");
                    leds.testPattern();
                    Serial.println("LED Test finished");
                    break;
                    
                case 'o':
                case 'O':
                    leds.allOff();
                    Serial.println("Alle LEDs off");
                    break;
                    
                case 'x':
                case 'X':
                    cpu.reset();
                    Serial.println("CPU Reset");
                    break;
                
                case 'e':
                case 'E':
                    {
                        bool newMode = !cpu.getExtendMode();
                        cpu.setExtendMode(newMode);
                        Serial.printf("Extend Mode: %s\n", newMode ? "ON" : "OFF");
                        Serial.printf("(Hardware Switch: %s)\n", 
                            switches.getExtendSwitch() ? "ON" : "OFF");
                    }
                    break;
                    
                case 'i':
                case 'I':
                    Serial.println("\n=== Performance Info ===");
                    Serial.printf("CPU Task on Core: 1\n");
                    Serial.printf("Loop on Core: %d\n", xPortGetCoreID());
                    Serial.printf("CPU Running: %s\n", g_cpuIsRunning ? "YES" : "NO");
                    Serial.printf("Instructions: %lu\n", g_instructionsExecuted);
                    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
                    Serial.printf("Memory: %d Banks x %d Words = %d KB\n", 
                        MEMORY_BANKS, BANK_SIZE, (EXTENDED_MEM_SIZE * 4) / 1024);
                    Serial.println("========================\n");
                    break;
                    
                case 'h':
                case 'H':
                    printHelp();
                    break;
                
                #ifdef BACKPLANE_SUPPORT    
                case 'b':
                case 'B':
                    testBackplane();
                    break;
                #endif

                #ifdef WEBSERVER_SUPPORT
                case 'a':
                case 'A':
                    testDisplay();
                    break;
                #endif

                default:
                    Serial.println("unknwon Command. 'h' für Hilfe.");
                    break;
            }
            xSemaphoreGive(cpuMutex);
        } else {
            Serial.println("CPU busy - Command ignored!");
        }
    }
    
    // Kurze Pause für Task-Scheduler
    delay(1);
}
