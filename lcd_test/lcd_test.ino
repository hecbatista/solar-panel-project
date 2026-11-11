// 1602 LCD wiring test (16x2 parallel LCD, 4-bit mode).
// Wiring: RS -> D7, E -> D6, D4 -> D5, D5 -> D4, D6 -> D3, D7 -> D2,
//         VSS, RW, K -> GND;  VDD -> 5V;  A -> 5V through 220 ohm;
//         V0 -> GND with a plain jumper (full contrast, no potentiometer).
// Text too dark or solid black blocks: put a 220 ohm or 1k resistor in that jumper's place.

#include <LiquidCrystal.h>

LiquidCrystal lcd(7, 6, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

void setup() {
  lcd.begin(16, 2);
  lcd.print(F("LCD test OK"));
}

void loop() {
  lcd.setCursor(0, 1);
  lcd.print(F("Uptime "));
  lcd.print(millis() / 1000);
  lcd.print(F(" s"));
  delay(250);
}
