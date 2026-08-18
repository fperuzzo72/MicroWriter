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

// ota_boot::switchTo() sempre grava o novo slot com estado "new" (pendente
// de confirmação) — correto para o autoupdate genuíno do próprio leitor
// (FirmwareFlasher.cpp reusa esse mesmo switchTo(), onde a rede de
// segurança de rollback do bootloader deve valer para firmware novo e não
// testado), mas errado para a troca dual-boot: aqui só apontamos para um
// slot irmão *já gravado e previamente funcional*, nunca para código novo.
// Deixado como "new", o próximo reset antes do app se confirmar (nenhum
// dos dois chama esp_ota_mark_app_valid_cancel_rollback()) é revertido
// silenciosamente pelo bootloader de volta pro outro slot — observado como
// "acorda do sleep sempre no app anterior" mesmo com o irmão ativo antes de
// dormir. Isso encontra a entrada que o switchTo() acabou de gravar (maior
// seq) e marca só o estado dela como válido, deixando o switchTo() em si —
// e portanto o autoupdate do leitor — com a proteção de rollback intacta.
inline void confirmLastOtaSwitch() {
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) return;
  constexpr size_t kSectorSize = 0x1000;
  if (otadata->size < 2 * kSectorSize) return;

  ota_boot::SelectEntry slots[2] = {};
  if (esp_partition_read(otadata, 0, &slots[0], sizeof(ota_boot::SelectEntry)) != ESP_OK ||
      esp_partition_read(otadata, kSectorSize, &slots[1], sizeof(ota_boot::SelectEntry)) != ESP_OK) {
    return;
  }

  int newestIdx = -1;
  uint32_t newestSeq = 0;
  for (int i = 0; i < 2; ++i) {
    if (slots[i].ota_seq == 0xFFFFFFFFu) continue;
    if (slots[i].crc != ota_boot::computeSeqCrc(slots[i].ota_seq)) continue;
    if (newestIdx < 0 || slots[i].ota_seq > newestSeq) {
      newestIdx = i;
      newestSeq = slots[i].ota_seq;
    }
  }
  if (newestIdx < 0) return;

  constexpr uint32_t kOtaImgValid = 2;  // ESP_OTA_IMG_VALID
  slots[newestIdx].ota_state = kOtaImgValid;

  const size_t off = static_cast<size_t>(newestIdx) * kSectorSize;
  if (esp_partition_erase_range(otadata, off, kSectorSize) != ESP_OK) return;
  esp_partition_write(otadata, off, &slots[newestIdx], sizeof(slots[newestIdx]));
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
    confirmLastOtaSwitch();
    esp_restart();
  }
}
'''

with open("crossink/src/OtaApps.h", "w") as f:
    f.write(OTA_APPS_H)

print("OtaApps.h: OK")
