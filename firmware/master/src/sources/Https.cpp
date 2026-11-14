#include "sources/Https.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

extern const uint8_t x509_crt_bundle_start[] asm("_binary_firmware_master_certs_x509_crt_bundle_bin_start");

static const size_t MAX_BODY = 8 * 1024;  // follower responses are a few hundred bytes

// Collects a response body and refuses bytes past its limit, so a chunked or lying
// reply can't grow the heap: HTTPClient stops reading when a write comes up short.
class CappedSink : public Stream {
 public:
  CappedSink(std::string& out, size_t cap) : out_(out), cap_(cap) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    if (out_.size() + n > cap_) {
      overflow_ = true;
      return 0;
    }
    out_.append((const char*)buf, n);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool overflowed() const { return overflow_; }

 private:
  std::string& out_;
  size_t cap_;
  bool overflow_ = false;
};

// Reads the body into r.body, at most cap bytes. An oversized body leaves it empty with code HTTP_TOO_BIG.
static const int HTTP_TOO_BIG = -101;
static void readBody(HTTPClient& http, HttpResponse& r, size_t cap) {
  int len = http.getSize();
  if (len > 0 && (size_t)len > cap) {
    r.code = HTTP_TOO_BIG;
    return;
  }
  CappedSink sink(r.body, cap);
  int got = http.writeToStream(&sink);
  if (sink.overflowed() || got < 0) {
    r.body.clear();
    if (sink.overflowed()) r.code = HTTP_TOO_BIG;
  }
}

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
    r.retryAfterSec = http.header("Retry-After").toInt();
    readBody(http, r, MAX_BODY);
  }
  http.end();
  return r;
}

HttpResponse httpsGet(const String& url, const String& bearer) { return request("GET", url, bearer, nullptr); }

HttpResponse httpsPostForm(const String& url, const String& formBody) { return request("POST", url, "", &formBody); }

HttpResponse httpPostJson(const String& url, const String& bearer, const std::string& json, uint16_t timeoutSec) {
  static const size_t MAX_REPLY = 16 * 1024;  // a queued word list is under 4 KB
  HttpResponse r;
  WiFiClientSecure tls;
  WiFiClient plain;
  // Plain http is only reachable for a local test server: applyConfigJson refuses it for anything else.
  const bool secure = url.startsWith("https://");
  if (secure) tls.setCACertBundle(x509_crt_bundle_start);
  WiFiClient& client = secure ? static_cast<WiFiClient&>(tls) : plain;
  client.setTimeout(timeoutSec);  // seconds

  HTTPClient http;
  http.setTimeout((uint16_t)(timeoutSec * 1000));
  http.setConnectTimeout(15000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    r.code = -100;
    return r;
  }
  http.addHeader("Authorization", "Bearer " + bearer);
  http.addHeader("Content-Type", "application/json");
  r.code = http.POST((uint8_t*)json.data(), json.size());
  if (r.code > 0) readBody(http, r, MAX_REPLY);
  http.end();
  return r;
}

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
