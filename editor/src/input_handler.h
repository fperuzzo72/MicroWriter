#pragma once

#include "config.h"

void inputSetup();
void enqueueKeyEvent(uint8_t keyCode, uint8_t modifiers, bool pressed);
int processAllInput();
char hidToAscii(uint8_t hid, uint8_t modifiers);

// Throw away every key still queued. For a screen that appears on its own --
// a prompt raised by something finishing, not by the user -- everything
// already in the queue was typed at a different screen and must not be
// allowed to answer a question that was not on display when it was pressed.
void inputDiscardPendingKeys();
