// 28BYJ-48 + ULN2003 test for the Uno clone.
// Wiring: IN1 -> D8, IN2 -> D9, IN3 -> D10, IN4 -> D11,
//         ULN2003 "+" -> 5V, "-" -> GND, motor plug into the white socket.
// Turns one revolution forward, rests, turns back to the start, rests, repeats.
// Open the serial monitor at 9600 baud.

const uint8_t MOTOR_PINS[4] = {8, 9, 10, 11};  // IN1, IN2, IN3, IN4

// Half-step sequence: 8 steps per cycle, about 4096 steps per output-shaft revolution.
const uint8_t HALF_STEPS[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1},
};

const long STEPS_PER_REV = 4096;
const unsigned long STEP_INTERVAL_US = 1000;  // ~15 RPM; much below this the motor may stall or skip
const unsigned long REST_MS = 1000;

long position = 0;        // steps from the start position
long target = 0;          // where the motor is heading
uint8_t phase = 0;        // index into HALF_STEPS
unsigned long lastStepUs = 0;
unsigned long restUntilMs = 0;
bool forward = true;

void applyPhase(uint8_t p) {
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOTOR_PINS[i], HALF_STEPS[p][i]);
  }
}

void releaseMotor() {  // all coils off: no holding torque, no heat
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MOTOR_PINS[i], LOW);
  }
}

// Take at most one step toward target. Returns true while still moving.
bool runMotor() {
  if (position == target) return false;
  if (micros() - lastStepUs < STEP_INTERVAL_US) return true;
  lastStepUs = micros();

  if (target > position) {
    phase = (phase + 1) % 8;
    position++;
  } else {
    phase = (phase + 7) % 8;  // same as phase - 1, wrapping 0 -> 7
    position--;
  }
  applyPhase(phase);
  return true;
}

void setup() {
  Serial.begin(9600);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(MOTOR_PINS[i], OUTPUT);
  }
  releaseMotor();

  Serial.println(F("28BYJ-48 test: starting in 3 seconds"));
  delay(3000);
  applyPhase(phase);
  target = STEPS_PER_REV;
  Serial.println(F("Turning 1 revolution forward"));
}

void loop() {
  if (runMotor()) return;

  // Arrived at the target.
  if (restUntilMs == 0) {
    releaseMotor();
    Serial.print(F("Arrived, position = "));
    Serial.println(position);
    restUntilMs = millis() + REST_MS;
    return;
  }
  if ((long)(millis() - restUntilMs) < 0) return;  // still resting

  // Rest over: head the other way.
  restUntilMs = 0;
  forward = !forward;
  target = forward ? STEPS_PER_REV : 0;
  applyPhase(phase);
  Serial.println(forward ? F("Turning 1 revolution forward")
                           : F("Turning 1 revolution back to start"));
}
