#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include <mutex>

enum class ModemModel { Unknown, SIM7600, A7670, EC25 };
const char* modelName(ModemModel m);

struct AtResult { bool ok; String lines; };
using AtCallback = std::function<void(const AtResult&)>;
using EventSink  = std::function<void(const String& type, const String& jsonData)>;

// UART AT driver: single in-flight command, FIFO queue, URC dispatch.
class Modem {
 public:
  void begin(EventSink sink);
  void powerOn();
  void loop();                                   // call frequently
  void enqueue(const String& cmd, AtCallback cb = nullptr, uint32_t timeoutMs = 5000,
               const String& prompt = "", const String& payload = "");
  AtResult sendSync(const String& cmd, uint32_t timeoutMs = 5000);  // only before loop runs
  ModemModel model() const { return model_; }
  void detect();
 private:
  struct Job { String cmd; AtCallback cb; uint32_t timeout; String prompt; String payload; };
  void handleLine(const String& line);
  bool isUrc(const String& line);
  void handleUrc(const String& line);
  void finish(bool ok);
  HardwareSerial* ser_ = nullptr;
  std::vector<Job> q_;
  std::recursive_mutex mx_;
  bool busy_ = false; uint32_t started_ = 0; String acc_, rx_;
  ModemModel model_ = ModemModel::Unknown;
  EventSink sink_;
  String pendingCmt_;
};
extern Modem modem;
