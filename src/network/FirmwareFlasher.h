#pragma once

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>

// Flash a firmware image from an SD-card path into the next OTA app
// partition, then switch otadata so the X3/X4 stock bootloader picks it up
// on next boot. Mirrors the web flasher: raw esp_partition_erase_range +
// esp_partition_write + ota_boot::switchTo (no Arduino Update class, no
// esp_image_verify — those reject our patched image on X4 silicon).
//
// Both the SD update activity and the OTA path land here. OTA first
// downloads the firmware to an SD-card cache file, then calls this.

namespace firmware_flash {

enum class Result {
  OK,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  BAD_SEGMENTS,  // segment table malformed or runs past EOF
  BAD_CHECKSUM,  // ESP image XOR checksum mismatch
  BAD_SHA,       // SHA256 trailer mismatch (hash_appended images)
  BAD_SIZE,      // body+pad+sha length doesn't match file size
  NO_PARTITION,
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
  SIBLING_APP_PROTECTED,  // next-update partition holds a foreign app (dual-boot sibling)
};

// Progress callback: called after every chunk write. `written`/`total` are bytes.
using ProgressCb = void (*)(size_t written, size_t total, void* ctx);

// Open `sdPath`, validate it looks like an ESP32 image, then stream it into the
// next OTA app partition with interleaved 64 KiB erase + sector writes. On
// success switches otadata via ota_boot::switchTo. Caller is responsible for
// ESP.restart() afterwards.
//
// Dual-boot guard: before touching anything, compares the next-update
// partition's own embedded app descriptor (esp_app_desc_t::project_name)
// against the running app's. In a dual-boot setup that slot holds the
// MicroWriter X4 editor, not a spare CPR-vCodex A/B slot — project_name
// will differ, and this returns SIBLING_APP_PROTECTED without erasing a
// single byte, rather than overwrite it (or leave otadata pointing at a
// slot that's mid-write / no longer holds what the dual-boot switch
// expects, which would strand the editor as unreachable even if its bytes
// happened to survive). An unflashed/unreadable slot reads as "not a
// sibling" and updates normally, same as plain CPR-vCodex self-update.
//
// `alreadyValidated` lets callers that have just run `validateImageFile()`
// themselves (e.g. SdFirmwareUpdateActivity, which validates before showing
// the user the confirmation prompt) skip the redundant second pass. Defaults
// to false so callers without prior validation (any future entry point) keep
// the defense-in-depth check.
Result flashFromSdPath(const char* sdPath, ProgressCb onProgress, void* ctx, bool alreadyValidated = false);

// Full-image integrity check that mirrors the bootloader's verification:
// header magic, segment table walk, XOR checksum, and SHA256 trailer (when
// hash_appended == 1). Run this before flashing a candidate firmware so a
// truncated/corrupted .bin never reaches otadata.
//
// `partitionSize` is the size of the destination OTA partition; pass 0 to
// skip the size-fits-partition check (e.g. when validating ahead of partition
// lookup). Streams the file in CHUNK-sized reads; the file is rewound on
// success so the caller can immediately reread it for flashing.
Result validateImageFile(const char* sdPath, size_t partitionSize);

// True if `dest` (an OTA app partition) holds a different app than the one
// currently running — i.e. it's the dual-boot sibling (MicroWriter X4),
// not a spare A/B slot for this same firmware. Lets callers warn/block
// before even starting the update flow; flashFromSdPath also checks this
// itself as the final gate.
bool destHoldsForeignApp(const esp_partition_t* dest);

const char* resultName(Result r);

}  // namespace firmware_flash
