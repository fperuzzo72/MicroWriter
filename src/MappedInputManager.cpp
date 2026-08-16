#include "MappedInputManager.h"

#include "core/SumiSettings.h"

// Physical button layout:
//   - frontLayout (FrontBCLR vs FrontLRBC) swaps the BOTTOM two toggle
//     switches between Back/Confirm and Left/Right. This is a pure
//     hardware remap and applies to every state.
//   - sideButtonLayout (PrevNext vs NextPrev) used to also swap the side
//     toggle between Up/Down, which broke menu navigation in every
//     non-reader state (user feedback: "the top sidebutton move the page
//     forward and bottom go backwards. While SUMI has a setting for this,
//     changing it also messes up the normal up down movements in menus").
//     That swap now lives inside ReaderState page navigation only; here
//     Up/Down are a direct 1:1 hardware mapping.
decltype(InputManager::BTN_BACK) MappedInputManager::mapButton(const Button button) const {
  const auto frontLayout = settings_ ? static_cast<sumi::Settings::FrontButtonLayout>(settings_->frontButtonLayout)
                                     : sumi::Settings::FrontBCLR;

  switch (button) {
    case Button::Back:
      switch (frontLayout) {
        case sumi::Settings::FrontLRBC:
          return InputManager::BTN_LEFT;
        case sumi::Settings::FrontBCLR:
        default:
          return InputManager::BTN_BACK;
      }
    case Button::Confirm:
      switch (frontLayout) {
        case sumi::Settings::FrontLRBC:
          return InputManager::BTN_RIGHT;
        case sumi::Settings::FrontBCLR:
        default:
          return InputManager::BTN_CONFIRM;
      }
    case Button::Left:
      switch (frontLayout) {
        case sumi::Settings::FrontLRBC:
          return InputManager::BTN_BACK;
        case sumi::Settings::FrontBCLR:
        default:
          return InputManager::BTN_LEFT;
      }
    case Button::Right:
      switch (frontLayout) {
        case sumi::Settings::FrontLRBC:
          return InputManager::BTN_CONFIRM;
        case sumi::Settings::FrontBCLR:
        default:
          return InputManager::BTN_RIGHT;
      }
    case Button::Up:
      return InputManager::BTN_UP;
    case Button::Down:
      return InputManager::BTN_DOWN;
    case Button::Power:
      return InputManager::BTN_POWER;
  }

  return InputManager::BTN_BACK;
}

bool MappedInputManager::wasPressed(const Button button) const { return inputManager.wasPressed(mapButton(button)); }

bool MappedInputManager::wasReleased(const Button button) const { return inputManager.wasReleased(mapButton(button)); }

bool MappedInputManager::isPressed(const Button button) const { return inputManager.isPressed(mapButton(button)); }

bool MappedInputManager::wasAnyPressed() const { return inputManager.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return inputManager.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return inputManager.getHeldTime(); }
