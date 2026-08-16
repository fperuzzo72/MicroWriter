#include "BleHostCallbacks.h"

#if FEATURE_BLUETOOTH

#include <Arduino.h>

namespace ble_host {

namespace {

constexpr int MAX_SUBSCRIBERS = 4;
Subscriber subscribers[MAX_SUBSCRIBERS] = {};
int subscriberCount = 0;
bool installed = false;

class SharedServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    // Snapshot the count up front — a subscriber's handler could
    // theoretically call subscribe()/unsubscribe(), and we don't want
    // to walk past the new tail or skip a shifted entry.
    const int n = subscriberCount;
    for (int i = 0; i < n; ++i) {
      if (subscribers[i].onConnect) {
        subscribers[i].onConnect(server, info, subscribers[i].userData);
      }
    }
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    const int n = subscriberCount;
    for (int i = 0; i < n; ++i) {
      if (subscribers[i].onDisconnect) {
        subscribers[i].onDisconnect(server, info, reason, subscribers[i].userData);
      }
    }
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    const int n = subscriberCount;
    for (int i = 0; i < n; ++i) {
      if (subscribers[i].onMtuChange) {
        subscribers[i].onMtuChange(mtu, info, subscribers[i].userData);
      }
    }
  }
};

SharedServerCallbacks sharedCallbacks;

}  // namespace

void install(NimBLEServer* server) {
  if (!server) return;
  if (installed) return;
  server->setCallbacks(&sharedCallbacks);
  installed = true;
  Serial.println("[BLE-HOST] Shared server callbacks installed (fan-out dispatcher)");
}

bool subscribe(const Subscriber& sub) {
  if (subscriberCount >= MAX_SUBSCRIBERS) {
    Serial.printf("[BLE-HOST] subscribe: table full (%d subs)\n", MAX_SUBSCRIBERS);
    return false;
  }
  subscribers[subscriberCount++] = sub;
  return true;
}

bool unsubscribe(const Subscriber& sub) {
  for (int i = 0; i < subscriberCount; ++i) {
    if (subscribers[i].onConnect == sub.onConnect &&
        subscribers[i].onDisconnect == sub.onDisconnect &&
        subscribers[i].onMtuChange == sub.onMtuChange &&
        subscribers[i].userData == sub.userData) {
      // Shift remaining entries down one slot.
      for (int j = i; j < subscriberCount - 1; ++j) {
        subscribers[j] = subscribers[j + 1];
      }
      subscribers[subscriberCount - 1] = {};
      subscriberCount--;
      return true;
    }
  }
  return false;
}

}  // namespace ble_host

#endif  // FEATURE_BLUETOOTH
