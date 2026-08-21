#include "input_handler.h"
#include "text_editor.h"
#include "file_manager.h"
#include "ble_keyboard.h"
#include "wifi_sync.h"
#include "dead_keys.h"

#include <Arduino.h>
#include <SDCardManager.h>

// External variables
extern bool autoReconnectEnabled;
extern bool darkMode;
extern bool cleanMode;
extern bool deleteConfirmPending;
extern WritingMode writingMode;
extern FontSize fontSize;
extern bool showWordCount;

// External functions
void storePairedDevice(const std::string& address, const std::string& name);
bool getStoredDevice(std::string& address, std::string& name);
void clearStoredDevice();
uint32_t getCurrentPasskey();
bool isDeviceScanning();
void refreshScanNow();
void clearAllBluetoothBonds();

// --- Input Queue ---
static KeyEvent inputQueue[INPUT_QUEUE_SIZE];
static int queueHead = 0;
static int queueTail = 0;
static volatile bool queueFull = false;

// --- CapsLock state ---
static bool capsLockOn = false;

// Where to return after title edit is confirmed or cancelled
static UIState renameReturnState = UIState::FILE_BROWSER;

// Forward declaration
static void openTitleEdit(const char* currentTitle, UIState returnTo);

// OTA app detection (defined in main.cpp)
extern OtaAppEntry otaApps[];
extern int otaAppCount;
void switchToOtaApp(int index);

// --- Shared UI state (defined in main.cpp) ---
extern UIState currentState;
extern int mainMenuSelection;
extern int selectedFileIndex;
extern int settingsSelection;
extern int bluetoothDeviceSelection;
extern int pairedKeyboardSelection;
extern Orientation currentOrientation;
extern bool screenDirty;
extern char renameBuffer[];
extern int renameBufferLen;

void inputSetup() {
  queueHead = 0;
  queueTail = 0;
  queueFull = false;
  capsLockOn = false;
}

static bool isQueueEmpty() {
  return (queueHead == queueTail) && !queueFull;
}

void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed) {
  noInterrupts();
  if (!queueFull) {
    inputQueue[queueHead].keyCode = keyCode;
    inputQueue[queueHead].modifiers = modifiers;
    inputQueue[queueHead].pressed = pressed;
    queueHead = (queueHead + 1) % INPUT_QUEUE_SIZE;
    if (queueHead == queueTail) queueFull = true;
  }
  interrupts();
}

static KeyEvent dequeueKeyEvent() {
  KeyEvent event = {0, 0, false};
  noInterrupts();
  if (!isQueueEmpty()) {
    event = inputQueue[queueTail];
    queueTail = (queueTail + 1) % INPUT_QUEUE_SIZE;
    queueFull = false;
  }
  interrupts();
  return event;
}

char hidToAscii(uint8_t hid, uint8_t modifiers) {
  bool shifted = isShift(modifiers) ^ capsLockOn;

  // Letters a-z (HID 0x04-0x1D)
  if (hid >= 0x04 && hid <= 0x1D) {
    char base = 'a' + (hid - 0x04);
    return shifted ? (base - 32) : base;
  }

  // Number row (HID 0x1E-0x27)
  static const char unshifted[] = "1234567890";
  static const char shiftedNum[] = "!@#$%^&*()";
  if (hid >= 0x1E && hid <= 0x27) {
    int idx = hid - 0x1E;
    return isShift(modifiers) ? shiftedNum[idx] : unshifted[idx];
  }

  // Special keys
  switch (hid) {
    case 0x28: return '\n';  // Enter
    case 0x2B: return '\t';  // Tab
    case 0x2C: return ' ';   // Space

    // Symbol keys
    case 0x2D: return isShift(modifiers) ? '_' : '-';
    case 0x2E: return isShift(modifiers) ? '+' : '=';
    case 0x2F: return isShift(modifiers) ? '{' : '[';
    case 0x30: return isShift(modifiers) ? '}' : ']';
    case 0x31: return isShift(modifiers) ? '|' : '\\';
    case 0x33: return isShift(modifiers) ? ':' : ';';
    case 0x34: return isShift(modifiers) ? '"' : '\'';
    case 0x35: return isShift(modifiers) ? '~' : '`';
    case 0x36: return isShift(modifiers) ? '<' : ',';
    case 0x37: return isShift(modifiers) ? '>' : '.';
    case 0x38: return isShift(modifiers) ? '?' : '/';

    default: return 0;
  }
}

// Handle text editor input
static void handleEditorKey(uint8_t keyCode, uint8_t modifiers) {
  // Ctrl shortcuts
  if (isCtrl(modifiers)) {
    if (keyCode == HID_KEY_S) {
      saveCurrentFile();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_Z) {
      editorUndo();
      screenDirty = true;
      return;
    }
    // Clean mode saiu do Ctrl+Z para o Ctrl+L. O Ctrl+Z e desfazer em
    // qualquer editor, e um atalho que faz outra coisa nesse lugar surpreende
    // exatamente quando a pessoa mais precisa dele -- logo depois de apagar
    // algo por engano.
    if (keyCode == HID_KEY_L) {
      cleanMode = !cleanMode;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_N) {
      openTitleEdit(editorGetCurrentTitle(), UIState::TEXT_EDITOR);
      return;
    }
    if (keyCode == HID_KEY_T) {
      writingMode = (writingMode == WritingMode::TYPEWRITER) ? WritingMode::NORMAL : WritingMode::TYPEWRITER;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_F) {
      int v = static_cast<int>(fontSize);
      fontSize = static_cast<FontSize>((v + 1) % 3);
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_W) {
      showWordCount = !showWordCount;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_P) {
      writingMode = (writingMode == WritingMode::PAGINATION) ? WritingMode::NORMAL : WritingMode::PAGINATION;
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_A) {
      editorSelectAll();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_C) {
      editorCopySelection();
      return;
    }
    if (keyCode == HID_KEY_X) {
      editorCutSelection();
      screenDirty = true;
      return;
    }
    if (keyCode == HID_KEY_V) {
      editorPasteOverSelection();
      screenDirty = true;
      return;
    }
    // Ctrl+Left/Right: jump pages in pagination mode
    if (writingMode == WritingMode::PAGINATION) {
      int pageSize = editorGetStoredVisibleLines();
      if (keyCode == HID_KEY_LEFT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorUp();
        screenDirty = true;
        return;
      }
      if (keyCode == HID_KEY_RIGHT) {
        for (int i = 0; i < pageSize; i++) editorMoveCursorDown();
        screenDirty = true;
        return;
      }
    }
    return;
  }

  // ESC = save and return to file browser
  if (keyCode == HID_KEY_ESCAPE) {
    deadKeyReset();  // discard any pending dead key
    if (editorHasUnsavedChanges()) saveCurrentFile();
    currentState = UIState::FILE_BROWSER;
    screenDirty = true;
    return;
  }

  // Tab cycles writing modes
  if (keyCode == HID_KEY_TAB) {
    int v = static_cast<int>(writingMode);
    writingMode = static_cast<WritingMode>((v + 1) % 3);
    screenDirty = true;
    return;
  }

  // Navigation keys — Shift extends/keeps the selection, a plain move clears it
  {
    bool shift = isShift(modifiers);
    switch (keyCode) {
      case HID_KEY_LEFT:      editorMoveCursorLeft(shift);  screenDirty = true; return;
      case HID_KEY_RIGHT:     editorMoveCursorRight(shift); screenDirty = true; return;
      case HID_KEY_UP:        editorMoveCursorUp(shift);    screenDirty = true; return;
      case HID_KEY_DOWN:      editorMoveCursorDown(shift);  screenDirty = true; return;
      case HID_KEY_HOME:      editorMoveCursorHome(shift);  screenDirty = true; return;
      case HID_KEY_END:       editorMoveCursorEnd(shift);   screenDirty = true; return;
      case HID_KEY_BACKSPACE:
        if (editorHasSelection()) editorDeleteSelection(); else editorDeleteChar();
        screenDirty = true;
        return;
      case HID_KEY_DELETE:
        if (editorHasSelection()) editorDeleteSelection(); else editorDeleteForward();
        screenDirty = true;
        return;
      case HID_KEY_PAGE_UP:
      case HID_KEY_PAGE_DOWN: {
        // Typewriter mode only ever shows 1 line (editorSetVisibleLines(1)
        // in ui_renderer.cpp), so a "page" there has to mean something else
        // — editorGetPageJumpLines() is a real screenful either way.
        // Pagination mode's own linesPerPage IS editorGetStoredVisibleLines(),
        // so this doubles as an alias for its existing Ctrl+Left/Right jump.
        int jump = (writingMode == WritingMode::TYPEWRITER) ? editorGetPageJumpLines()
                                                              : editorGetStoredVisibleLines();
        if (jump < 1) jump = 1;
        for (int i = 0; i < jump; i++) {
          if (keyCode == HID_KEY_PAGE_UP) editorMoveCursorUp(shift);
          else editorMoveCursorDown(shift);
        }
        screenDirty = true;
        return;
      }
    }
  }

  // CapsLock toggle
  if (keyCode == HID_KEY_CAPSLOCK) {
    capsLockOn = !capsLockOn;
    return;
  }

  // Printable character — process through US-International dead key engine
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0) {
    const char* composed = deadKeyProcess(c);
    if (composed == nullptr) {
      // No dead key involved: insert normally, replacing any selection
      if (editorHasSelection()) editorDeleteSelection();
      editorInsertChar(c);
      screenDirty = true;
    } else if (composed[0] != '\0') {
      // Composed result (or flushed dead key literal): insert UTF-8 string,
      // replacing any selection
      if (editorHasSelection()) editorDeleteSelection();
      editorInsertUtf8(composed);
      screenDirty = true;
      // If a non-composable char was requeued, insert it too
      char req = deadKeyTakeRequeue();
      if (req != 0) {
        editorInsertChar(req);
      }
    }
    // else: composed[0] == '\0' → dead key stored, nothing to insert yet
    // (leaves any existing selection untouched — nothing was typed)
  }
}

// Open the title edit screen, returning to `returnTo` on confirm/cancel
static void openTitleEdit(const char* currentTitle, UIState returnTo) {
  strncpy(renameBuffer, currentTitle, MAX_TITLE_LEN - 1);
  renameBuffer[MAX_TITLE_LEN - 1] = '\0';
  renameBufferLen = strlen(renameBuffer);
  renameReturnState = returnTo;
  currentState = UIState::RENAME_FILE;
  screenDirty = true;
}

// Handle title edit input
static void handleRenameKey(uint8_t keyCode, uint8_t modifiers) {
  if (keyCode == HID_KEY_ENTER) {
    if (renameBufferLen > 0) {
      if (renameReturnState == UIState::TEXT_EDITOR) {
        editorSetCurrentTitle(renameBuffer);
        if (editorGetCurrentFile()[0] == '\0') {
          // New file — derive filename from title
          char filename[MAX_FILENAME_LEN];
          deriveUniqueFilename(renameBuffer, filename, MAX_FILENAME_LEN);
          editorSetCurrentFile(filename);
        } else {
          // Existing file — rename on disk to match new title
          updateFileTitle(editorGetCurrentFile(), renameBuffer);
        }
        editorSetUnsavedChanges(true);
        saveCurrentFile();
      } else {
        // Updating title of a file selected in the browser
        FileInfo* files = getFileList();
        updateFileTitle(files[selectedFileIndex].filename, renameBuffer);
      }
    }
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_ESCAPE) {
    deadKeyReset();  // discard any pending dead key
    currentState = renameReturnState;
    screenDirty = true;
    return;
  }

  if (keyCode == HID_KEY_BACKSPACE) {
    deadKeyReset();  // dead key + backspace → discard dead key
    if (renameBufferLen > 0) {
      renameBufferLen--;
      renameBuffer[renameBufferLen] = '\0';
      screenDirty = true;
    }
    return;
  }

  // Allow all printable characters in a title (including spaces),
  // processed through the US-International dead key engine.
  char c = hidToAscii(keyCode, modifiers);
  if (c != 0 && c >= ' ') {
    const char* composed = deadKeyProcess(c);
    if (composed == nullptr) {
      // Normal character
      if (renameBufferLen < MAX_TITLE_LEN - 1) {
        renameBuffer[renameBufferLen++] = c;
        renameBuffer[renameBufferLen] = '\0';
        screenDirty = true;
      }
    } else if (composed[0] != '\0') {
      // Composed UTF-8 string (1–3 bytes typically): copy each byte
      for (const char* p = composed; *p != '\0' && renameBufferLen < MAX_TITLE_LEN - 1; p++) {
        renameBuffer[renameBufferLen++] = *p;
      }
      renameBuffer[renameBufferLen] = '\0';
      screenDirty = true;
      // If a non-composable char was requeued, insert it too
      char req = deadKeyTakeRequeue();
      if (req != 0 && req >= ' ' && renameBufferLen < MAX_TITLE_LEN - 1) {
        renameBuffer[renameBufferLen++] = req;
        renameBuffer[renameBufferLen] = '\0';
      }
    }
    // else: dead key stored, nothing to insert yet
  }
}

static void dispatchEvent(const KeyEvent& event) {
  if (!event.pressed) return;

  switch (currentState) {
    case UIState::MAIN_MENU: {
      int menuCount = 4 + otaAppCount;
      if (event.keyCode == HID_KEY_DOWN) {
        mainMenuSelection = (mainMenuSelection + 1) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        mainMenuSelection = (mainMenuSelection - 1 + menuCount) % menuCount;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (mainMenuSelection == 0) {
          refreshFileList();
          currentState = UIState::FILE_BROWSER;
          screenDirty = true;
        } else if (mainMenuSelection == 1) {
          createNewFile();
          openTitleEdit("Untitled", UIState::TEXT_EDITOR);
        } else if (mainMenuSelection == 2) {
          currentState = UIState::SETTINGS;
          screenDirty = true;
        } else if (mainMenuSelection == 3) {
          wifiSyncStart();
          currentState = UIState::WIFI_SYNC;
          screenDirty = true;
        } else if (mainMenuSelection >= 4) {
          switchToOtaApp(mainMenuSelection - 4);
        }
      }
      break;
    }

    case UIState::FILE_BROWSER: {
      int fc = getFileCount();

      // Delete confirmation pending — Enter confirms, anything else cancels
      if (deleteConfirmPending) {
        if (event.keyCode == HID_KEY_ENTER && fc > 0) {
          FileInfo* files = getFileList();
          deleteFile(files[selectedFileIndex].filename);
          int newFc = getFileCount();
          if (selectedFileIndex >= newFc) selectedFileIndex = newFc - 1;
          if (selectedFileIndex < 0) selectedFileIndex = 0;
        }
        deleteConfirmPending = false;
        screenDirty = true;
        break;
      }

      if (event.keyCode == HID_KEY_DOWN && fc > 0) {
        selectedFileIndex = (selectedFileIndex + 1) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP && fc > 0) {
        selectedFileIndex = (selectedFileIndex - 1 + fc) % fc;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_ENTER && fc > 0) {
        FileInfo* files = getFileList();
        loadFile(files[selectedFileIndex].filename);
        screenDirty = true;
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_N) {
        if (fc > 0) {
          FileInfo* files = getFileList();
          openTitleEdit(files[selectedFileIndex].title, UIState::FILE_BROWSER);
        }
      } else if (isCtrl(event.modifiers) && event.keyCode == HID_KEY_D) {
        if (fc > 0) {
          deleteConfirmPending = true;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::TEXT_EDITOR:
      handleEditorKey(event.keyCode, event.modifiers);
      break;

    case UIState::RENAME_FILE:
      handleRenameKey(event.keyCode, event.modifiers);
      break;

    case UIState::SETTINGS: {
      const int SETTINGS_COUNT = 6;  // Orientation, Dark Mode, Writing Mode, Font Size, Bluetooth, Paired Keyboards

      // Up/Down: navigate settings list (physical buttons also map here)
      if (event.keyCode == HID_KEY_DOWN) {
        settingsSelection = (settingsSelection + 1) % SETTINGS_COUNT;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_UP) {
        settingsSelection = (settingsSelection - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
        screenDirty = true;

      // Enter or Right: cycle setting forward
      } else if (event.keyCode == HID_KEY_ENTER || event.keyCode == HID_KEY_RIGHT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v + 1) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v + 1) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v + 1) % 3);
        } else if (settingsSelection == 4) {
          currentState = UIState::BLUETOOTH_SETTINGS;
        } else if (settingsSelection == 5) {
          pairedKeyboardSelection = 0;
          currentState = UIState::PAIRED_KEYBOARDS;
        }
        screenDirty = true;

      // Left: cycle setting backward (keyboard only — physical L/R map to Up/Down)
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (settingsSelection == 0) {
          int v = static_cast<int>(currentOrientation);
          currentOrientation = static_cast<Orientation>((v - 1 + 4) % 4);
        } else if (settingsSelection == 1) {
          darkMode = !darkMode;
        } else if (settingsSelection == 2) {
          int v = static_cast<int>(writingMode);
          writingMode = static_cast<WritingMode>((v - 1 + 3) % 3);
        } else if (settingsSelection == 3) {
          int v = static_cast<int>(fontSize);
          fontSize = static_cast<FontSize>((v - 1 + 3) % 3);
        }
        screenDirty = true;

      } else if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::MAIN_MENU;
        screenDirty = true;
      }
      break;
    }

    case UIState::BLUETOOTH_SETTINGS: {
      int deviceCount = getDiscoveredDeviceCount();

      // Ensure selection is within bounds
      if (bluetoothDeviceSelection >= deviceCount && deviceCount > 0) {
        bluetoothDeviceSelection = deviceCount - 1;
      } else if (deviceCount == 0) {
        bluetoothDeviceSelection = 0; // Reset to 0 when no devices
      }

      if (event.keyCode == HID_KEY_ESCAPE) {
        DBG_PRINTLN("[INPUT] BT: Escape pressed - returning to settings");
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection + 1) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Down pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (deviceCount > 0) {
          bluetoothDeviceSelection = (bluetoothDeviceSelection - 1 + deviceCount) % deviceCount;
          DBG_PRINTF("[INPUT] BT: Up pressed - selection now %d/%d\n", bluetoothDeviceSelection, deviceCount);
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (deviceCount > 0 && !isDeviceScanning()) {
          // Connect to the selected device
          connectToDevice(bluetoothDeviceSelection);
        } else if (!isDeviceScanning()) {
          // No devices — start a new scan
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_RIGHT) {
        // Right button = re-scan for devices
        if (!isDeviceScanning()) {
          startDeviceScan();
        }
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_LEFT) {
        if (isKeyboardConnected()) {
          disconnectCurrentDevice();
          screenDirty = true;
        }
      }
      break;
    }

    case UIState::PAIRED_KEYBOARDS: {
      int count = getPairedKeyboardCount();

      if (event.keyCode == HID_KEY_ESCAPE) {
        currentState = UIState::SETTINGS;
        screenDirty = true;
      } else if (event.keyCode == HID_KEY_DOWN) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection + 1) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_UP) {
        if (count > 0) {
          pairedKeyboardSelection = (pairedKeyboardSelection - 1 + count) % count;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_ENTER) {
        if (count > 0) {
          connectToPairedKeyboard(pairedKeyboardSelection);
          currentState = UIState::SETTINGS;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_D) {
        if (count > 0) {
          removePairedKeyboard(pairedKeyboardSelection);
          int newCount = getPairedKeyboardCount();
          if (pairedKeyboardSelection >= newCount && newCount > 0)
            pairedKeyboardSelection = newCount - 1;
          screenDirty = true;
        }
      } else if (event.keyCode == HID_KEY_LEFT) {
        // Disconnect if this keyboard is the currently connected one
        if (count > 0 && isKeyboardConnected()) {
          std::string addr, name; uint8_t addrType;
          getPairedKeyboard(pairedKeyboardSelection, addr, name, addrType);
          if (getCurrentDeviceAddress() == addr) {
            disconnectCurrentDevice();
            screenDirty = true;
          }
        }
      }
      break;
    }

    case UIState::WIFI_SYNC: {
      syncHandleKey(event.keyCode, event.modifiers);
      break;
    }

    default:
      break;
  }
}

void inputDiscardPendingKeys() {
  while (!isQueueEmpty()) (void)dequeueKeyEvent();
}

int processAllInput() {
  int processedCount = 0;
  while (!isQueueEmpty()) {
    KeyEvent event = dequeueKeyEvent();
    dispatchEvent(event);
    processedCount++;
  }
  return processedCount;
}
