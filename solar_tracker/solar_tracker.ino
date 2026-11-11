// Single-axis solar tracker: points the 28BYJ-48 at the sun's compass direction.
//
// Hardware (same as rtc_test + motor_test):
//   DS3231: SDA -> A4, SCL -> A5.  ULN2003: IN1..IN4 -> D8..D11.
//
// Motor range: step 0 = due east (azimuth 90°), half a revolution = due west (270°).
// IMPORTANT: there is no position sensor. At power-up the sketch assumes the motor
// is at step 0, so point the shaft/panel due EAST before plugging in.
//
// Serial commands (9600 baud, end with newline):
//   D              play today's sun path in about 60 seconds (demo), then resume
//   T<unix secs>   set the RTC (local standard time); used by tools/set_rtc_time.pl

#include <Wire.h>
#include <RTClib.h>
#include <math.h>

// ---- Location and time ----
const float LATITUDE  = 42.3601;   // Boston, MA (degrees north)
const float LONGITUDE = -71.0589;  // degrees east (west is negative)
const int   UTC_OFFSET_HOURS = -5; // the RTC holds EST all year (no daylight saving)

// ---- Motor ----
const uint8_t MOTOR_PINS[4] = {8, 9, 10, 11};  // IN1, IN2, IN3, IN4
const uint8_t HALF_STEPS[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1},
};
const long STEPS_PER_REV = 4096;               // half-steps per output-shaft revolution
const long STEPS_EAST_TO_WEST = STEPS_PER_REV / 2;
const float EAST_AZIMUTH = 90.0;
const float WEST_AZIMUTH = 270.0;
const unsigned long STEP_INTERVAL_US = 1000;   // ~15 RPM

// ---- Timing ----
const unsigned long TRACK_INTERVAL_MS  = 10000;  // recompute the sun every 10 s
const unsigned long REPORT_INTERVAL_MS = 60000;  // print status every minute
const unsigned long DEMO_TICK_MS       = 100;    // demo: update 10 times a second
const uint32_t DEMO_START_SECS = 4UL * 3600;     // demo plays 04:00 ...
const uint32_t DEMO_END_SECS   = 21UL * 3600;    // ... to 21:00
const uint32_t DEMO_SIM_SECS_PER_TICK = (DEMO_END_SECS - DEMO_START_SECS) / 600;  // ~60 s total

RTC_DS3231 rtc;
bool timeValid = false;

long position = 0;   // current motor step (0 = east)
long target = 0;     // step the motor is heading to
uint8_t phase = 0;
unsigned long lastStepUs = 0;
bool coilsOn = false;

bool demoRunning = false;
DateTime demoTime;
unsigned long lastDemoTickMs = 0;
uint32_t demoLastPrintSecs = 0;

unsigned long lastTrackMs = 0;
unsigned long lastReportMs = 0;

// ---------------------------------------------------------------- motor

void applyPhase(uint8_t p) {
  for (uint8_t i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], HALF_STEPS[p][i]);
  coilsOn = true;
}

void releaseMotor() {
  for (uint8_t i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], LOW);
  coilsOn = false;
}

// Take at most one step toward target; turn the coils off once there.
void runMotor() {
  if (position == target) {
    if (coilsOn) releaseMotor();
    return;
  }
  if (micros() - lastStepUs < STEP_INTERVAL_US) return;
  lastStepUs = micros();

  if (target > position) { phase = (phase + 1) % 8; position++; }
  else                   { phase = (phase + 7) % 8; position--; }
  applyPhase(phase);
}

// ---------------------------------------------------------------- sun position

// Sun azimuth (degrees clockwise from north) and elevation (degrees above the
// horizon) for a local-standard-time moment. NOAA approximation, good to ~0.5°.
void sunPosition(const DateTime& local, float& azimuth, float& elevation) {
  DateTime utc = local - TimeSpan((int32_t)UTC_OFFSET_HOURS * 3600);

  long dayOfYear = (DateTime(utc.year(), utc.month(), utc.day()).unixtime() -
                    DateTime(utc.year(), 1, 1).unixtime()) / 86400L + 1;
  float hourUtc = utc.hour() + utc.minute() / 60.0 + utc.second() / 3600.0;

  float g = 2.0 * PI / 365.0 * (dayOfYear - 1 + (hourUtc - 12.0) / 24.0);  // fractional year (radians)

  float eqTimeMin = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                              - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  float decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g)
               - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g)
               - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);          // radians

  float solarTimeMin = hourUtc * 60.0 + eqTimeMin + 4.0 * LONGITUDE;
  float hourAngle = radians(solarTimeMin / 4.0 - 180.0);
  float lat = radians(LATITUDE);

  float sinElev = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(hourAngle);
  elevation = degrees(asin(sinElev));

  // Angle from south, then shift so 0° = north, 90° = east.
  float fromSouth = atan2(sin(hourAngle),
                          cos(hourAngle) * sin(lat) - tan(decl) * cos(lat));
  azimuth = degrees(fromSouth) + 180.0;
}

// Motor step for a sun position: track between east and west while the sun is up,
// wait at the nearest end if it's outside that range, go home (east) at night.
long stepsForSun(float azimuth, float elevation) {
  if (elevation <= 0) return 0;
  float a = constrain(azimuth, EAST_AZIMUTH, WEST_AZIMUTH);
  return lround((a - EAST_AZIMUTH) / (WEST_AZIMUTH - EAST_AZIMUTH) * STEPS_EAST_TO_WEST);
}

float stepsToAzimuth(long steps) {
  return EAST_AZIMUTH + (float)steps / STEPS_EAST_TO_WEST * (WEST_AZIMUTH - EAST_AZIMUTH);
}

void printStatus(const char* label, const DateTime& t, float az, float el) {
  char buf[] = "YYYY-MM-DD hh:mm";
  Serial.print(label);
  Serial.print(t.toString(buf));
  Serial.print(F("  sun az "));
  Serial.print(az, 1);
  Serial.print(F("  el "));
  Serial.print(el, 1);
  Serial.print(F("  ->  motor "));
  Serial.print(target);
  Serial.print(F(" steps ("));
  Serial.print(stepsToAzimuth(target), 1);
  Serial.println(el > 0 ? F(" deg)") : F(" deg, night: home)"));
}

// ---------------------------------------------------------------- serial commands

void handleSerial() {
  if (!Serial.available()) return;
  char line[16];
  size_t n = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  line[n] = '\0';

  if (line[0] == 'T') {
    uint32_t t = strtoul(line + 1, NULL, 10);
    if (t < 1700000000UL) { Serial.println(F("Bad time value")); return; }
    rtc.adjust(DateTime(t));
    timeValid = true;
    char buf[] = "YYYY-MM-DD hh:mm:ss";
    Serial.print(F("Time set: "));
    Serial.println(DateTime(t).toString(buf));
    lastTrackMs = millis() - TRACK_INTERVAL_MS;  // re-aim right away
  } else if (line[0] == 'D' || line[0] == 'd') {
    if (!timeValid) { Serial.println(F("Set the time first")); return; }
    DateTime now = rtc.now();
    demoTime = DateTime(now.year(), now.month(), now.day()) + TimeSpan(DEMO_START_SECS);
    demoLastPrintSecs = 0;
    demoRunning = true;
    Serial.println(F("Demo: playing today's sun path from 04:00 to 21:00"));
  }
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);
  for (uint8_t i = 0; i < 4; i++) pinMode(MOTOR_PINS[i], OUTPUT);
  releaseMotor();

  Serial.println(F("Solar tracker starting (motor assumed at EAST = step 0)"));
  if (!rtc.begin()) {
    Serial.println(F("RTC not found. Check SDA->A4, SCL->A5, VCC, GND."));
    while (true) {}
  }
  timeValid = !rtc.lostPower();
  if (!timeValid) {
    Serial.println(F("RTC lost power: set it with tools/set_rtc_time.pl. Motor stays home."));
  }
  Serial.println(F("Send D for a 60-second demo of today's sun path."));
  lastTrackMs = millis() - TRACK_INTERVAL_MS;   // aim immediately
  lastReportMs = millis() - REPORT_INTERVAL_MS; // and report immediately
}

void loop() {
  handleSerial();
  runMotor();

  if (demoRunning) {
    if (millis() - lastDemoTickMs < DEMO_TICK_MS) return;
    lastDemoTickMs = millis();

    float az, el;
    sunPosition(demoTime, az, el);
    target = stepsForSun(az, el);

    uint32_t secsOfDay = demoTime.hour() * 3600UL + demoTime.minute() * 60UL;
    if (secsOfDay - demoLastPrintSecs >= 1800 || demoLastPrintSecs == 0) {  // every 30 sim-minutes
      demoLastPrintSecs = secsOfDay - (secsOfDay % 1800);
      printStatus("[demo] ", demoTime, az, el);
    }

    demoTime = demoTime + TimeSpan(DEMO_SIM_SECS_PER_TICK);
    if (demoTime.hour() * 3600UL >= DEMO_END_SECS) {
      demoRunning = false;
      Serial.println(F("Demo finished, following the real clock again"));
      lastTrackMs = millis() - TRACK_INTERVAL_MS;
      lastReportMs = millis() - REPORT_INTERVAL_MS;
    }
    return;
  }

  if (!timeValid) { target = 0; return; }
  if (millis() - lastTrackMs < TRACK_INTERVAL_MS) return;
  lastTrackMs = millis();

  DateTime now = rtc.now();
  float az, el;
  sunPosition(now, az, el);
  target = stepsForSun(az, el);

  if (millis() - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = millis();
    printStatus("", now, az, el);
  }
}
