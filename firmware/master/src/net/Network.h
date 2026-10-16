// Wi-Fi setup (WiFiManager captive portal "SplitFlap-Setup"), mDNS splitflap.local.
// Kept in its own translation unit: WiFiManager's WebServer.h and ESPAsyncWebServer.h clash.
#pragma once

class App;

namespace Network {
// Blocking at boot. Opens the setup portal when there are no Wi-Fi credentials, no admin key yet,
// or the BOOT button is held at power-on. The portal also asks for the admin key.
void begin(App& app);
}  // namespace Network
