#include "web/WebServer.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "app/App.h"

static AsyncWebServer server(80);
static App* g_app = nullptr;
static const size_t MAX_BODY = 8192;  // a full word list (100 words of 32 bytes) is about 3.6 KB

static void sendJson(AsyncWebServerRequest* req, int code, const std::string& json) {
  AsyncWebServerResponse* res = req->beginResponse(code, "application/json", json.c_str());
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

static void sendError(AsyncWebServerRequest* req, int code, const std::string& msg) {
  JsonDocument doc;
  doc["error"] = msg;
  std::string out;
  serializeJson(doc, out);
  sendJson(req, code, out);
}

static bool authorized(AsyncWebServerRequest* req) {
  if (!g_app->adminKeySet()) {
    sendError(req, 403, "no admin key set on the device: hold BOOT while powering on to open the setup portal");
    return false;
  }
  const AsyncWebHeader* h = req->getHeader("X-Admin-Key");
  if (!h || !g_app->checkAdminKey(h->value())) {
    sendError(req, 401, "missing or wrong X-Admin-Key");
    return false;
  }
  return true;
}

// POST route: the body callback buffers the (possibly chunked) body into _tempObject, which the
// library free()s, so it must come from malloc. The request callback runs once the body is complete.
using BodyHandler = std::string (*)(const std::string& body);

static void onPost(const char* path, BodyHandler handler) {
  server.on(
      path, HTTP_POST,
      [handler](AsyncWebServerRequest* req) {
        if (req->contentLength() > MAX_BODY) return sendError(req, 413, "body too large");
        if (!authorized(req)) return;
        std::string body = req->_tempObject ? static_cast<const char*>(req->_tempObject) : "";
        std::string err = handler(body);
        if (err.empty()) sendJson(req, 200, "{\"ok\":true}");
        else sendError(req, 400, err);
      },
      nullptr,
      [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        if (total > MAX_BODY) return;
        if (index == 0) {
          req->_tempObject = malloc(total + 1);
          if (req->_tempObject) static_cast<char*>(req->_tempObject)[total] = '\0';
        }
        if (req->_tempObject && index + len <= total) memcpy(static_cast<char*>(req->_tempObject) + index, data, len);
      });
}

void Web::begin(App& app) {
  g_app = &app;
  if (!LittleFS.begin(true)) Serial.println("[web] LittleFS mount failed - run: pio run -e master -t uploadfs");

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) { sendJson(req, 200, g_app->statusJson()); });
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) { sendJson(req, 200, g_app->configJson()); });

  onPost("/api/config", [](const std::string& body) { return g_app->updateConfig(body); });
  onPost("/api/tokens", [](const std::string& body) { return g_app->updateTokens(body); });
  onPost("/api/rehome", [](const std::string&) {
    g_app->requestRehome();
    return std::string();
  });
  onPost("/api/show", [](const std::string& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return std::string("invalid json");
    return g_app->showText(doc["text"] | "", doc["align"] | "");
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest* req) { sendError(req, 404, "not found"); });
  server.begin();
  Serial.println("[web] listening on port 80");
}
