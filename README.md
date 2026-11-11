# Arduino solar tracker

A single-axis sun tracker for an Arduino Uno. A DS3231 real-time clock supplies the date and time. The sketch works out the sun's compass direction (azimuth) for the configured location and turns a 28BYJ-48 stepper motor to face it. It follows the sun from east to west during the day and returns home at night.

> Status: work in progress. One axis (azimuth) works on the bench. A second axis and a stronger NEMA-17 motor are planned.

## Hardware

| Part | Notes |
|------|-------|
| Arduino Uno (or clone) | Tested with a Smraza Uno clone |
| DS3231 RTC module | With a CR2032 coin cell so it keeps time when unplugged |
| 28BYJ-48 5V stepper + ULN2003 driver board | The pair included in most Arduino starter kits |
| 1602 16×2 LCD + 220 Ω resistor | Status screen (optional) |
| Breadboard + jumper wires | The Uno has only one 5V pin, so the RTC and driver share it through the breadboard rail |

### Wiring

| From | To |
|------|----|
| DS3231 VCC / GND | 5V rail / GND rail |
| DS3231 SDA | Uno **A4** |
| DS3231 SCL | Uno **A5** |
| ULN2003 IN1, IN2, IN3, IN4 | Uno **D8, D9, D10, D11** |
| ULN2003 "+" / "−" | 5V rail / GND rail (keep the board's power jumper on) |
| Uno 5V / GND | 5V rail / GND rail |
| 28BYJ-48 | The ULN2003's white 5-pin socket |
| LCD RS, E | Uno **D7, D6** |
| LCD D4, D5, D6, D7 | Uno **D5, D4, D3, D2** (leave LCD D0–D3 empty) |
| LCD VSS, RW, K | GND rail |
| LCD VDD | 5V rail |
| LCD A (backlight) | 5V rail through a 220 Ω resistor |
| LCD V0 | GND rail with a plain jumper (sets the contrast; with a resistor here the author's screen stayed blank. If your text is too dark, try 220 Ω–1 kΩ instead of the jumper) |

One 28BYJ-48 draws about 240 mA, which is fine on USB power. For two motors, use a separate 5V supply and connect its ground to the Uno's GND.

## Setup

These steps install the toolchain with Homebrew on macOS. On other systems, install `arduino-cli` from [arduino.github.io/arduino-cli](https://arduino.github.io/arduino-cli/).

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli core update-index
arduino-cli core install arduino:avr
arduino-cli lib install RTClib LiquidCrystal
```

On Apple Silicon Macs the AVR compiler needs Rosetta 2:

```bash
softwareupdate --install-rosetta --agree-to-license
```

Find the board's port:

```bash
arduino-cli board list
```

On a Mac it looks like `/dev/cu.usbmodem1301` or `/dev/cu.usbserial-XXXX`. Use your port in place of `PORT` in the commands below.

## Usage

### 1. Set the clock

Upload `rtc_test`, then send the computer's time to the clock:

```bash
arduino-cli compile --fqbn arduino:avr:uno -u -p PORT rtc_test
perl tools/set_rtc_time.pl PORT
```

The clock is kept in **local standard time all year**, with no daylight-saving switch. The script works out your standard-time offset on its own.

### 2. Test the motor (optional)

```bash
arduino-cli compile --fqbn arduino:avr:uno -u -p PORT motor_test
```

The motor turns one revolution forward, then back, over and over.

### 3. Run the tracker

Before powering the board, **point the motor shaft (or the panel) due east**. There is no position sensor: the sketch assumes it starts at east.

```bash
arduino-cli compile --fqbn arduino:avr:uno -u -p PORT solar_tracker
arduino-cli monitor -p PORT -c baudrate=9600
```

The board prints a status line every minute:

```
2026-11-10 10:00  sun az 156.2  el 27.3  ->  motor 754 steps (156.3 deg)
```

`az` is the compass direction (90 = east, 180 = south, 270 = west), and `el` is the sun's height above the horizon. The LCD shows the same information, e.g. `11:32 Az181 El31` on top and `Panel 181 TRACK` below.

**Demo mode:** in the serial monitor, type `D` and press Enter. The board plays today's sun path from 04:00 to 21:00 in about 30 seconds, then goes back to following the real clock. To make it easier to see, the demo stretches the sunrise-to-sunset arc into one full revolution, and at sunset the motor spins quickly back home. Set `DEMO_EXAGGERATE = false` to demo the true angles instead.

## Configuration

At the top of [`solar_tracker/solar_tracker.ino`](solar_tracker/solar_tracker.ino):

| Constant | Meaning |
|----------|---------|
| `LATITUDE`, `LONGITUDE` | Your location in decimal degrees (west and south are negative). Set to Boston, MA by default |
| `UTC_OFFSET_HOURS` | Your **standard-time** offset from UTC, e.g. `-5` for US Eastern |
| `STEP_INTERVAL_US` | Time between motor steps. `1000` is about 15 RPM; much faster and the 28BYJ-48 skips steps |
| `DEMO_EXAGGERATE` | `true`: the demo turns a full revolution from sunrise to sunset. `false`: the demo shows the true sun angles |

## How it works

- **Sun position:** uses the NOAA solar-position approximation, accurate to about 0.5°. It's computed on the Uno in floating point from the day of year, UTC time, latitude and longitude.
- **Motor range:** step 0 = due east (90°), half a revolution (2048 half-steps) = due west (270°). When the sun is outside that range (summer sunrise and sunset are north of east and west), the motor waits at the nearest end. At night it returns to east.
- **Non-blocking loop:** the motor takes one step at a time, timed with `micros()`, so the board keeps reading serial commands and the clock while it moves. The coils are switched off whenever the motor is still.

## Repo layout

| Path | What it is |
|------|------------|
| `solar_tracker/` | Main sketch |
| `rtc_test/` | Clock check: prints time and temperature, accepts the set-time command |
| `motor_test/` | Stepper check: one revolution forward and back |
| `lcd_test/` | Screen check: test message and uptime on the LCD |
| `tools/set_rtc_time.pl` | Sets the DS3231 from your computer's clock over USB |
| `AGENTS.md` | Project notes for AI coding agents (pinout, toolchain quirks, conventions) |
| `CLAUDE.md` | Imports `AGENTS.md` for Claude Code |

## Known limitations and next steps

- No homing sensor: a reset during the day loses the real position. A limit switch at the east end would fix this.
- Only one axis. Tilt (elevation) tracking needs a second motor.
- The 28BYJ-48 is too weak for a real panel. The planned upgrade is a NEMA-17 bipolar stepper, which needs an H-bridge driver such as an A4988 or TB6612 instead of the ULN2003.
