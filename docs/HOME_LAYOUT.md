# Home screen layout

MicroWriter X4's Home screen deliberately does not look like SUMI's. It
follows CrossPoint Reader's built-in `CLASSIC` home theme instead (also
used by the CrossInk fork): the last book read at the top, a plain
menu list of rows directly below it, no decorative art background, no
book carousel.

## Why

SUMI's stock Home (`HomeState`/`HomeView` before this change) drew a large
centered book card over a full-screen sumi-e ink-art background, with a
left/right carousel to flip between recently-read books. That's a
deliberate aesthetic choice on SUMI's part, but not the one this project
wants — see the project's own README for the reasoning.

## What changed

- `ui::HomeView` (`src/ui/views/HomeView.h`) dropped `useArtBackground` and
  the whole recent-books carousel (`recentBooks[]`, `selectedBookIndex`,
  `addRecentBook()`, etc.). In its place: a flat `menuItems[]` list with a
  single `selection` index, navigated with Up/Down, activated with Center —
  same model CrossPoint's own `buildSelectableHomeMenuItems` uses, where
  "Continue reading" is just row 0 of one list, not a separate carousel
  widget with its own input handling.
- `ui::render(..., HomeView)` (`src/ui/views/HomeView.cpp`) draws: brand +
  battery header, then the book block (small cover + title/author beside
  it, progress bar below), then the menu row list. `CardDimensions` was
  replaced by `calculateBookBlockLayout()` — a single function shared by
  `ui::render()` and `HomeState::renderCoverToCard()` (which draws an
  SD-loaded BMP cover on top afterwards), so the two draws can't drift out
  of alignment the way two independent magic-number blocks could.
- `HomeState::update()` navigates the unified list and dispatches through
  `launchMenuTarget()`, which either acts locally (open the book) or
  transitions to an existing `StateId` (FileList, Settings, PluginList) or
  launches a specific registered plugin directly via `PluginHostState`
  (same mechanism `PluginListState` itself uses, just skipping its picker
  screen for menu rows that go straight to one plugin — see
  `HomeState.h`'s `setHostState()`).

## Known loose ends from this change (not yet cleaned up)

- **Settings still has a "Home Art" screen** (`SettingsScreen::HomeArt` in
  `SettingsState.cpp`) for picking a `/config/themes/*.bmp` background.
  It's now inert — HomeState no longer reads `homeArtTheme` at all — but
  removing it means renumbering `SettingsState::openSelected()`'s
  index-based switch across ~15 call sites, which felt like a separate,
  riskier change to make blind (no hardware to verify the settings menu
  still works end to end). Left in place on purpose; worth removing once
  this has been tested on a device.
- **Progress display lost its "Chapter 12 of 34" / "Page 5 of 20" text.**
  The old Home drew that detail; the new book block reuses `ui::progress()`
  (shared with other screens), which only shows a bar + percentage. Traded
  off deliberately for less new/bespoke code, but it's a real feature
  reduction someone might miss.
- Only visually verified by inspection and a successful `pio run` build —
  not run on an actual X4 yet.
