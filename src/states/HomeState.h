#pragma once

#include <cstdint>
#include <string>

#include "../ui/views/HomeView.h"
#include "State.h"

class GfxRenderer;

namespace sumi {

class PluginHostState;

class HomeState : public State {
 public:
  explicit HomeState(GfxRenderer& renderer);
  ~HomeState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Home; }

  // Needed to launch a specific plugin directly from a menu row (Escrever,
  // Dicionário, Jogos) without detouring through PluginListState's own
  // picker UI. Same wiring pattern as PluginListState::setHostState.
  void setHostState(PluginHostState* host) { hostState_ = host; }

 private:
  GfxRenderer& renderer_;
  Core* core_ = nullptr;  // Stored for theme loading
  ui::HomeView view_;
  PluginHostState* hostState_ = nullptr;

  // Cover image state
  std::string coverBmpPath_;
  bool hasCoverImage_ = false;
  bool coverLoadFailed_ = false;
  uint32_t currentBookHash_ = 0;

  void loadLastBook(Core& core);
  void buildMenu();
  void openSelectedBook(Core& core);
  void updateBattery();
  void renderCoverToCard();
  StateTransition launchMenuTarget(Core& core, ui::HomeView::MenuTarget target);

  bool pendingOpen_ = false;  // Set by openSelectedBook to trigger Reader transition
};

}  // namespace sumi
