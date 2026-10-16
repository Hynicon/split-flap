#include "sources/Https.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

extern const uint8_t x509_crt_bundle_start[] asm("_binary_firmware_master_certs_x509_crt_bundle_bin_start");

static const size_t MAX_BODY = 8 * 1024;  // follower responses are a few hundred bytes

static HttpResponse request(const char* method, const String& url, const String& bearer, const String* form) {
  HttpResponse r;
  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start);
  client.setTimeout(15);  // seconds

  HTTPClient http;
  http.setTimeout(15000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    r.code = -100;
    return r;
  }
  const char* headers[] = {"Retry-After"};
  http.collectHeaders(headers, 1);
  if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);

  if (form) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    r.code = http.sendRequest(method, *form);
  } else {
    r.code = http.sendRequest(method);
  }
  if (r.code > 0) {
    int len = http.getSize();
    if (len < 0 || (size_t)len <= MAX_BODY) {
      String body = http.getString();
      r.body.assign(body.c_str(), body.length() > MAX_BODY ? MAX_BODY : body.length());
    }
    r.retryAfterSec = http.header("Retry-After").toInt();
  }
  http.end();
  return r;
}

HttpResponse httpsGet(const String& url, const String& bearer) { return request("GET", url, bearer, nullptr); }

HttpResponse httpsPostForm(const String& url, const String& formBody) { return request("POST", url, "", &formBody); }

String urlEncode(const String& s) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += HEX_DIGITS[c >> 4];
      out += HEX_DIGITS[c & 0xF];
    }
  }
  return out;
}
