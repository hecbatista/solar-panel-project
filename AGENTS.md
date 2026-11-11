# Agent guide: Arduino solar tracker

Instructions for AI coding agents working in this repo. `CLAUDE.md` imports this file for Claude Code; other tools read `AGENTS.md` directly. For a human-oriented overview, see `README.md`.

This is an Arduino sketch project built and uploaded with `arduino-cli` (no Arduino IDE).

## Hardware

| Part | Details |
|------|---------|
| Board | Smraza Arduino Uno clone (ATmega328P). Shows up as a genuine-style UNO on `/dev/cu.usbmodem*`, no driver needed |
| RTC | DS3231 module on I2C: SDA = A4, SCL = A5 |
| Motor (kit, in use) | 1 × 28BYJ-48 5V unipolar stepper, from the Smraza kit |
| Motor driver (kit, in use) | 1 × ULN2003 board (inputs IN1–IN4, "+" and "−" power pins, power jumper) |
| Display | Kit 1602A 16×2 parallel LCD (HD44780), driven by the `LiquidCrystal` library |
| Motor (not wired yet) | Adafruit P324 NEMA-17 bipolar stepper: 12V, 350 mA, 200 steps/rev, 35 Ω coils. Coil A = red + yellow, coil B = green + brown/gray. **Cannot use a ULN2003**: needs an H-bridge driver (A4988, TB6612, L298N), which the kit does not include |

### Pin assignments

| Function | Pins |
|----------|------|
| DS3231 SDA / SCL | A4 / A5 (hardware I2C, fixed on the Uno) |
| Stepper 1 (28BYJ-48) IN1, IN2, IN3, IN4 | D8, D9, D10, D11 |
| Stepper 1 ULN2003 power | "+" → Uno 5V, "−" → Uno GND (OK for one 28BYJ-48 on USB power) |
| Stepper 2 | _not yet decided_ (second 28BYJ-48 + ULN2003, or the NEMA-17 once a driver is bought) |
| 1602 LCD RS, E | D7, D6 |
| 1602 LCD D4, D5, D6, D7 | D5, D4, D3, D2 (LCD D0–D3 unconnected, 4-bit mode) |
| 1602 LCD power | VSS, RW, K → GND; VDD → 5V; A (backlight) → 5V through 220 Ω; V0 → GND with a plain jumper (full contrast, no potentiometer). On this screen 1 kΩ to GND left it completely blank; if text is too dark, use 220 Ω–1 kΩ instead of the jumper |

The Uno has a single 5V pin, so the DS3231 and the ULN2003 share it through the breadboard's power rail. Update this table when the wiring changes.

Pin rules:
- Never use A4/A5 for anything else; they are the I2C bus.
- Avoid D0/D1; they are the USB serial line used for upload and the serial monitor.
- D13 drives the onboard LED. It works as an output but flickers during boot.

## Toolchain

- `arduino-cli` 1.6.0 (Homebrew), core `arduino:avr` 1.8.8
- Libraries: `RTClib` 2.1.4 and its dependency `Adafruit BusIO`; `LiquidCrystal` 1.0.7
- On Apple Silicon Macs the AVR compiler is an x86_64 binary and needs Rosetta 2 (`softwareupdate --install-rosetta --agree-to-license`). A `bad CPU type in executable` error means Rosetta is missing.
- Board FQBN: `arduino:avr:uno`. If uploads fail with `not in sync` errors, the clone has the old bootloader: use `arduino:avr:nano:cpu=atmega328old` instead.
- On the author's Mac the board has shown up as `/dev/cu.usbmodem1301` and `/dev/cu.usbmodem1401` (it depends on the USB socket), and `arduino-cli board list` identifies it as "Arduino UNO" (`arduino:avr:uno`). The port name can change when it's plugged into a different USB port, so rerun `board list` if an upload can't find it.

## Commands

```bash
arduino-cli compile --fqbn arduino:avr:uno <sketch_dir>
arduino-cli compile --fqbn arduino:avr:uno -u -p /dev/cu.usbmodem1301 <sketch_dir>   # compile + upload
arduino-cli monitor -p /dev/cu.usbmodem1301 -c baudrate=9600
arduino-cli lib install "<Library Name>"
```

A sketch lives in its own folder, and the `.ino` file must have the same name as the folder (for example `tracker/tracker.ino`). Always compile before uploading, and check the reported flash and RAM use. The Uno has 32 KB of flash and only 2 KB of RAM.

## Hardware notes for code

### DS3231
- Use `RTC_DS3231` from RTClib. I2C address is 0x68.
- Check `rtc.lostPower()` in `setup()`. If it returns true, set the time once with `rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))`, which uses the compile time on the computer. Don't leave an unconditional `adjust()` in the sketch: it resets the clock to the compile time on every reboot.
- **Time convention: the RTC holds local standard time, US Eastern (EST, UTC−5), and never switches for daylight saving.** Sketches must not apply DST. Convert to UTC by adding 5 hours, for example for sun-position math.
- To set the clock, upload a sketch that accepts the serial command `T<unix seconds>\n` (`rtc_test` does), then run `perl tools/set_rtc_time.pl /dev/cu.usbmodem1301`. The script works out the standard-time offset itself and sends the time right on a second boundary. Opening the port resets the Uno, and the script waits for the board to start before sending.

### 28BYJ-48 + ULN2003
- Gear ratio is about 1:64, giving roughly 2048 steps per revolution in full-step mode and 4096 in half-step mode.
- Practical top speed is about 10–15 RPM. Faster than that and the motor stalls or skips steps.
- With the built-in `Stepper` library, pass the pins in the order **IN1, IN3, IN2, IN4**. Otherwise the motor buzzes or only vibrates. AccelStepper with `AccelStepper::HALF4WIRE` takes IN1, IN3, IN2, IN4 as well.
- Each motor draws about 240 mA. Power the ULN2003 boards from an external 5V supply and connect its ground to the Uno's GND. Don't power both motors from the Uno's 5V pin.
- Steppers have no position feedback, so positions are tracked by counting steps. Keep both motors' step counts in code, and plan a homing or reference routine if the absolute position matters.
- To save power and reduce heat while a motor is idle, set all four of its pins LOW.

## Sketches

| Folder | Purpose |
|--------|---------|
| `rtc_test/` | Prints DS3231 time and temperature every second. Accepts the `T` set-time command |
| `motor_test/` | Turns the 28BYJ-48 one revolution forward and back, repeating |
| `lcd_test/` | Prints a test message and uptime on the 1602 LCD, for checking wiring and contrast |
| `solar_tracker/` | **Main sketch.** Points the motor at the sun's compass direction (azimuth) from the RTC time |

### solar_tracker behavior
- Location: Boston, MA (`LATITUDE = 42.3601`, `LONGITUDE = -71.0589`). The RTC is EST, so `UTC_OFFSET_HOURS = -5`.
- Sun position uses the NOAA approximation (accurate to about 0.5°). Checked against real values: Boston on Nov 10 gives sunrise ~06:33 EST at 113°, solar noon 11:28 at 30.6° elevation, sunset ~16:24 at 247°.
- Motor range: step 0 = due east (90°), 2048 half-steps = due west (270°). While the sun is up the motor follows its azimuth, waiting at the nearest end when the sun is outside 90–270° (Boston summer sunrise and sunset are around 56° and 304°). At night it goes home to step 0.
- **There is no position sensor. At power-up the motor is assumed to be at step 0 (east)**, so the shaft must point east before the board is powered. A reset in the middle of the day loses the real position.
- Re-aims every 10 s and prints a status line every 60 s. Coils are off whenever the motor isn't moving.
- LCD shows `HH:MM AzNNN ElNN` (or `HH:MM sun down`) on row 1, and `Panel NNN STATE` on row 2. STATE is TRACK, MOVE, WAIT (sun outside 90–270°), HOME (night) or DEMO; during the exaggerated demo row 2 is `DEMO NN% turn`. Refreshed every 500 ms by overwriting rows (no `lcd.clear()`, which would stall the motor).
- Serial `D` plays today's sun path from 04:00 to 21:00 in about 30 s, then returns to real time. With `DEMO_EXAGGERATE = true` (the default) the demo maps the sunrise-to-sunset azimuth arc onto one full revolution (4096 steps) instead of true angles, and spins back home at sunset. This affects only the demo, never real tracking. The `T` command sets the clock, as in `rtc_test`. If the RTC lost power, the motor stays home until the time is set.
