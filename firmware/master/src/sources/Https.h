// Minimal HTTPS helper. Verifies servers against the embedded Mozilla CA bundle
// (firmware/master/certs/x509_crt_bundle.bin, regenerate with tools/gen_cert_bundle.py). No setInsecure().
#pragma once

#include <Arduino.h>

#include <string>

struct HttpResponse {
  int code = 0;              // HTTP status, or < 0 for transport errors (see HTTPClient)
  std::string body;
  uint32_t retryAfterSec = 0;
};

// Never log `url`, `bearer` or `formBody`: they may contain tokens.
HttpResponse httpsGet(const String& url, const String& bearer = "");
HttpResponse httpsPostForm(const String& url, const String& formBody);
// JSON POST for the relay server, which may hold the request open (long-poll), hence the timeout.
// Also accepts plain http:// URLs, for a relay on the local network.
HttpResponse httpPostJson(const String& url, const String& bearer, const std::string& json, uint16_t timeoutSec);

String urlEncode(const String& s);
