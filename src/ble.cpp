// BLE GATT: service with RX (write) and TX (notify) characteristics, JSON lines.
#include <BLEDevice.h>
#include <BLEServer.h>
#include "services.h"

#define SVC_UUID "7a1c0001-5e5f-4d1b-9c3a-56c7f0a0b001"
#define RX_UUID  "7a1c0002-5e5f-4d1b-9c3a-56c7f0a0b001"
#define TX_UUID  "7a1c0003-5e5f-4d1b-9c3a-56c7f0a0b001"
static BLECharacteristic* tx = nullptr;
static bool connected = false;

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected = true; }
  void onDisconnect(BLEServer* s) override { connected = false; s->getAdvertising()->start(); }
};
class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue().c_str();
    api_handle(v, [](const String& resp){ ble_notify(resp); });
  }
};
void ble_begin() {
  BLEDevice::init("VictorModem");
  BLEDevice::setMTU(247);
  BLEServer* s = BLEDevice::createServer(); s->setCallbacks(new SrvCb());
  BLEService* svc = s->createService(SVC_UUID);
  tx = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  BLECharacteristic* rx = svc->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new RxCb());
  svc->start();
  s->getAdvertising()->addServiceUUID(SVC_UUID); s->getAdvertising()->start();
}
void ble_notify(const String& s) {
  if (!connected || !tx) return;
  // chunk to fit MTU; phone reassembles until '\n'
  String m = s + "\n";
  for (size_t i = 0; i < m.length(); i += 180) {
    String part = m.substring(i, i + 180);
    tx->setValue((uint8_t*)part.c_str(), part.length()); tx->notify(); delay(5);
  }
}
