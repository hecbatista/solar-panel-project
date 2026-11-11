// DS3231 RTC test for the Uno clone.
// Wiring: VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5.
// Open the serial monitor at 9600 baud.

#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println(F("DS3231 test starting"));

  Wire.begin();
  Wire.beginTransmission(0x68);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("No device at I2C address 0x68. Check wiring: SDA->A4, SCL->A5, VCC->5V, GND->GND."));
    while (true) {}
  }

  if (!rtc.begin()) {
    Serial.println(F("RTC found on I2C but rtc.begin() failed."));
    while (true) {}
  }
  Serial.println(F("RTC found at 0x68"));

  // Only set the clock if it has lost its time (new module, dead/missing battery).
  // It uses the time this sketch was compiled, so upload right after compiling.
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting time to compile time"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

// Set the clock over serial: send "T<seconds since 1970>\n" (local standard time).
// tools/set_rtc_time.pl does this from the Mac.
void checkSetTimeCommand() {
  if (!Serial.available()) return;
  char line[16];
  size_t n = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  line[n] = '\0';
  if (line[0] != 'T') return;

  uint32_t t = strtoul(line + 1, NULL, 10);
  if (t < 1700000000UL) {  // anything before Nov 2023 is a bad value
    Serial.println(F("Bad time value"));
    return;
  }
  rtc.adjust(DateTime(t));
  char buf[] = "YYYY-MM-DD hh:mm:ss";
  Serial.print(F("Time set: "));
  Serial.println(DateTime(t).toString(buf));
}

void loop() {
  checkSetTimeCommand();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < 1000) return;
  lastPrint = millis();

  DateTime now = rtc.now();
  char buf[] = "YYYY-MM-DD hh:mm:ss";
  Serial.print(now.toString(buf));
  Serial.print(F("  temp: "));
  Serial.print(rtc.getTemperature());
  Serial.println(F(" C"));
}
