// Web UI (LittleFS) + JSON API on port 80. Every POST needs the X-Admin-Key header.
//   GET  /api/status   counts, fetch status, masked tokens, bus health
//   GET  /api/config   settings (admin key omitted)
//   POST /api/config   partial settings update (JSON)
//   POST /api/tokens   {"platform":..,"access_token":..} or {"platform":..,"clear":true}
//   POST /api/rehome   re-home all units
//   POST /api/show     {"text":"..","align":"left|center|right"} -> text mode
#pragma once

class App;

namespace Web {
void begin(App& app);
}
