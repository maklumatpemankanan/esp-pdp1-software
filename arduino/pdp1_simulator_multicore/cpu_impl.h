#ifndef CPU_IMPL_H
#define CPU_IMPL_H

// Forward declarations für Webserver-Funktionen
#ifdef WEBSERVER_SUPPORT
    extern void sendTypewriterChar(uint8_t ch);
    extern void handleDisplayOutput(int16_t x, int16_t y, uint8_t intensity);
    extern void sendPunchData(uint8_t byte); 
    extern bool isWebTapeMounted();
    extern std::vector<uint8_t> getWebTapeData();
    extern void sendReaderPosition(size_t position);
#endif

// Implementation of PDP1 methods that are too complex for inline

void PDP1::handleSwitches() {
    if (!switches) return;
    
    switches->update();
    
    // Power Switch
    if (switches->getPower() && !powerOn) {
        powerOn = true;
        Serial.println("Power ON");
        showRandomLEDs = true;
        if (leds) {
            leds->showRandomPattern();
            
            uint32_t randomAC = random(0, 0777777);
            uint32_t randomIO = random(0, 0777777);
            uint16_t randomPC = random(0, 07777);
            uint16_t randomMA = random(0, 07777);
            uint32_t randomMB = random(0, 0777777);
            uint32_t randomInstr = random(0, 037) << 13;
            uint8_t randomPF = random(0, 64);
            
            leds->updateDisplay(randomAC, randomIO, randomPC, randomMA, randomMB, randomInstr, false, randomPF, 0, true, false, false);
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
    
    bool singleStepMode = switches->getSingleStep();
    
    if (switches->getStopPressed()) {
        running = false;
        halted = true;
        showRandomLEDs = false;
        if (leds) leds->clearRandomPattern();
        Serial.println("STOP pressed");
    }
    
    if (switches->getStartDownPressed()) {
        if (singleStepMode) {
            step();
            Serial.printf("STEP: PC=%04o AC=%06o\n", PC, AC);
            stepModeStop = false;
        } else {
            running = true;
            halted = false;
            showRandomLEDs = false;
            if (leds) leds->clearRandomPattern();
            Serial.printf("START from PC=%04o\n", PC);
        }
    }
    
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
            showRandomLEDs = false;
            if (leds) leds->clearRandomPattern();
            Serial.printf("START from Address Switches: %04o\n", PC);
        }
    }
    
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
                showRandomLEDs = false;
                if (leds) leds->clearRandomPattern();
                Serial.println("CONTINUE");
            }
        }
    }
    
    if (switches->getExaminePressed()) {
        showRandomLEDs = false;
        if (leds) leds->clearRandomPattern();
        examineAddress = switches->getAddressSwitches() & ADDR_MASK;
        MA = examineAddress;
        MB = readMemory(examineAddress);
        Serial.printf("EXAMINE: Addr=%04o Data=%06o\n", examineAddress, MB);
    }
    
    if (switches->getDepositPressed()) {
        showRandomLEDs = false;
        if (leds) leds->clearRandomPattern();
        uint16_t addr = switches->getAddressSwitches() & ADDR_MASK;
        uint32_t data = switches->getTestWord();
        writeMemory(addr, data);
        Serial.printf("DEPOSIT: Addr=%04o Data=%06o\n", addr, data);
    }

    if (switches->getReadInPressed()) {
        Serial.println("READ IN pressed");
        
        showRandomLEDs = false;
        if (leds) leds->clearRandomPattern();

        #ifdef WEBSERVER_SUPPORT
            if (isWebTapeMounted()) {
                Serial.println("[READ IN] Loading from WEB TAPE...");
                
                // Hole die gemounteten Tape-Daten
                std::vector<uint8_t> tapeData = getWebTapeData();
                
                // Lade mit loadFromArray!
                reset();
                uint16_t startPC = 0;
                if (RIMLoader::loadFromArray(tapeData.data(), tapeData.size(), memory, startPC)) {
                    PC = startPC;
                    Serial.println("[READ IN] Loaded from web tape!");
                    updateLEDs();
                } else {
                    Serial.println("[READ IN] Failed to load from web tape!");
                }
                return;  // WICHTIG: Nicht zu SD-Karte fallen!
            } else {
                Serial.println("[READ IN] Loading from SD CARD...");
            }
        #endif
        
        // Fallback: SD-Karte
        uint8_t senseValue = switches->getSenseSwitches();
        String filename = RIMLoader::getRIMFileFromFolder(senseValue);
        if (filename.length() > 0) {
            if (loadRIM(filename.c_str())) {
                Serial.printf("[READ IN] Loaded: %s\n", filename.c_str());
            }
        } else {
            Serial.println("[READ IN] No file found for sense switches");
        }
    }
    // if (switches->getReadInPressed()) {
    //     uint8_t senseSwitches = switches->getSenseSwitches();
    //     uint8_t folderNumber = senseSwitches & 0x0F;
        
    //     if (folderNumber > 12) {
    //         Serial.printf("READ IN: Ungültiger Ordner %d (Sense Switches: %02o)\n", 
    //                      folderNumber, senseSwitches);
    //         return;
    //     }
        
    //     Serial.printf("\nREAD IN: Lade aus Ordner %d (Sense Switches: %02o)\n", 
    //                  folderNumber, senseSwitches);
        
    //     String rimFile = RIMLoader::getRIMFileFromFolder(folderNumber);
        
    //     if (rimFile.length() > 0) {
    //         reset();
    //         uint16_t startPC = 0;
    //         if (RIMLoader::loadFromSD(rimFile.c_str(), memory, startPC)) {
    //             PC = startPC;
    //             Serial.printf("Programm geladen und bereit bei PC=%04o\n", PC);
    //             updateLEDs();
    //             showRandomLEDs = false;
    //             if (leds) leds->clearRandomPattern();
    //         }
    //     }
    // }
    
    if (switches->getSingleInstrPressed() && !running) {
        showRandomLEDs = false;
        if (leds) leds->clearRandomPattern();
        step();
        Serial.printf("SINGLE INSTR: PC=%04o AC=%06o\n", PC, AC);
    }

    if (!showRandomLEDs && powerOn) {
        updateLEDs();
    }
}

void PDP1::executeInstruction() {
    uint32_t instruction = readMemory(PC);
    
    uint8_t opField = (instruction >> 12) & 077;
    bool indirect = (opField & 1);
    uint8_t opcode = opField & 076;
    uint16_t Y = instruction & 07777;
    
    PC = (PC + 1) & ADDR_MASK;
    cycles++;
    
    if (opcode <= 056 && (opcode & 1) == 0) {
        executeMemoryReference(instruction, opcode, indirect, Y);
    }
    else if (opcode == 060) {
        if (indirect) Y = readMemory(Y) & ADDR_MASK;
        PC = Y;
    }
    else if (opcode == 062) {
        if (indirect) Y = readMemory(Y) & ADDR_MASK;
        AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
        PC = Y;
    }
    else if (opcode == 064) {
        executeSkip(instruction);
    }
    else if (opcode == 066 || opcode == 067) {
        executeShift(instruction);
    }
    else if (opcode == 070) {
        AC = indirect ? (Y ^ WORD_MASK) : Y;
    }
    else if (opcode == 072) {
        executeIOT(instruction);
    }
    else if (opcode == 076) {
        executeOperate(instruction);
    }
    
    updateLEDs();
}

void PDP1::executeMemoryReference(uint32_t instruction, uint8_t opcode, bool indirect, uint16_t Y) {
    if (indirect) {
        Y = readMemory(Y) & ADDR_MASK;
    }
    
    uint32_t memValue;
    int32_t result;
    
    switch(opcode) {
        case 002:  // AND
            AC &= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 004:  // IOR
            AC |= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 006:  // XOR
            AC ^= readMemory(Y);
            AC &= WORD_MASK;
            break;
            
        case 010:  // XCT
            {
                uint16_t savedPC = PC;
                uint32_t targetInstr = readMemory(Y);
                PC = Y;
                executeInstruction();
                PC = savedPC;
            }
            break;
            
        case 016:  // CAL
            writeMemory(0100, AC);
            AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
            PC = 0101;
            break;
            
        case 017:  // JDA
            writeMemory(Y, AC);
            AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
            PC = (Y + 1) & ADDR_MASK;
            break;
            
        case 020:  // LAC
            AC = readMemory(Y);
            break;
            
        case 022:  // LIO
            IO = readMemory(Y);
            break;
            
        case 024:  // DAC
            writeMemory(Y, AC);
            break;
            
        case 026:  // DAP
            memValue = readMemory(Y);
            memValue = (memValue & ~ADDR_MASK) | (AC & ADDR_MASK);
            writeMemory(Y, memValue);
            break;
            
        case 030:  // DIP
            memValue = readMemory(Y);
            memValue = (memValue & ADDR_MASK) | (AC & ~ADDR_MASK);
            writeMemory(Y, memValue);
            break;
            
        case 032:  // DIO
            writeMemory(Y, IO);
            break;
            
        case 034:  // DZM
            writeMemory(Y, 0);
            break;
            
        case 040:  // ADD
            result = onesCompToSigned(AC) + onesCompToSigned(readMemory(Y));
            if (result > 0777777 || result < -0777777) OV = true;
            AC = signedToOnesComp(result);
            break;
            
        case 042:  // SUB
            result = onesCompToSigned(AC) - onesCompToSigned(readMemory(Y));
            if (result > 0777777 || result < -0777777) OV = true;
            AC = signedToOnesComp(result);
            break;
            
        case 044:  // IDX
            memValue = readMemory(Y);
            memValue = (memValue + 1) & WORD_MASK;
            //if (memValue == 0) memValue = WORD_MASK;
            writeMemory(Y, memValue);
            AC = memValue;
            break;
            
        case 046:  // ISP
            memValue = readMemory(Y);
            memValue = (memValue + 1) & WORD_MASK;
            //if (memValue == 0) memValue = WORD_MASK;
            writeMemory(Y, memValue);
            AC = memValue;
            if (!(memValue & SIGN_BIT)) {
                PC = (PC + 1) & ADDR_MASK;
            }
            break;
            
        case 050:  // SAD
            if (AC != readMemory(Y)) {
                PC = (PC + 1) & ADDR_MASK;
                // Serial.print("SAD PC: ");
                // Serial.println(PC ,OCT);
            }
            break;
            
        case 052:  // SAS
            if (AC == readMemory(Y)) {
                PC = (PC + 1) & ADDR_MASK;
            }
            break;
            
        case 054:  // MUS
            {
                int32_t multiplier = onesCompToSigned(readMemory(Y));
                int32_t multiplicand = onesCompToSigned(AC);
                int64_t product = (int64_t)multiplier * (int64_t)multiplicand;
                
                if (product >= 0) {
                    AC = (product >> 18) & WORD_MASK;
                    IO = product & WORD_MASK;
                } else {
                    product = -product;
                    AC = ((product >> 18) ^ WORD_MASK) & WORD_MASK;
                    IO = (product ^ WORD_MASK) & WORD_MASK;
                }
            }
            break;
            
        case 056:  // DIS
            {
                int32_t divisor = onesCompToSigned(readMemory(Y));
                if (divisor == 0) {
                    OV = true;
                    break;
                }
                
                int64_t dividend = ((int64_t)onesCompToSigned(AC) << 18) | 
                                  (int64_t)onesCompToSigned(IO);
                int64_t quotient = dividend / divisor;
                int64_t remainder = dividend % divisor;
                
                if (quotient > 0777777 || quotient < -0777777) {
                    OV = true;
                } else {
                    AC = signedToOnesComp((int32_t)quotient);
                    IO = signedToOnesComp((int32_t)remainder);
                }
            }
            break;
    }
}

void PDP1::executeOperate(uint32_t instruction) {
    uint32_t bits = instruction & 07777;
    
    if (bits & 0200) {
        AC = 0;
    }
    
    if (bits & 01000) {
        AC = AC ^ WORD_MASK;
    }
    
    if (bits & 04000) {
        IO = 0;
    }
    
    if (bits & 0400) {
        halted = true;
        running = false;
        Serial.println("\n*** PDP-1 HALTED ***");
        Serial.printf("Final AC=%06o IO=%06o PC=%04o Cycles=%lu\n\n", AC, IO, PC, cycles);
    }
    
    if (bits & 0100) {
        AC = (PC & ADDR_MASK) | (OV ? 0400000 : 0);
    }
    
    if (bits & 02200) {
        if (switches) {
            AC = switches->getTestWord();
        }
    }
    
    int flagNum = bits & 07;
    
    if (bits & 0020) {
        if (flagNum == 7) {
            memset(PF, 0, sizeof(PF));
        } else if (flagNum >= 1 && flagNum <= 6) {
            PF[flagNum] = false;
        }
    }
    
    if (bits & 0010) {
        if (flagNum == 7) {
            memset(PF, 1, sizeof(PF));
        } else if (flagNum >= 1 && flagNum <= 6) {
            PF[flagNum] = true;
        }
    }
}

void PDP1::executeSkip(uint32_t instruction) {
    bool shouldSkip = false;
    bool invert = instruction & I_BIT;
    uint32_t bits = instruction & 07777;
    
    if (bits & 0100) {
        shouldSkip |= (AC == 0);
    }
    
    if (bits & 0200) {
        shouldSkip |= ((AC & SIGN_BIT) == 0);
    }
    
    if (bits & 0400) {
        shouldSkip |= (AC & SIGN_BIT);
    }
    
    if (bits & 02000) {
        shouldSkip |= ((IO & SIGN_BIT) == 0);
    }
    
    if (bits & 01000) {
        shouldSkip |= !OV;
        OV = false;
    }
    
    if (bits & 0007) {
        int flagNum = bits & 07;
        if (flagNum == 7) {
            bool allClear = true;
            for (int i = 1; i <= 6; i++) {
                if (PF[i]) allClear = false;
            }
            shouldSkip |= allClear;
        } else if (flagNum >= 1 && flagNum <= 6) {
            shouldSkip |= !PF[flagNum];
        }
    }
    
    if (bits & 0070) {
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

void PDP1::executeShift(uint32_t instruction) {
    int count = 0;
    for (int i = 0; i < 9; i++) {
        if (instruction & (1 << i)) count++;
    }
    
    uint8_t opField = (instruction >> 12) & 077;
    uint8_t baseOp = opField & 076;
    
    bool left = (baseOp == 066);
    bool arith = (baseOp == 067 || baseOp == 065);
    uint8_t reg = (instruction >> 9) & 03;
    
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
}

void PDP1::executeIOT(uint32_t instruction) {
    uint8_t device = instruction & 077;

    switch(device) {
        // ====================================================================
        // 730002: rpb - Read Paper Binary
        // Liest 18 Bit vom virtuellen Paper Tape in IO
        // ====================================================================
        case 002:
            IO = RIMLoader::readPaperBinary();
            //Serial.print("730002 : IO ");
            //Serial.println(IO, OCT);
            break;
            
        // ====================================================================
        // 730003: Typewriter Output
        // ====================================================================
        case 003:
            {
                uint8_t fiodec = IO & 077;
                char ch = fiodecToAscii(fiodec);
                Serial.print(ch);
                typewriter_buffer += ch;
                #ifdef WEBSERVER_SUPPORT
                    sendTypewriterChar(ch);
                #endif
            }
            break;
            
        // ====================================================================
        // 730004: Typewriter Input (Keyboard)
        // ====================================================================
        case 004:
            if (Serial.available()) {
                char ch = Serial.read();
                IO = asciiToFiodec(ch) << 12;
                PF[1] = true;
            }
            break;

        // ====================================================================
        // 720006: PPB - Punch Paper Binary
        // ====================================================================
        case 006:  // ppb
            {
                
                //uint8_t dataBits = IO & 0x3F;           // IO Bits 0-5 extrahieren
                uint8_t dataBits = (IO >> 12) & 0x3F;   // Obere 6 Bits (Bits 12-17)

                // Paper Tape Format: Bits 0-5 = Daten, Bit 7 = Sprocket
                uint8_t tapeByte = dataBits | 0x80;
                
                #ifdef WEBSERVER_SUPPORT
                    sendPunchData(tapeByte);
                    //Serial.println(tapeByte, BIN);
                #endif
            }
            break;

        // ====================================================================
        // 730007: Display Output
        // ====================================================================
        #ifdef WEBSERVER_SUPPORT
            case 007:
            {
                uint8_t intensity = 7;  // Maximum Helligkeit

                // X aus AC
                int16_t pdp_x = (AC & 0x3FF);
                if (pdp_x >= 512) pdp_x -= 1024;

                // Y aus IO
                int16_t pdp_y = (IO & 0x3FF);
                if (pdp_y >= 512) pdp_y -= 1024;

                handleDisplayOutput(pdp_x, pdp_y, intensity);            
            }
            break;
        #endif    

        // ====================================================================
        // 730012: Test Output Device (Backplane)
        // ====================================================================
        #ifdef BACKPLANE_SUPPORT    
        case 012:
            {
                bkp_set_address(device, 1);
                bkp_set_ac_value(AC);
                bkp_set_io_value(IO);
                //Serial.println(IO, OCT);
                bkp_set_address(device, 0);
            }
            break;
        #endif
    }
}

void PDP1::step() {
    // MULTICORE: Prüfe externes Stop-Flag SOFORT
    if (externalStopFlag && *externalStopFlag) {
        running = false;
        halted = true;
        *externalStopFlag = false;
        return;
    }
    
    if (!halted) {
        executeInstruction();
    }   
   
    if (!halted) {
        executeInstruction();
    }
    
    if (switches && switches->getStop()) {
        running = false;
        halted = true;
        //Serial.println("\nSTOP while running");
    }
    
    if (switches && switches->getSingleStep()) {
        running = false;
        stepModeStop = true;
    }
}

void PDP1::loadLEDTestProgram() {
    const uint32_t ledTest[] = {
        0600000, 0601000, 0640001, 0260450, 0050450, 0671001, 0260450, 0050450,
        0340435, 0050450, 0150452, 0640100, 0000404, 0640001, 0260450, 0050450,
        0671001, 0260450, 0160450, 0340435, 0050450, 0150452, 0640100, 0000417,
        0777777, 0160452, 0340435, 0340435, 0340435, 0260435, 0650000, 0260451,
        0040451, 0000440, 0010435, 0777777, 0260450, 0050450, 0661001, 0260450,
        0050450, 0340435, 0050450, 0640100, 0000445, 0777777, 0260450, 0050450,
        0661001, 0260450, 0160450, 0340435, 0050450, 0640100, 0000457, 0600000,
        0601000, 0340435, 0340435, 0340435, 0000400, 0000000, 0000000, 0777777
    };
    
    reset();
    
    for (size_t i = 0; i < sizeof(ledTest)/sizeof(ledTest[0]); i++) {
        writeMemory(0400 + i, ledTest[i]);
    }
    
    PC = 0400;
    updateLEDs();
    
    Serial.println("\n*** LED Test Programm loaded ***");
    Serial.println("Programm startet bei Adresse 0400");
    Serial.println("Funktion: Füllt AC/IO bitweise, dann leert sie");
    Serial.println("Benutze 'r' zum Starten oder START-Schalter\n");
}

#endif // CPU_IMPL_H
