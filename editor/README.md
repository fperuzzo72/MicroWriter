# MicroSlate US-International keyboard version

> Imported into [MicroWriter](https://github.com/fperuzzo72/MicroWriter)
> as `editor/` at tag `microwriter-0.1`. The original repo this came from,
> [microslate-firmware-US-International](https://github.com/fperuzzo72/microslate-firmware-US-International),
> stays up as the frozen historical record at that same tag — all editor
> development from here on happens in this copy, not there.

Everything you had on MicroSlate 2.0.3, updated to use dead keys to emulate the US-International keyboard layout as used by Microsoft Windows. I just needed to use accented characters.

Also fixed two bugs found while using this daily — documented here in case they're useful upstream, since this fork carries no shared git history back to the original repo:

- **Backspace on accented characters left garbage.** The original delete code checked the wrong byte offset when scanning backward for UTF-8 continuation bytes, so deleting a character typed via a dead key (2+ UTF-8 bytes, i.e. any accented letter) could leave a stray byte behind instead of cleanly removing the whole character. Fixed in `utf8BackLen()` (`src/text_editor.cpp`): it now walks back over every continuation byte (`10xxxxxx`) before stopping at the lead byte — the original tested the byte *before* the one it needed to, so it never found them.
- **Word-wrap could silently drop characters off-screen.** Word-wrap decided line breaks by counting characters against a `charsPerLine` estimate derived from one sample string's average glyph width. Any line whose actual characters rendered wider than that average — an ordinary word like "deve" was enough, no accents required — overflowed the screen's real pixel width, and the renderer's own overflow guard (`truncatedText()`) clipped it silently: the extra characters stayed in the buffer (removable with backspace) but were never drawn. Fixed by making word-wrap measure real glyph widths instead of estimating them: `editorRecalculateLines()` (`src/text_editor.cpp`) now sums each codepoint's actual advance width — via a `glyphWidthFn` callback the renderer registers at startup, so this module keeps no font/renderer dependency of its own — against a real pixel budget (`editorSetMaxLineWidthPx`, set from the screen's actual text-area width). No line can render wider than the screen regardless of font size or character mix. Verified at both small and medium font sizes, portrait and landscape, on physical hardware.

As I made this modification after Crosspoint 1.4.0 has been released, also modified to include that upgrade on dual boot. That pairing has since moved to a dedicated, actively maintained integration — **[MicroWriter X4](https://github.com/fperuzzo72/MicroWriter)** (paired with CPR-vCodex, not the older CrossPoint 1.4.0) — where this repo is used as the editor half; see that repo's `NOTICE.md` for the current dual-boot credit and mechanism.

**Stable release: MicroWriter 0.2.** This is the editor half of the MicroWriter X4 (MicroSlate) / CPR-vCodex pairing, confirmed working together on physical Xteink X4 hardware. New in 0.2: PgUp/PgDn (and Shift+PgUp/PgDn to select), mode-aware — a real screen-sized jump even in Typewriter mode, where only one line is ever drawn. Everything from 0.1 (browser file manager at `/`, mDNS/OTA rename to "MicroWriter") carries forward unchanged.

Everything else is exactly as it were on MicroSlate 2.0.3 described below. 

# MicroSlate

A dedicated writing firmware for the **Xteink X4** e-paper device. Pairs with any **Bluetooth LE (BLE)** keyboard and saves notes to MicroSD.

## Features

- **Bluetooth Keyboard** — BLE HID host, connects to any standard wireless keyboard. Stores up to 4 keyboards; auto-cycles through them on reconnect. Tested with Logitech Keys-To-Go 2 and Keychron K3.
- **Note Management** — browse, create, rename, and delete notes from an SD card
- **Named Notes** — each note has a title stored in the file; shown in the browser and editable without touching body text
- **Text Editor** — cursor navigation, word-wrap, fast e-paper refresh
- **Writing Modes** — three display modes to suit different writing styles:
  - *Scroll* — standard scrolling editor (default)
  - *Typewriter* — shows only the current line centered on a blank screen. Focused, distraction-free single-line writing
  - *Pagination* — page-based display instead of scrolling. Clean page flips instead of per-line scroll refreshes
- **Auto-Save** — content is silently saved to SD card after 10 seconds of idle or every 2 minutes during continuous typing; no manual save required. Every exit path (back button, Esc, power button, sleep, restart) also saves automatically
- **Safe Writes** — saves use a write-verify + `.bak` rotation pattern; a failed or interrupted write never destroys the previous version. Orphaned files from a crash are recovered automatically on next boot
- **Clean Mode** — hides all UI chrome while editing so only your text is on screen (Ctrl+Z to toggle)
- **Dark Mode** — inverted display
- **Display Orientation** — portrait, landscape, and inverted variants
- **Power Management** — ESP-IDF light sleep between loop iterations (CPU drops to 10MHz), BLE modem sleep keeps the radio alive, SD card sleeps between accesses, display analog circuits power down after each refresh, and the device enters deep sleep after 5 minutes of inactivity
- **WiFi Sync** — one-button backup of all notes to your PC over WiFi. Saves network credentials for instant reconnect. Read-only server — nothing on the device can be modified over the network
- **Standalone Build** — all libraries are bundled in the repo; no sibling projects required
- **Dual-Boot** — optional combined firmware that includes an e-reader in a second OTA slot. An entry appears in the main menu; selecting it reboots into the reader, which gains a reciprocal entry back. Both apps work normally when flashed standalone. In the current [MicroWriter X4](https://github.com/fperuzzo72/MicroWriter) pairing (CPR-vCodex, not the older CrossPoint 1.4.0 this section originally described) these entries read "MicroWriter" and "CPR-vCodex" respectively — "MicroWriter" without the hardware-specific "X4" suffix, same as the mDNS hostname (see WiFi Sync, below), so this identifier stays meaningful if this codebase is ever ported to a different device.
- **Settings Backup** — BLE pairing info, WiFi credentials, and UI preferences are backed up to the SD card as JSON files. They are silently restored after a firmware flash so you don't need to re-pair your keyboard or re-enter WiFi passwords.

## Hardware Requirements

- Xteink X4 e-paper device (ESP32-C3, 800x480 display, physical buttons, SD slot)
- MicroSD card formatted as FAT32
- A **Bluetooth LE (BLE)** HID keyboard — confirm your keyboard uses BLE before pairing. The ESP32-C3 hardware has no Classic Bluetooth (BR/EDR) radio; Classic BT keyboards cannot connect regardless of firmware settings.

## Installation

### Option 1 — Browser installer (recommended)

No software required. Works on Windows and Mac in Chrome or Edge.

**[Install MicroSlate → typeslate.com/tools/microslate](https://typeslate.com/tools/microslate/)**

Connect your Xteink X4 via USB and click **Install MicroSlate** for the standalone firmware, or **Install Dual-Boot** to get MicroSlate + CrossPoint on the same device. Takes about 2 minutes.

### Option 2 — Build from source

Requires a Windows or Linux x86_64 machine (the ESP-IDF toolchain does not support Mac ARM or Raspberry Pi).

**Prerequisites**

- [PlatformIO](https://platformio.org/install/) (CLI or VS Code extension)
- USB cable to connect to the Xteink X4

```bash
# Clone the repository
git clone https://github.com/Josh-writes/microslate-firmware
cd xteink-writer-firmware

# Build and upload (adjust port if needed)
pio run --target upload --upload-port /dev/ttyUSB0
```

The upload port defaults to `COM5` in `platformio.ini`.

All libraries are included in the `lib/` directory. The only external dependency fetched automatically by PlatformIO is **esp-nimble-cpp** (BLE stack).

### First Boot

1. Insert a FAT32-formatted MicroSD card
2. Power on the device — it boots to the main menu
3. Go to **Settings → Bluetooth** and scan for your keyboard
4. Select your keyboard from the list and press Enter to pair
5. Return to the main menu and start writing

The device remembers paired keyboards (up to 4) and reconnects automatically on subsequent boots. If multiple keyboards are stored, it cycles through them until one responds.

## Usage

### Main Menu

| Key | Action |
|-----|--------|
| Up / Down | Navigate |
| Left / Right | Also navigate (convenient in landscape) |
| Enter | Select |

Options: **Browse Notes**, **New Note**, **Settings**, **Sync** — and **CrossPoint** if the dual-boot firmware is installed

### File Browser

| Key | Action |
|-----|--------|
| Up / Down | Navigate list |
| Left / Right | Also navigate (convenient in landscape) |
| Enter | Open note |
| Ctrl+N | Edit title of selected note |
| Ctrl+D | Delete selected note (confirmation required) |
| Esc | Back to main menu |

When delete is pending, the footer shows `Delete? Enter:Yes  Esc:No`. Press Enter to confirm or any other key to cancel.

### Text Editor

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor |
| Shift+Arrow keys | Select text (extends from where Shift was first held) |
| Home / End | Start / end of line |
| Shift+Home / Shift+End | Select to start / end of line |
| PgUp / PgDn | Jump one screen up/down (one page, in Pagination mode) |
| Shift+PgUp / Shift+PgDn | Select one screen up/down |
| Backspace / Delete | Remove characters (or the selection, if any) |
| Ctrl+A | Select all |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste |
| Tab | Cycle writing mode (Scroll → Typewriter → Pagination) |
| Ctrl+S | Save manually |
| Ctrl+N | Edit note title |
| Ctrl+Z | Toggle clean mode (hides UI chrome) |
| Ctrl+T | Toggle Typewriter mode |
| Ctrl+P | Toggle Pagination mode |
| Ctrl+Left / Right | Jump pages (Pagination mode only) |
| Esc / Back button | Save and return to file browser |

The current writing mode is shown in the header: **[S]** Scroll, **[T]** Typewriter, **[P]** Pagination.

Auto-save runs silently after 10 seconds of idle or every 2 minutes during continuous typing — Ctrl+S is only needed if you want to save immediately.

**Selection** works like a modern editor, not WordStar's Ctrl-K mark-begin/mark-end: hold Shift and move the cursor to select, the same as any desktop app. Typing while a selection is active replaces it. There's a single clipboard slot (the device has no OS clipboard) that survives switching notes, so you can copy in one and paste in another. One simplification versus a desktop editor: moving the cursor *without* Shift always just moves it and clears the selection — it doesn't collapse to the selection's edge first the way some editors do.

### Writing Modes

**Scroll [S]** — Standard scrolling editor. Text scrolls as the cursor moves down the page.

**Typewriter [T]** — Only the current line is shown, centered vertically on a blank screen. When you press Enter, the previous line disappears and a fresh line appears. Text is still saved to the buffer normally. Combine with Clean Mode (Ctrl+Z) for a completely minimal writing experience.

**Pagination [P]** — Instead of scrolling when text fills the screen, the display flips to a new blank page. The current page is shown in the header (e.g. "Pg 1/3"). Use Ctrl+Left and Ctrl+Right to jump between pages. Eliminates per-line scroll refreshes — only one refresh per page transition.

### Title Edit

Accessed via Ctrl+N from the file browser or editor.

| Key | Action |
|-----|--------|
| Type | Enter title text |
| Backspace | Delete last character |
| Enter | Confirm |
| Esc | Cancel |

### Settings

Navigate with all four direction buttons (or Up/Down on keyboard). Press Enter (or confirm button) to cycle through a setting's values. On a keyboard, Left/Right also cycle values backward/forward.

| Setting | Values |
|---------|--------|
| Orientation | Portrait, Landscape CW, Inverted, Landscape CCW |
| Dark Mode | Light / Dark |
| Writing Mode | Normal, Typewriter, Pagination |
| Bluetooth | Opens Bluetooth scan to pair a new keyboard |
| Paired Keyboards | Manage saved keyboards (connect, forget, disconnect) |

All settings persist across reboots.

### Paired Keyboards

Shows all keyboards saved on the device (up to 4). The currently active keyboard is labelled **active**; the last used keyboard when none is connected is labelled **last**.

| Key | Action |
|-----|--------|
| Up / Down | Navigate list |
| Enter | Switch to selected keyboard |
| D | Forget selected keyboard (removes pairing) |
| Left | Disconnect selected keyboard (if currently active) |
| Esc | Back to Settings |

To pair a second keyboard, go to **Settings → Bluetooth**, scan, and connect. Both keyboards will then appear in the Paired Keyboards list. On each boot the device tries the last-used keyboard first, then works through the rest of the list until one connects.

### Bluetooth Settings

| Key | Action |
|-----|--------|
| Up / Down | Navigate device list |
| Enter | Connect to selected device (or start scan if list is empty) |
| Right | Re-scan for devices |
| Left | Disconnect current keyboard |
| Esc | Back to Settings |

A scan runs for 5 seconds and then stops. Up to 10 nearby devices are shown with name, address, and signal strength.

### WiFi Sync

Manage notes over WiFi. The device and your computer/phone must be on the **same WiFi network**. No app or account — everything runs from the device itself.

#### Connecting

1. Select **Sync** from the main menu on the device
2. **First time:** pick your WiFi network and enter the password. The device asks to save credentials.
3. **After that:** the device auto-connects — just press Sync and wait
4. Once connected, the device shows a URL — e.g. `http://192.168.1.42/`

#### Browser file manager (no PC software needed)

Open the URL shown on the device in any browser (phone, tablet, or computer) to browse, upload, download, and delete notes — the same webserver + file-manager UI CPR-vCodex uses for books, ported and trimmed down to plain `.txt` notes (see `src/web_files_page.h`).

The device also advertises itself at `http://microwriter.local/` via mDNS, but mDNS name resolution doesn't work on every network or OS — **the numeric URL shown on the device's screen always works and is the reliable fallback** if the `.local` name doesn't resolve.

Uploads over 16KB are rejected outright (see "Note size limit" under File Format, below) rather than silently accepted and truncated later.

#### Automatic PC sync (optional, one-way backup)

A background script that auto-discovers the device (via the same mDNS name) and pulls every new note down to a folder on your PC, so you don't have to open a browser each time. This predates the browser file manager above and still works alongside it — the device's webserver is the same one both use.

**One-time setup:**

1. Install [Python 3](https://www.python.org/downloads/) if you don't have it
2. Install the required library:
   ```bash
   pip install requests
   ```
3. Run the installer for your platform:

**Windows** — double-click **`sync\install_sync.bat`**

**macOS / Linux** — run in a terminal:
```bash
chmod +x sync/install_sync.sh && sync/install_sync.sh
```

That's it. The script starts immediately and will run silently in the background on every login. When a sync completes, a desktop notification lists the files that were downloaded (Windows balloon, macOS notification, or Linux `notify-send`). Notes are saved to `Documents/MicroSlate Notes/` by default (edit `LOCAL_DIR` in `microslate_sync.py` to change).

To stop auto-start later:
- **Windows** — double-click **`sync\uninstall_sync.bat`**
- **macOS / Linux** — run `sync/uninstall_sync.sh`

If the sync script isn't running, you can start it manually:
```bash
python3 sync/microslate_sync.py
```

This direction is one-way and read-only from the script's side: it only ever downloads, never uploads or deletes. Files already on the PC with the same name and size are skipped; files deleted from the device are **not** deleted from the PC — they stay as a backup.

#### How it all works

- The device runs the HTTP server (port 80, mDNS `microwriter.local`) the whole time Sync is open — both the browser file manager and the PC script talk to that same server
- The browser file manager can upload and delete (see above) — the server is **not** read-only overall, only the PC script's own usage pattern is
- WiFi turns off automatically after 60 seconds of no activity, or when you press Esc

#### Sync controls

| Key | Action |
|-----|--------|
| Up / Down | Navigate network list |
| Enter | Select network / confirm |
| Esc | Cancel / back |

## File Format

Notes are plain `.txt` files stored in `/notes/` on the SD card. Filenames are derived from the note title — spaces become underscores, everything is lowercased, and `.txt` is appended. For example, a note titled "My Note" becomes `my_note.txt`.

Files are fully compatible with any text editor on a computer. To add notes manually, drop `.txt` files into the `/notes/` folder on the SD card — the title shown on the device is derived from the filename.

### Note size limit — TODO, revisit

A note's maximum size is a fixed 16KB (`TEXT_BUFFER_SIZE` in `src/config.h`), not a function of free RAM — there's no SD-backed paging/streaming, the whole note is always fully in RAM or not loaded at all.

**This is currently a silent data-loss trap, not just a size cap.** If a `.txt` file already larger than 16KB ends up in `/notes/` (only possible today by copying one onto the SD card manually — nothing the device itself writes can exceed the limit), opening it silently truncates to the first ~16KB with no warning on screen, and saving it afterwards overwrites the original file with that truncated content — permanently dropping everything past the cut point after the first save (a second save even overwrites the one-generation `.bak`). See the comments at `TEXT_BUFFER_SIZE` (`src/config.h`) and in `loadFile()` (`src/file_manager.cpp`).

Not fixed yet — at minimum, detecting and warning on a truncated load would close the silent-data-loss part even before tackling paging.

## Project Structure

```
xteink-writer-firmware/
├── src/
│   ├── main.cpp          — setup, main loop, shared UI state
│   ├── sd_backup.h       — inline SD/JSON helpers for NVS backup and restore
│   ├── ble_keyboard.cpp  — BLE scanning, pairing, HID report handling
│   ├── input_handler.cpp — keyboard event queue and UI state dispatch
│   ├── text_editor.cpp   — text buffer and cursor management
│   ├── file_manager.cpp  — SD card file operations
│   ├── ui_renderer.cpp   — screen rendering for all UI modes
│   ├── wifi_sync.cpp     — WiFi sync server and state machine
│   └── config.h          — enums, buffer sizes, constants
├── sync/
│   ├── microslate_sync.py   — PC sync script (Python, cross-platform)
│   ├── install_sync.bat     — register auto-start on Windows login
│   ├── uninstall_sync.bat   — remove auto-start on Windows
│   ├── install_sync.sh      — register auto-start on macOS / Linux
│   └── uninstall_sync.sh    — remove auto-start on macOS / Linux
├── lib/                  — all hardware/display libraries (bundled)
│   ├── GfxRenderer/
│   ├── EpdFont/
│   ├── EInkDisplay/
│   ├── hal/
│   ├── BatteryMonitor/
│   ├── InputManager/
│   ├── SDCardManager/
│   └── Utf8/
└── platformio.ini
```

## Troubleshooting

**Keyboard not showing in scan**
- Make sure the keyboard is in pairing mode and not connected to another device
- Press Right to re-scan after switching the keyboard to pairing mode
- **Classic Bluetooth keyboards will never appear** — the ESP32-C3 only has a BLE radio. This is a hardware constraint, not a software limitation. Verify your keyboard uses BLE before debugging further (check the manufacturer's specs; most keyboards sold after 2014 use BLE, but some older or multi-device keyboards still use Classic Bluetooth)

**Physical buttons not responding**
- BLE scanning can occasionally interfere with the ADC button reads
- Hold the BACK button for 3 seconds to restart the device

**Display appears frozen**
- E-paper refresh takes ~430ms — wait for it to complete before pressing more keys

**Serial monitor shows nothing on startup**
- The ESP32-C3 USB-CDC port re-enumerates after reset; startup logs are sent before the monitor reconnects. This is normal — the device is working correctly.

---

## More from TypeSlate

MicroSlate is the hardware companion to **TypeSlate** — a free, full-screen distraction-free writing app for Windows. Same idea, different form factor: open it, write, close it.

- **TypeSlate for Windows** — free on the [Microsoft Store](https://apps.microsoft.com/detail/9PM3J9SQB0TV?hl=en-us&gl=US&ocid=pdpshare)
- **Website** — [typeslate.com](https://typeslate.com)

If MicroSlate is useful to you and you'd like to say thanks, you can support the project at [ko-fi.com/typeslate](https://ko-fi.com/typeslate).
