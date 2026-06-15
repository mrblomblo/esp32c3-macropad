<img width="1920" alt="Banner image of the finished macro pad" src="https://github.com/user-attachments/assets/49cba890-6401-4d10-acb9-053189fc5b9d" />

# ESP32-C3 BLE MacroPad

A compact 2×4 Bluetooth macro keyboard built on the **ESP32-C3 SuperMini**. Keys are fully remappable from a browser. No app, no driver, no USB cable (for programming) required after flashing.

## Features

- **8-key matrix** (2 columns × 4 rows) with 15 ms debounce and 6-key rollover
- **BLE HID keyboard**, pairs natively with Windows, macOS, Linux, Android, iOS
- **Consumer / media key** support (play, pause, skip, volume, mute)
- **Modifier combos**, Ctrl, Shift, Alt, AltGr, GUI (Windows key), or any combination
- **SSD1306 OLED** (128×32) showing live key labels and press highlights
- **Display auto-sleep** after 60 seconds of inactivity
- **NVS persistence**, keymap survives power cycles
- **Web Configurator** (`index.html` or [hosted](https://macropad.mrblomblo.com/)) over BLE, runs in any Chromium browser

<details>
<summary>View Images</summary>

<img width="1440" height="1920" alt="image" src="https://github.com/user-attachments/assets/fc2aaa2d-c099-4ab6-b1eb-e4b8a5ca96fc" />

<img width="1920" height="1440" alt="image" src="https://github.com/user-attachments/assets/8e2087e8-97c9-426e-9561-595a4cfcc262" />

<img width="1920" height="1440" alt="image" src="https://github.com/user-attachments/assets/aecf7e4c-a3cf-4195-98fa-051e66dbfcc8" />

</details>

## Hardware

| Part | Notes |
| --- | --- |
| ESP32-C3 SuperMini | Any variant with the standard pinout works |
| SSD1306 OLED | 128×32, I2C, address `0x3C` |
| 8 Switches | Mechanical keyboard switches recommended. Wired in a 4-row × 2-column matrix |
| 8 Diodes | Probs not needed, but best to be safe |
| 8 Keycaps | Choose whatever you like |
| 9 M3 screws | Should be countersunk and should not have a total length of more than 14 mm |
| 3D-printed Parts | Found [here](https://www.printables.com/model/1754812-esp32-c3-macropad) |
| Pull-up resistors | **Not needed, firmware uses internal `INPUT_PULLUP` on rows** |

### Pin Wiring

```
Matrix columns  ->  GPIO 10 (COL 0),  GPIO 20 (COL 1)
Matrix rows     ->  GPIO  0 (ROW 0),  GPIO  1 (ROW 1),
                    GPIO  2 (ROW 2),  GPIO  3 (ROW 3)

OLED SDA        ->  GPIO  8
OLED SCL        ->  GPIO  9
```

The columns are driven LOW one at a time during each scan; rows are read with internal pull-ups. Wire each switch between its column pin and row pin (no diodes required for this matrix size).

### Key Layout

```
┌────────┬────────┐
│ R0 C0  │ R0 C1  │  <- hold R0C0 at boot = Config mode
├────────┼────────┤
│ R1 C0  │ R1 C1  │  <- hold R1C0 at boot = Flash/bootloader mode
├────────┼────────┤
│ R2 C0  │ R2 C1  │
├────────┼────────┤
│ R3 C0  │ R3 C1  │
└────────┴────────┘
```

## Firmware Setup

### Dependencies

Install these libraries in the Arduino IDE before compiling:

- **ESP32 Arduino core** (≥ 2.x)
- **Adafruit GFX Library**
- **Adafruit SSD1306**

### Compiling & Flashing

1. Open `Macropad.ino` in the Arduino IDE.
2. Select board: **ESP32C3 Dev Module** (or your specific SuperMini variant).
3. Set USB CDC On Boot: **Enabled** (needed for the serial monitor).
4. Compile and upload over USB the first time.

> **Subsequent reflashes without USB:** Hold **R1C0** (second row, left key) while powering on. The firmware forces the ESP32-C3 into its hardware bootloader so you can flash over USB without pressing the physical BOOT button in case something goes horribly wrong.

## Boot Modes

The firmware checks two keys during startup (before the BLE stack initialises). Hold the key, apply power, then release once you see the OLED message.

| Key held at power-on | Mode | BLE name | Purpose |
| --- | --- | --- | --- |
| *(none)* | **Keyboard mode** | `MacroPad` | Normal operation, acts as a BLE HID keyboard |
| **R0C0** (top-left) | **Config mode** | `MacroPad-CFG` | Exposes the keymap characteristic for the web configurator |
| **R1C0** (2nd row, left) | **Flash mode** | - | Reboots straight into the hardware bootloader for USB reflashing |

Config mode advertises on a separate Bluetooth MAC address so it should never conflict with an existing HID pairing on the host. Though, this does not seem to work as intended most of the time.

> [!NOTE]
> You may need to "forget" the Macropad device for the Macropad-CFG device (configurator mode) to work properly!

## OLED Display

The 128×32 screen is divided into two columns of four rows, one cell per key.

| Element | Meaning |
| --- | --- |
| Key label (white text) | The assigned key name (e.g. `Ctrl+C`, `Vol+`, `F5`) |
| Inverted pill (black text on white) | Key is currently pressed |
| Small flashing dot (top-right) | BLE not connected, blinks every second |
| `CFG` badge (top-centre) | Device is in Config mode |
| Blank screen | Display has gone to sleep after 60s of inactivity, or you are in Flash mode |

## Web Configurator

Open `index.html` (or the [hosted one](https://macropad.mrblomblo.com/)) in a **Chromium-based browser** (Chrome, Edge, Brave, Opera). Firefox and Safari do not currently support the Web Bluetooth API.

### First-time connection

1. Boot the macropad into **Config mode** (hold R0C0, apply power, release).
2. Open `index.html` in Chrome/Edge.
3. Click **Connect** and a browser dialog will list nearby Bluetooth devices.
4. Select **MacroPad-CFG** and click Pair.
5. The configurator reads the current keymap from the device and displays it.

### Editing a key

1. Click any key card in the grid.
2. In the modal, choose a key from the dropdown (Letters, Numbers, Function Keys, Special, Symbols, or Media).
3. Optionally tick one or more modifiers (Ctrl, Shift, Alt, AltGr, GUI). Modifiers are disabled automatically for media keys.
4. Click **Apply**, or **Clear** to make the key do nothing.

### Saving

Click **Save to Device**. The button lights up with a ring highlight whenever there are unsaved changes. All 8 keys are written to the device in one BLE write and persisted to NVS flash immediately.

> After saving, reboot the macropad normally (no key held) to return to Keyboard mode.

### Presets

The **Presets** dropdown loads a ready-made 8-key layout. Applying a preset marks the config as "dirty", so you still need to click **Save to Device** to write it.

| Preset | Keys |
| --- | --- |
| Default (A–H) | A, B, C, D, E, F, G, H |
| Productivity | Ctrl+Z, Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+A, Ctrl+S, Ctrl+N, Ctrl+Y |
| Media | Prev, Next, Vol−, Vol+, Mute, Play/Pause, —, — |
| Drawing | `[`, `]`, Ctrl+S, E, Ctrl+Shift+Z, Ctrl+Z, Delete, Space |
| Navigation | ↑, ↓, ←, →, Home, End, PgUp, PgDn |

### Export & Import

- **Export**: downloads the current keymap as `macropad-config.json`.
- **Import**: loads a previously exported `.json` file into the editor (does not save automatically).

The configurator also caches the last keymap in browser `localStorage`, so the grid is pre-populated on the next visit even without a device connected.

## Key Encoding

Keycodes are stored as `uint16_t` values in the format below. You can use this if you want to craft a JSON config by hand.

```
Bit 15–13  : reserved (0)
Bit 12     : 1 = media/consumer key, 0 = standard key
Bits 15–8  : modifier mask (standard keys only)
Bits  7–0  : HID keycode (standard) or consumer usage ID (media)
```

**Modifier byte (bits 15–8)**

| Bit | Modifier |
| --- | --- |
| 0 | Ctrl (left) |
| 1 | Shift (left) |
| 2 | Alt (left) |
| 3 | GUI / Win / Cmd |
| 6 | AltGr (right Alt) |

**Examples**

| Encoded value | Meaning |
| --- | --- |
| `0x0004` | A |
| `0x0116` | Ctrl + S |
| `0x0319` | Ctrl + Shift + V |
| `0x10CD` | Play / Pause (media) |
| `0x10E9` | Volume Up (media) |
| `0x0000` | Unassigned (no action) |

## BLE Services

| Mode | Service UUID | Characteristic UUID | Purpose |
| --- | --- | --- | --- |
| Keyboard | `0x1812` (standard HID) | standard HID reports | Keyboard + consumer input |
| Config | `12345678-1234-5678-1234-56789abcdef0` | `12345678-1234-5678-1234-56789abcdef1` | Read/write raw keymap (16 bytes, 8 × `uint16_t` LE) |

## Troubleshooting

**The macropad doesn't appear in the Bluetooth device list**
- Make sure it was booted in Config mode (OLED shows `CONFIG MODE`).
- Refresh the browser tab and click Connect again, the Web Bluetooth picker only scans for a few seconds.
- "Forget" the Macropad device before attempting to connect to the Macropad-CFG device

**"Web Bluetooth not supported" error**
- You must use a Chromium-based browser. Firefox and Safari do not currently support Web Bluetooth.

**Keys fire wrong characters on the host**
- HID keycodes are layout-independent (they represent physical key positions). If your OS uses a non-US keyboard layout, the characters produced may differ from the labels shown in the configurator.  
  For example, `AltGr+9` is needed for the `]` symbol on a Swedish keyboard layout.

**Save to Device fails immediately after connecting**
- The ESP32 BLE stack needs ~200 ms after GATT connection to enumerate services. The configurator adds this delay automatically, so if it still fails try disconnecting and reconnecting.

**Keys stop responding after reflashing**
- If the host OS has bonded to the old HID profile, remove the old pairing on the host and re-pair.

**Keypresses are not being sent to my host device**
- Disconnect and, to be safe, "forget" the Macropad-CFG device, then re-pair with the Macropad device.
- Verify that you connected the diodes correctly.
- Verify that you connected the cables to the ESP32-C3 correctly.

**Display never lights up**
- Verify SDA/SCL wiring and that your OLED module is set to I2C address `0x3C`. A few SSD1306 breakouts ship with `0x3D` set by a solder jumper.
