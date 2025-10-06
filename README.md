# esp-pdp1<br>
PDP-1 Simulator on ESP32<br>
<h2>Hardware Components</h2>

ESP32 Microcontroller (520KB RAM)<br>
SD Card (SPI) for program storage<br>
8x MCP23S17 Port Expanders (SPI) for LED display<br>
7x PCF8574 I²C Port Expanders for switch inputs<br>
TCA9548A I²C Multiplexer<br>
<br><br>
<h2>Implemented PDP-1 Architecture</h2>
Registers (all 18-bit)<br>
<br><br>
AC (Accumulator) - Main arithmetic register<br>
IO (In-Out Register) - I/O and secondary arithmetic register<br>
PC (Program Counter) - 12-bit program counter<br>
MA (Memory Address) - Currently addressed memory location<br>
MB (Memory Buffer) - Read/written memory content<br>
OV (Overflow Flag)<br>
PF[1-6] (Program Flags) - 6 software flags<br>
<br><br>
</h2>Memory</h2>
4096 words (0000-7777 octal)<br>
18-bit per word<br>
1's Complement arithmetic<br>
<br>
<h2>Implemented Instructions</h2>  
Memory Reference Instructions (Opcodes 00-56 octal, even)
<br>
AND (00) - Logical AND<br>
IOR (02) - Logical OR<br>
XOR (04) - Exclusive OR<br>
XCT (06) - Execute instruction<br>
CAL (16) - Call Subroutine at 100<br>
JDA (17) - Jump and Deposit AC<br>
LAC (20) - Load AC<br>
LIO (22) - Load IO<br>
DAC (24) - Deposit AC<br>
DAP (26) - Deposit Address Part<br>
DIP (30) - Deposit Instruction Part<br>
DIO (32) - Deposit IO<br>
DZM (34) - Deposit Zero in Memory<br>
ADD (40) - Addition with overflow detection<br>
SUB (42) - Subtraction with overflow detection<br>
IDX (44) - Index (increment and load)<br>
ISP (46) - Index and Skip if Positive<br>
SAD (50) - Skip if AC Different<br>
SAS (52) - Skip if AC Same<br>
MUL (54) - Multiply (36-bit result in AC:IO)<br>
DIV (56) - Divide (quotient in AC, remainder in IO)<br>
<br>
Jump Instructions<br>
<br>
JMP (60) - Unconditional Jump<br>
JSP (62) - Jump and Save PC<br>
<br>
Skip Group (64)<br>
<br>
SZA - Skip if AC = Zero<br>
SPA - Skip if AC Plus (≥0)<br>
SMA - Skip if AC Minus (<0)<br>
SPI - Skip if IO Plus<br>
SZO - Skip if Overflow Zero (and clear overflow)<br>
SZF - Skip if Program Flag Zero<br>
SZS - Skip if Sense Switch Zero<br>
With I-Bit: All skip conditions inverted<br>
<br>
Shift/Rotate Group (66, 67)<br>
<br>
RAL/RAR - Rotate AC Left/Right<br>
RIL/RIR - Rotate IO Left/Right<br>
RCL/RCR - Rotate Combined AC:IO Left/Right<br>
SAL/SAR - Shift AC Left/Right (arithmetic)<br>
SIL/SIR - Shift IO Left/Right<br>
SCL/SCR - Shift Combined AC:IO Left/Right<br>
Shift count determined by number of set bits in bits 9-17<br>
<br>
Operate Group (76)<br>
<br>
CLA - Clear AC<br>
CMA - Complement AC (1's Complement)<br>
CLI - Clear IO<br>
HLT - Halt<br>
LAP - Load AC with PC<br>
LAT - Load AC from Test Word<br>
CLF/STF - Clear/Set Program Flags (1-6 or all with 7)<br>
NOP - No Operation<br>
<br>
LAW (70, 71)<br>
<br>
LAW - Load AC with immediate value (0-4095)<br>
LAW I - Load AC with negated immediate<br>
<br>
I/O Transfer Group (72)<br>
<br>
TYO (720003) - Type Out (Typewriter output, FIODEC)<br>
TYI (720004) - Type In (Typewriter input)<br>
Additional IOT commands can be defined<br>
<br>
RIM Loader<br>
Features<br>
<br>
Loads programs in RIM format from SD card<br>
Recognizes RIM loader (bootstrap code starting at 7751)<br>
Re-synchronization after RIM loader end<br>
Automatic start address detection<br>
Correct 6-bit FIODEC byte decoding<br>
<br>
Folder-based Program Management<br>
<br>
12 folders on SD card (/0 through /12)<br>
Sense Switches (6-bit) select folder<br>
READ IN button loads program from selected folder<br>
Automatic detection of first .rim file in folder<br>
<br>
Console Switches (Hardware)<br>
Power & Mode<br>
<br>
Power Switch - System On/Off with random LED pattern on power-up<br>
Single Step Switch - Activate Step-Mode (one instruction per START/CONTINUE)<br>
Single Instruction - Execute single instruction (in stop mode)<br>
<br>
Program Control<br>
<br>
START DOWN - Start from current PC address<br>
START UP - Start from Address Switches position<br>
STOP - Stop program execution immediately (after current instruction)<br>
CONTINUE - Resume stopped program (in Step-Mode: next instruction)<br>
<br>
Memory Operations<br>
<br>
EXAMINE - Display memory content at Address Switches position (MA/MB LEDs)<br>
DEPOSIT - Write Test Word to Address Switches position<br>
READ IN - Load RIM program from folder (selected via Sense Switches)<br>
<br>
Inputs<br>
<br>
Address Switches (16-bit) - Memory address for EXAMINE/DEPOSIT/START UP<br>
Test Word Switches (18-bit) - Data value for DEPOSIT or LAT instruction<br>
Sense Switches (6-bit) - Software-readable via SZS, selects folder for READ IN<br>
<br>
LED Display<br>
Register LEDs (18-bit)<br>
<br>
AC - Accumulator content<br>
IO - In-Out Register content<br>
MB - Memory Buffer (read/written value)<br>
<br>
Address LEDs (12-16 bit)<br>
<br>
PC - Program Counter (displayed as 16-bit)<br>
MA - Memory Address (displayed as 16-bit)<br>
<br>
Status LEDs<br>
<br>
INSTR - Current opcode (5 bits)<br>
Program Flags (6 LEDs) - PF[1] through PF[6]<br>
Sense Switches (6 LEDs) - Current switch state<br>
Overflow - Overflow flag status<br>
Power - System powered on<br>
Run - Program running<br>
Step - Single-Step mode active<br>
<br>
Special Features<br>
<br>
Random bit pattern on power-up (like the original)<br>
Remains until first switch operation<br>
<br>
FIODEC Character Encoding<br>
Implemented Characters<br>
<br>
Letters a-z (lowercase)<br>
Digits 0-9<br>
Special characters: Space, ', ", /, =, +, -, (, ), <, >, ~, ^, _, ]<br>
Control characters: Carriage Return, Backspace<br>
<br>
I/O Operations<br>
<br>
TYO - Output character (6-bit FIODEC from IO register)<br>
TYI - Input character (sets Program Flag 1)<br>
<br>
Operating Modes<br>
Normal Mode<br>
<br>
Continuous execution at full speed<br>
~1000 instructions per loop iteration<br>
STOP switch stops immediately after current instruction<br>
<br>
Single Step Mode<br>
<br>
One instruction per START/CONTINUE<br>
Ideal for debugging<br>
LEDs show state after each instruction<br>
<br>
Examine/Deposit Mode<br>
<br>
Manual memory read/write<br>
Independent of program state<br>
No PC modification<br>
<br>
Performance<br>
<br>
Instruction Execution: ~1000 instructions/loop in Normal Mode<br>
I/O Response: Immediate switch reaction (STOP within 1 instruction)<br>
LED Update: After each significant operation<br>
SD-Card Loading: Program load in <1 second<br>
