# edge_ai — 3D gesture recognition on STM32 BlackPill (TinyML)

A production-grade Edge AI / TinyML application running bare-metal on an
STM32 BlackPill (STM32F411CEU6) with an InvenSense MPU6050 6-axis IMU,
under Zephyr RTOS. The device classifies dynamic 3D gestures in real time
from a fixed 256x6 sample window, with on-device inference through
TensorFlow Lite for Microcontrollers.

Core constraints:

- Deterministic real time on Zephyr RTOS, sampling at exactly 100 Hz
- Zero dynamic allocation: every buffer is static (`.bss`/`.rodata`)
- Inference under 5 ms per window, tensor arena under 4 KB SRAM
- Model weights stored in Flash as an aligned `const` C array, int8
  Post-Training Quantization end to end

## Project status

| Phase | Scope | Status |
| --- | --- | --- |
| 1 | Data logger firmware + host capture tool | Done, in use |
| 2 | Host model training & export | Next (dataset being collected) |
| 3 | Autonomous TFLM inference firmware | Pending |

## Hardware

| Signal | Pin | Note |
| --- | --- | --- |
| I2C1 SCL | PB8 | MPU6050 @ 0x68, 400 kHz |
| I2C1 SDA | PB9 | |
| MPU INT (data-ready) | PA2 | Rising-edge EXTI, DRDY driver |
| KEY button | PA0 | Active low, internal pull-up — manual capture trigger |
| User LED | PC13 | Active low — capture/window feedback |
| Console | USB CDC ACM (`/dev/ttyACM0`) | Logging, `dfu` reflash trigger, space capture command |

Board: WeAct Studio BlackPill V2/V3 (`blackpill_f411ce`), 96 MHz SYSCLK,
100 Hz sampling reached by hand-configured DLPF + SMPLRT_DIV (see
`src/imu.c`), sensor ranges +/-4 g and +/-1000 deg/s.

## Firmware structure

- `src/main.c` — capture state machine (IDLE / ACQUIRE / COOLDOWN).
  Triggers: KEY button on PA0 and the console `space` command.
  The automatic gyro-energy trigger exists but is compile-switched off
  during dataset collection (`ENABLE_AUTO_TRIGGER`, see below). The main
  loop only advances on DRDY interrupt samples so every recorded sample
  is exactly 10 ms apart.
- `src/imu.c/h` — MPU6050 bring-up: manual register configuration for
  exact 100 Hz (DLPF bypassing + SMPLRT_DIV), DRDY interrupt hooked to
  PA2, polled reads of the 14 registers per sample.
- `src/capture.c/h` — pre-buffer of 16 samples (160 ms) kept rolling in
  IDLE to preserve the gesture attack, fixed 256-sample capture window,
  CSV dump framed by `BEGIN`/`END` markers.
- `src/dfu_boot.c/h` — reusable "jump to ROM DFU bootloader" module for
  any STM32F4 Zephyr project: when `dfu` is typed on the console, it
  hands the Cortex-M over to the system bootloader with a warm handover
  (soft USB disconnect, clock tree restored to its reset state,
  SysTick stopped, NVIC disarmed, VTOR/MSP swapped), so the board
  re-enumerates as 0483:DF11 and can be reflashed without any button.
  Drop it in another app with its header unchanged.
- `boards/blackpill_f411ce.overlay` — I2C1 pin muxing, MPU6050 node with
  INT on PA2, USB CDC ACM console mapping.
- `prj.conf` — USB device stack, console, sensor, GPIO configuration.

## Host tool

`tools/capture.py` listens on the serial console, catches `BEGIN`/`END`
CSV windows and stores them as `dataset/<class_name>/sample_XXX.csv`.
The numbering scans the existing files first, so captures are never
overwritten, even across sessions.

```sh
python tools/capture.py -p /dev/ttyACM0 -c IDLE -d 2   # idle class, 2 s settle
python tools/capture.py -p /dev/ttyACM0 -c A           # gesture class A
```

- Press **Space** in the tool's terminal: it waits the settle delay
  (default 1 s, `-d` to change), then sends the trigger byte to the
  board. The window then records the gesture performed during those
  seconds.
- The KEY button on the board also starts a capture.
- Typing `w` (in tio, or in the tool's terminal) toggles watch mode on
  the board: the CSV dump goes silent and only the detections are
  printed — the final-product voice.
- Every window is typed-validated and required to contain exactly 256
  rows; malformed or short frames are dropped with a warning.
- `q` or Ctrl-C quits and restores the terminal.

## Building and flashing

Environment (west workspace `zephyr-ws`, cross toolchain, Python venv):

```sh
source .venv/bin/activate
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE=/usr/bin/arm-none-eabi-
west build -p auto -b blackpill_f411ce
```

First flash, or whenever the DFU path misbehaves: hold BOOT0, press and
release RESET, run:

```sh
west flash    # dfu-util runner, re-enumeration as 0483:df11
```

Reflash without touching any button once the app runs: in `tio
/dev/ttyACM0`, type `dfu`. The board hands itself over to the ROM
bootloader, `lsusb` shows `0483:df11`, then `west flash` renews the
application. A build marker (`[dfu vX]`) in the boot line identifies
which build is running on the chip.

Note: print lines emitted before the host enumerates the CDC device are
lost; open the console first, then press RESET, to see the boot.

## Signal pipeline (spec summary)

- Window: 256 samples x 6 channels (ax, ay, az, gx, gy, gz), 2.56 s at
  100 Hz
- Trigger: gyro angular kinetic energy (gx²+gy²+gz²) > threshold
  (currently 12.0 (rad/s)²), or KEY press; 500 ms cooldown after each
  window
- Preprocessing: per-channel z-score normalization, mean/std exported
  by training (`norm_params.h`), applied in place at inference
- Model: 1D-CNN — Conv1D(16, k=5) + MaxPool2, Conv1D(32, k=3) +
  MaxPool2, GlobalAveragePooling1D, Dense(16), Dense(NUM_CLASSES)
- Runtime: TFLM, int8 PTQ (inputs, weights, activations, outputs),
  static 4 KB tensor arena, weights in Flash

## Dataset guidance

- `dataset/IDLE/`: the rejection class. Record plenty (200+), mixing
  rest, walking and deliberate desk bumps; keep it free of anything
  resembling a target gesture.
- `dataset/A/`, `dataset/B/`, ...: the target gestures, several speeds
  and hand orientations.
- Variety across days beats volume recorded in one sitting: the model
  must not memorize one recording session.
- Keep a test split (10-15 %) permanently out of training.

## Known pitfalls (learned the hard way)

- The KEY (PA0) and BOOT0 buttons look alike but have nothing in
  common at run time: BOOT0 is sampled only at reset.
- The ROM bootloader refuses to leave DFU when the flash contents do
  not validate as a bootable image; dfu-util clears the leftover
  status on open, that banner is benign.

## Roadmap

1. Finish the dataset harvest (IDLE everywhere, A and B at several
   speeds), then calibrate `GYRO_ENERGY_THRESHOLD` on real motion
2. Phase 2: training + quantization scripts (TensorFlow, Python <=
   3.12 venv), export of `norm_params.h` and `gesture_model_data.h`
3. Phase 3: TFLM interpreter in firmware, static arena, in-place
   normalization, confidence threshold, LED output, DWT latency
   profiling
