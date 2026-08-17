#pragma once

// Dual-boot sibling-app detection and switching. The register/detect/switch
// scheme is ported from MicroSlate (github.com/Josh-writes/microslate-
// firmware, src/main.cpp), itself adapted from CrossInk's build-time
// dual-boot patch (uxjulia/CrossInk) — see NOTICE.md. The low-level
// otadata-write primitive (ota_boot::switchTo) is NOT ported: CPR-vCodex
// already ships the same fix natively as src/network/OtaBootSwitch.h/.cpp
// (used by its own SD/OTA self-update), so this reuses that instead of
// duplicating it.
//
// CPR-vCodex's own firmware self-update (Settings > Check for Updates / SD
// Firmware Update) always targets esp_ota_get_next_update_partition() —
// i.e. "the other OTA slot" — which in a dual-boot setup is where the
// MicroWriter X4 editor lives. FirmwareFlasher.cpp guards against this:
// destHoldsForeignApp() compares that partition's embedded app descriptor
// against the running app's own and refuses the update (before erasing
// anything) if they differ, rather than overwrite the editor or leave
// otadata pointing somewhere the dual-boot switch can no longer make sense
// of. See FirmwareFlasher.h.
//
// Requires the ota_0/ota_1 partitions.csv layout this project already
// ships, which matches MicroSlate's own partition table byte for byte.

#include <Preferences.h>
#include <esp_ota_ops.h>

#include <cstdio>
#include <cstring>

#include "network/OtaBootSwitch.h"

static constexpr int MAX_OTA_APPS = 4;

struct OtaAppEntry {
  char name[32];
  int partitionSubtype;
};

// Records this app's display name in shared NVS, keyed by which OTA slot
// it's currently running from, so sibling apps can show it by name.
inline void registerOtaAppName(const char* name) {
  const esp_partition_t* self = esp_ota_get_running_partition();
  if (!self) return;
  const int slot = self->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
  char key[8];
  snprintf(key, sizeof(key), "ota_%d", slot);
  Preferences prefs;
  prefs.begin("ota_names", false);
  prefs.putString(key, name);
  prefs.end();
}

// Scans every OTA app partition except the one currently running and fills
// `apps[]` with their registered names (falling back to "OTA Slot N" for an
// unregistered/never-booted sibling). Returns the number found.
inline int detectOtaApps(OtaAppEntry* apps, int maxApps) {
  int count = 0;
  const esp_partition_t* running = esp_ota_get_running_partition();
  Preferences prefs;
  prefs.begin("ota_names", true);

  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it && count < maxApps) {
    const esp_partition_t* p = esp_partition_get(it);
    if (p && p != running && p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
      esp_app_desc_t desc;
      if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
        const int slot = p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
        char key[8];
        snprintf(key, sizeof(key), "ota_%d", slot);
        String nvsName = prefs.getString(key, "");

        OtaAppEntry& entry = apps[count];
        if (nvsName.length() > 0) {
          strncpy(entry.name, nvsName.c_str(), sizeof(entry.name) - 1);
        } else {
          snprintf(entry.name, sizeof(entry.name), "OTA Slot %d", slot);
        }
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.partitionSubtype = p->subtype;
        count++;
      }
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  prefs.end();
  return count;
}

// Switches the boot partition to `subtype` and restarts. Uses
// ota_boot::switchTo (writes otadata directly) instead of
// esp_ota_set_boot_partition, which fails on this hardware with a bogus
// efuse-blk-rev verification error.
inline void switchToOtaApp(int subtype) {
  const esp_partition_t* target =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, static_cast<esp_partition_subtype_t>(subtype), nullptr);
  if (!target) return;
  if (ota_boot::switchTo(target)) {
    esp_restart();
  }
}

// Convenience for the "MicroWriter" shortcut (ShortcutId::MicroSlate —
// named after the editor's underlying codebase, MicroSlate, not its display
// name): this firmware only ever has one dual-boot sibling, so just switch
// to whichever OTA app is detected first (a no-op if the other slot has
// never been flashed).
inline void switchToFirstOtaApp() {
  OtaAppEntry apps[MAX_OTA_APPS];
  const int count = detectOtaApps(apps, MAX_OTA_APPS);
  if (count > 0) {
    switchToOtaApp(apps[0].partitionSubtype);
  }
}
