#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/ShortcutRegistry.h"

class ShortcutVisibilityActivity final : public Activity {
 public:
  explicit ShortcutVisibilityActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ShortcutVisibility", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<const ShortcutDefinition*> entries;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;

  void reloadEntries();
  void toggleSelectedEntry();
};
