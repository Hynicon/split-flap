// Relay client: lets the app reach the display when nothing can connect in to it (for example
// behind a phone hotspot). The display calls out to the relay server, reports its status and
// settings, and receives queued commands. Protocol: firmware/PROTOCOL.md, "Relay".
#pragma once

class App;

namespace Remote {
// Starts the background task. It idles until a relay URL and token are set in the settings.
void begin(App& app);
}  // namespace Remote
