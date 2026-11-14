#include "net/Remote.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "app/App.h"
#include "sources/Https.h"

static App* g_app = nullptr;

static const uint16_t REQUEST_TIMEOUT_S = 40;  // the server holds a request for up to 25 s
static const uint32_t RETRY_MIN_MS = 5000, RETRY_MAX_MS = 60000;

// Runs one command from the server. Returns "" or an error message.
static std::string run(JsonObjectConst cmd) {
  std::string type = cmd["type"] | "";
  std::string body;
  serializeJson(cmd["body"], body);
  if (type == "config") {
    // The admin key and the relay link itself may only be changed on the display (its own page or
    // the setup portal). Enforced here, not just on the server: a relay that is compromised or
    // misconfigured must not be able to take the display over or point it somewhere else.
    for (const char* key : {"admin_key", "remote_url", "remote_token"}) {
      if (!cmd["body"][key].isNull()) return std::string(key) + " can only be changed on the display itself";
    }
    return g_app->updateConfig(body);
  }
  if (type == "tokens") return g_app->updateTokens(body);
  if (type == "show") return g_app->showText(cmd["body"]["text"] | "", cmd["body"]["align"] | "");
  if (type == "rehome") {
    g_app->requestRehome();
    return "";
  }
  return "unknown command " + type;
}

static void task(void*) {
  uint32_t retryMs = RETRY_MIN_MS;
  uint64_t lastId = 0;        // commands up to this id have run; a repeat is acknowledged, not re-run
  std::string results = "[]";  // outcome of the commands from the previous reply
  bool sendConfig = true;
  bool wasOk = false;

  for (;;) {
    std::string url, token;
    if (WiFi.status() != WL_CONNECTED || !g_app->remoteSettings(url, token)) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    if (g_app->takeConfigChanged()) sendConfig = true;
    std::string body = "{\"results\":" + results + ",\"status\":" + g_app->statusJson();
    if (sendConfig) body += ",\"config\":" + g_app->configJson();
    body += "}";

    HttpResponse r = httpPostJson((url + "/api/device/sync").c_str(), token.c_str(), body, REQUEST_TIMEOUT_S);
    JsonDocument doc;
    if (r.code != 200 || deserializeJson(doc, r.body)) {
      // Keep `results` and `sendConfig`: they go out again with the next attempt.
      if (wasOk || retryMs == RETRY_MIN_MS) Serial.printf("[remote] sync failed (%d), retrying\n", r.code);
      wasOk = false;
      vTaskDelay(pdMS_TO_TICKS(retryMs));
      retryMs = retryMs * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : retryMs * 2;
      continue;
    }
    if (!wasOk) Serial.println("[remote] connected to the relay");
    wasOk = true;
    retryMs = RETRY_MIN_MS;
    sendConfig = false;

    JsonDocument out;
    JsonArray done = out.to<JsonArray>();
    for (JsonObjectConst cmd : doc["commands"].as<JsonArrayConst>()) {
      uint64_t id = cmd["id"] | (uint64_t)0;
      JsonObject res = done.add<JsonObject>();
      res["id"] = id;
      if (id > lastId) {
        std::string err = run(cmd);
        if (!err.empty()) {
          res["error"] = err;
          Serial.printf("[remote] command %s rejected: %s\n", (const char*)(cmd["type"] | ""), err.c_str());
          sendConfig = true;  // let the server see the settings as they really are
        }
        lastId = id;
      }
    }
    results.clear();
    serializeJson(out, results);
    // Give the display loop a moment to act, so the next report shows the new frame.
    if (done.size()) vTaskDelay(pdMS_TO_TICKS(400));
  }
}

void Remote::begin(App& app) {
  g_app = &app;
  // Own task: a held request must never stall the display loop. TLS needs a roomy stack.
  xTaskCreatePinnedToCore(task, "remote", 12288, nullptr, 1, nullptr, 1);
}
