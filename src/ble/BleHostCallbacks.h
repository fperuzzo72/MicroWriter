#pragma once

/**
 * @file BleHostCallbacks.h
 * @brief Shared NimBLEServerCallbacks dispatcher.
 *
 * NimBLE 2.x lets only ONE callback object register with a server via
 * `NimBLEServer::setCallbacks(...)` — last setter wins. Multiple modules
 * sharing the same server (BleFileTransfer + BleBridge today, future
 * services tomorrow) used to "agree" that file-transfer would install
 * the callback object and bridge would skip it; bridge then polled
 * `_server->getConnectedCount() > 0` instead of getting events. Two
 * problems with that:
 *
 *   1. If a user disables file-transfer and only uses bridge, bridge
 *      has no connection-event hooks. Disconnect cleanup doesn't run.
 *   2. The "later caller wins" invariant is fragile — anyone who
 *      reorders init() calls (to bring up bridge before file-transfer)
 *      silently breaks file-transfer's callbacks.
 *
 * This dispatcher fixes both: it installs a single shared
 * `NimBLEServerCallbacks` and fan-outs each event to all registered
 * subscribers. Modules subscribe with function pointers + an optional
 * `void* userData`. Audit #50.
 *
 * Capacity is fixed (4 subscribers — currently 2 are needed and 4
 * gives breathing room for future plugins). All storage is file-
 * static so init order doesn't matter.
 */

#include "../config.h"

#if FEATURE_BLUETOOTH

#include <NimBLEDevice.h>

namespace ble_host {

using ConnectFn    = void (*)(NimBLEServer*, NimBLEConnInfo&, void* userData);
using DisconnectFn = void (*)(NimBLEServer*, NimBLEConnInfo&, int reason, void* userData);
using MtuChangeFn  = void (*)(uint16_t mtu, NimBLEConnInfo&, void* userData);

struct Subscriber {
  ConnectFn    onConnect    = nullptr;
  DisconnectFn onDisconnect = nullptr;
  MtuChangeFn  onMtuChange  = nullptr;
  void*        userData     = nullptr;

  // Explicit ctor — GCC 8.4 (riscv32-esp-elf shipped with espressif32
  // 6.12.0) refuses brace-init on aggregates that mix default member
  // initialisers in this layout. Pre-Batch-3 we hit the same on
  // ReadVisitor; same fix here.
  Subscriber() = default;
  Subscriber(ConnectFn c, DisconnectFn d, MtuChangeFn m, void* u)
      : onConnect(c), onDisconnect(d), onMtuChange(m), userData(u) {}
};

// Install the shared callbacks on the server. Idempotent: safe to call
// from multiple modules during their init(). The internal callback
// object is file-static so its lifetime is the program's — NimBLE
// stores a raw pointer.
void install(NimBLEServer* server);

// Register a subscriber. Returns true on success, false if the
// fixed-size table is full. Callbacks may be nullptr; the dispatcher
// only invokes non-null function pointers.
bool subscribe(const Subscriber& sub);

// Unregister a subscriber. Matches by struct equality (function-
// pointer + userData identity). Returns true if a subscriber was
// removed.
bool unsubscribe(const Subscriber& sub);

}  // namespace ble_host

#endif  // FEATURE_BLUETOOTH
