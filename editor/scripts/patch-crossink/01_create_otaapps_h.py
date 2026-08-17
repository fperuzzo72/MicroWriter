"""Cria crossink/src/OtaApps.h — arquivo novo, sem conflito com o CrossInk.

Reaproveita o src/network/OtaBootSwitch.h do próprio CrossInk, que já contorna
o bug de esp_image_verify (efuse-blk-rev) do esp_ota_set_boot_partition nesse
hardware — não precisamos reescrever essa parte.
"""

OTA_APPS_H = r'''#pragma once
#include <esp_ota_ops.h>
#include <Preferences.h>
#include "network/OtaBootSwitch.h"

static constexpr int MAX_OTA_APPS = 4;

struct OtaAppEntry {
  char name[32];
  int  partitionSubtype;
};

// Grava o nome deste app no NVS, indexado pelo slot OTA em que está rodando.
inline void registerOtaAppName(const char* name) {
  const esp_partition_t* self = esp_ota_get_running_partition();
  if (!self) return;
  int slot = self->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
  char key[8];
  snprintf(key, sizeof(key), "ota_%d", slot);
  Preferences prefs;
  prefs.begin("ota_names", false);
  prefs.putString(key, name);
  prefs.end();
}

// Varre todos os slots OTA exceto o corrente e preenche `apps[]`.
// Retorna o número de apps encontrados.
inline int detectOtaApps(OtaAppEntry* apps, int maxApps) {
  int count = 0;
  const esp_partition_t* running = esp_ota_get_running_partition();
  Preferences prefs;
  prefs.begin("ota_names", true);
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it && count < maxApps) {
    const esp_partition_t* p = esp_partition_get(it);
    if (p && p != running
           && p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0
           && p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15) {
      esp_app_desc_t desc;
      if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
        int slot = p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
        char key[8];
        snprintf(key, sizeof(key), "ota_%d", slot);
        String nvsName = prefs.getString(key, "");
        OtaAppEntry& e = apps[count];
        if (nvsName.length() > 0)
          strncpy(e.name, nvsName.c_str(), sizeof(e.name) - 1);
        else
          snprintf(e.name, sizeof(e.name), "OTA Slot %d", slot);
        e.name[sizeof(e.name) - 1] = '\0';
        e.partitionSubtype = p->subtype;
        count++;
      }
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  prefs.end();
  return count;
}

// Troca o boot partition para `subtype` e reinicia. Usa ota_boot::switchTo
// (grava direto no otadata) em vez de esp_ota_set_boot_partition, que falha
// nesse hardware com um erro de verificação de efuse-blk-rev.
inline void switchToOtaApp(int subtype) {
  const esp_partition_t* target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(subtype), NULL);
  if (!target) return;
  if (ota_boot::switchTo(target)) {
    esp_restart();
  }
}
'''

with open("crossink/src/OtaApps.h", "w") as f:
    f.write(OTA_APPS_H)

print("OtaApps.h: OK")
