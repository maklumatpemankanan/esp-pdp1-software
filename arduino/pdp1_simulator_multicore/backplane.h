/*
MULTICORE-SAFE BACKPLANE.H
SPI-Zugriffe können von beiden Cores kommen
Optional: Mutex für SPI-Bus-Zugriff
*/

#ifdef USE_VERSION1
  #define BKP_CS    32
  #define BKP_INT   5
#elif defined(USE_VERSION2)
  #define BKP_CS    32
  #define BKP_INT   5
#endif

// MCP Port Expander
MCP23S17 bkp_mcp0(BKP_CS, 0); 
MCP23S17 bkp_mcp1(BKP_CS, 1); 
MCP23S17 bkp_mcp2(BKP_CS, 2); 
MCP23S17 bkp_mcp3(BKP_CS, 3); 
MCP23S17 bkp_mcp4(BKP_CS, 4); 
MCP23S17 bkp_mcp5(BKP_CS, 5); 
MCP23S17 bkp_mcp6(BKP_CS, 6); 

// ============================================================================
// MULTICORE: Optionaler SPI-Mutex für Backplane
// ============================================================================
// HINWEIS: Die MCP23S17-Bibliothek ist bereits SPI-safe, da sie 
// SPI.beginTransaction() / endTransaction() verwendet.
// Dieser Mutex ist nur nötig, wenn du eigene SPI-Operationen durchführst.

// Uncomment falls du eigene SPI-Operationen hast:
// static SemaphoreHandle_t backplaneSpiMutex = NULL;

//==============================================================================
// MCP FUNCTIONS
//==============================================================================

void bkp_set_io_output(){
  // HINWEIS: Diese Funktion ist SPI-safe durch MCP23S17-Bibliothek
  bkp_mcp0.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp0.portMode(MCP23S17_PORTB, 0xFF);
  bkp_mcp1.pinMode(8, MCP23S17_OUTPUT);
  bkp_mcp1.pinMode(9, MCP23S17_OUTPUT);
}

void bkp_set_io_input(){
  bkp_mcp0.portMode(MCP23S17_PORTA, 0x00);
  bkp_mcp0.portMode(MCP23S17_PORTB, 0x00);
  bkp_mcp1.pinMode(8, MCP23S17_INPUT);
  bkp_mcp1.pinMode(9, MCP23S17_INPUT);
}

void bkp_set_io_value(uint32_t value){
  bkp_set_io_output();
  
  // Reverse bits 0-7 für Port B
  uint8_t portB = value & 0xFF;
  portB = ((portB & 0xF0) >> 4) | ((portB & 0x0F) << 4);
  portB = ((portB & 0xCC) >> 2) | ((portB & 0x33) << 2);
  portB = ((portB & 0xAA) >> 1) | ((portB & 0x55) << 1);
  
  // Reverse bits 8-15 für Port A
  uint8_t portA = (value >> 8) & 0xFF;
  portA = ((portA & 0xF0) >> 4) | ((portA & 0x0F) << 4);
  portA = ((portA & 0xCC) >> 2) | ((portA & 0x33) << 2);
  portA = ((portA & 0xAA) >> 1) | ((portA & 0x55) << 1);
  
  bkp_mcp0.writePort(MCP23S17_PORTA, portA);
  bkp_mcp0.writePort(MCP23S17_PORTB, portB);
  
  // mcp1: bits 16-17 an pins 8-9
  uint8_t mcp1_portb = (value >> 16) & 0x03;
  uint8_t current = bkp_mcp1.readPort(MCP23S17_PORTB);
  bkp_mcp1.writePort(MCP23S17_PORTB, (current & 0xFC) | mcp1_portb);
}

uint32_t bkp_read_io_value(){
  bkp_set_io_input();

  uint32_t value = 0;
  
  // Port B lesen: bits 0-7
  uint8_t portB = bkp_mcp0.readPort(MCP23S17_PORTB);
  portB = ((portB & 0xF0) >> 4) | ((portB & 0x0F) << 4);
  portB = ((portB & 0xCC) >> 2) | ((portB & 0x33) << 2);
  portB = ((portB & 0xAA) >> 1) | ((portB & 0x55) << 1);
  value |= portB;
  
  // Port A lesen: bits 8-15
  uint8_t portA = bkp_mcp0.readPort(MCP23S17_PORTA);
  portA = ((portA & 0xF0) >> 4) | ((portA & 0x0F) << 4);
  portA = ((portA & 0xCC) >> 2) | ((portA & 0x33) << 2);
  portA = ((portA & 0xAA) >> 1) | ((portA & 0x55) << 1);
  value |= ((uint32_t)portA << 8);
  
  // mcp1 Port B: bits 16-17
  uint8_t mcp1_portB = bkp_mcp1.readPort(MCP23S17_PORTB);
  uint8_t io_bits = mcp1_portB & 0x03;
  value |= ((uint32_t)io_bits << 16);
  
  return value;
}

void bkp_set_ac_value(uint16_t value){
  // Port A: ac06-ac11
  uint8_t portA_ac = (value >> 6) & 0x3F;
  portA_ac = portA_ac << 2;
  
  // Port B: ac00-ac05
  uint8_t portB_ac = value & 0x3F;
  portB_ac = ((portB_ac & 0x20) >> 5) | ((portB_ac & 0x10) >> 3) | 
             ((portB_ac & 0x08) >> 1) | ((portB_ac & 0x04) << 1) | 
             ((portB_ac & 0x02) << 3) | ((portB_ac & 0x01) << 5);
  portB_ac = portB_ac << 2;
  
  uint8_t current_portA = bkp_mcp1.readPort(MCP23S17_PORTA);
  uint8_t current_portB = bkp_mcp1.readPort(MCP23S17_PORTB);
  
  portA_ac = (current_portA & 0x03) | portA_ac;
  portB_ac = (current_portB & 0x03) | portB_ac;
  
  bkp_mcp1.writePort(MCP23S17_PORTA, portA_ac);
  bkp_mcp1.writePort(MCP23S17_PORTB, portB_ac);
}

void bkp_set_address(int device, int status){
  int mcp_index = device / 16;
  int local_addr = device % 16;
  
  uint8_t pin_value = 0;
  uint8_t port;
  
  if(local_addr < 8){
    pin_value = status ? (1 << local_addr) : 0x00;
    port = MCP23S17_PORTB;
  } else {
    int reversed_bit = 15 - local_addr;
    pin_value = status ? (1 << reversed_bit) : 0x00;
    port = MCP23S17_PORTA;
  }
  
  switch(mcp_index){
    case 0: bkp_mcp3.writePort(port, pin_value); break;
    case 1: bkp_mcp4.writePort(port, pin_value); break;
    case 2: bkp_mcp5.writePort(port, pin_value); break;
    case 3: bkp_mcp6.writePort(port, pin_value); break;
  }
}

void bkp_send_ack(){
  bkp_mcp2.digitalWrite(8, HIGH);
  delayMicroseconds(20);
  bkp_mcp2.digitalWrite(8, LOW);  
}

uint8_t bkp_read_programflags(){
  uint8_t portB_value = bkp_mcp2.readPort(MCP23S17_PORTB);
  portB_value = (portB_value >> 2) & 0x3F;
  return portB_value & 0x3F;
}

void testBackplane(){
  // MULTICORE-HINWEIS: Diese Funktion sollte nur von Core 0 aufgerufen werden
  // (über Serial-Kommando 'b')
  Serial.println("[BACKPLANE] Running test pattern...");
  
  int i = 1;
  for(int a = 0; a < 18; a++){
    bkp_set_io_value(i);
    if(a < 12){
      bkp_set_ac_value(i);
    } else {
      bkp_set_ac_value(0);
    }
    i = i << 1;
    delay(50);
  }
  
  Serial.println("[BACKPLANE] Test completed");
}

void bkp_mcp_init(){
  Serial.println("[BACKPLANE] Initializing backplane support...");
  
  // Optional: SPI-Mutex erstellen
  // backplaneSpiMutex = xSemaphoreCreateMutex();
  
  Serial.print("[BACKPLANE] init bkp_mcp0 ");
  if (bkp_mcp0.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp1 ");
  if (bkp_mcp1.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp2 ");
  if (bkp_mcp2.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp3 ");
  if (bkp_mcp3.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp4 ");
  if (bkp_mcp4.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp5 ");
  if (bkp_mcp5.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  Serial.print("[BACKPLANE] init bkp_mcp6 ");
  if (bkp_mcp6.begin()) {
    Serial.println("ok");
  } else {
    Serial.println("error");
  }
  
  // Port-Modi konfigurieren
  bkp_mcp0.portMode(MCP23S17_PORTA, 0xFF); 
  bkp_mcp0.portMode(MCP23S17_PORTB, 0xFF);
  bkp_mcp1.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp1.portMode(MCP23S17_PORTB, 0xFF);
  
  // mcp2: Port A output, Port B gemischt
  bkp_mcp2.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp2.portMode(MCP23S17_PORTB, 0x01);  // 0=ack output, rest input
  
  // Interrupt-Konfiguration für Port B
  bkp_mcp2.setPortInterrupts(MCP23S17_PORTB, 0xFC, MCP23S17_INT_CHANGE);
  bkp_mcp2.setInterruptMirror(false);
  bkp_mcp2.setInterruptPolarity(false);

  bkp_mcp3.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp3.portMode(MCP23S17_PORTB, 0xFF);
  bkp_mcp4.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp4.portMode(MCP23S17_PORTB, 0xFF);
  bkp_mcp5.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp5.portMode(MCP23S17_PORTB, 0xFF);
  bkp_mcp6.portMode(MCP23S17_PORTA, 0xFF);
  bkp_mcp6.portMode(MCP23S17_PORTB, 0xFF);

  // Alle Decoder-MCPs auf LOW
  bkp_mcp3.writePort(MCP23S17_PORTA, 0x00);
  bkp_mcp3.writePort(MCP23S17_PORTB, 0x00);
  bkp_mcp4.writePort(MCP23S17_PORTA, 0x00);
  bkp_mcp4.writePort(MCP23S17_PORTB, 0x00);
  bkp_mcp5.writePort(MCP23S17_PORTA, 0x00);
  bkp_mcp5.writePort(MCP23S17_PORTB, 0x00);
  bkp_mcp6.writePort(MCP23S17_PORTA, 0x00);
  bkp_mcp6.writePort(MCP23S17_PORTB, 0x00);

  bkp_set_ac_value(0);
  bkp_set_io_value(0);
  
  Serial.println("[BACKPLANE] Initialization complete");
}

// ============================================================================
// MULTICORE HINWEISE
// ============================================================================
/*
 * Die Backplane-Funktionen sind grundsätzlich thread-safe, da:
 * 
 * 1. Die MCP23S17-Bibliothek bereits SPI.beginTransaction() / endTransaction() 
 *    verwendet, was Hardware-Locking macht
 * 
 * 2. Die meisten Backplane-Zugriffe kommen nur von Core 0:
 *    - Interrupt-Handling (g_backplaneInterruptFlag)
 *    - testBackplane() über Serial-Kommando
 * 
 * ABER VORSICHT:
 * - Falls du bkp_set_ac_value() oder bkp_set_io_value() von der CPU (Core 1)
 *   aufrufen willst, dann solltest du einen Mutex hinzufügen!
 * 
 * Beispiel mit Mutex:
 * 
 *   void bkp_set_io_value_safe(uint32_t value) {
 *       if (backplaneSpiMutex && xSemaphoreTake(backplaneSpiMutex, 10) == pdTRUE) {
 *           bkp_set_io_value(value);
 *           xSemaphoreGive(backplaneSpiMutex);
 *       }
 *   }
 * 
 * Aktuell: Alle Backplane-Zugriffe nur von Core 0 → kein Mutex nötig
 */
