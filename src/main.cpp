#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <bearssl/bearssl_hmac.h>
#include <bearssl/bearssl_hash.h>
#include <stddef.h>
#include <time.h>
#include "web_ui.h"
#include "tls_roots.h"

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x4553503C;
constexpr size_t EEPROM_SIZE = 4096;
constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_ATTEMPT_DELAY_MS = 800;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t PORTAL_FALLBACK_DELAY_MS = 5000;
constexpr uint32_t PORTAL_HOLD_MS = 600000;
constexpr uint32_t RESTART_DELAY_MS = 900;
constexpr size_t AP_PASSWORD_MIN_LENGTH = 8;
constexpr size_t STREAM_CHUNK_SIZE = 448;
constexpr size_t STREAM_LOOKAHEAD_SIZE = 32;
constexpr size_t MAX_PAGE_TOKENS = 48;
constexpr size_t MAX_SCAN_RESULTS = 24;
constexpr uint16_t DEFAULT_WEB_PORT = 80;
constexpr uint32_t DEFAULT_DDNS_INTERVAL_SEC = 600;
constexpr uint32_t DEFAULT_DDNS_RETRY_SEC = 60;
constexpr uint32_t DEFAULT_DDNS_FORCE_SEC = 86400;
constexpr uint32_t DDNS_INTERVAL_MIN_SEC = 10;
constexpr uint32_t DDNS_INTERVAL_MAX_SEC = 604800;
constexpr uint32_t DDNS_RETRY_MIN_SEC = 10;
constexpr uint32_t DDNS_RETRY_MAX_SEC = 3600;
constexpr uint32_t DDNS_FORCE_MIN_SEC = 600;
constexpr uint32_t DDNS_FORCE_MAX_SEC = 2592000;
constexpr time_t MIN_VALID_EPOCH = 1000000000;
constexpr uint16_t SETUP_AP_CHANNEL_DELAY_MS = 300;
constexpr char CLEAR_SECRET_VALUE[] = "-";
constexpr char SETUP_AP_SSID[] = "ESP8266-Setup";
constexpr char DDNS_GET[] = "GET";
constexpr char DDNS_POST[] = "POST";
const char ALIYUN_API_HOST[] = "https://alidns.aliyuncs.com/?";
const char ALIYUN_API_VERSION[] = "2015-01-09";
const char ALIYUN_RR_DEFAULT[] = "@";
const char DNSPOD_API_HOST[] = "https://dnspod.tencentcloudapi.com";
const char DNSPOD_API_NAME[] = "dnspod.tencentcloudapi.com";
const char DNSPOD_SERVICE[] = "dnspod";
const char DNSPOD_API_VERSION[] = "2021-03-23";
const char CALLBACK_RECORD_TYPE[] = "A";
const char CALLBACK_RECORD_TTL[] = "600";
const char* const DDNS_PROVIDERS[] = {"generic", "aliyun", "cloudflare", "dnspod"};
const char* const DDNS_RECORD_TYPES[] = {"A", "AAAA", "CNAME"};
const char* const NTP_SERVERS[] = {"pool.ntp.org", "time.nist.gov"};

const char* const DEFAULT_PUBLIC_IP_APIS[] = {
  "https://ipv4.icanhazip.com",
  "https://api.ipify.org",
  "https://checkip.amazonaws.com",
  "https://ifconfig.me/ip"
};
constexpr size_t DEFAULT_PUBLIC_IP_API_COUNT = sizeof(DEFAULT_PUBLIC_IP_APIS) / sizeof(DEFAULT_PUBLIC_IP_APIS[0]);

struct DeviceConfig {
  uint32_t magic;
  char wifiSsid[33];
  char wifiPassword[65];
  char staticIp[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  char ddnsHostname[96];
  char ddnsUsername[48];
  char ddnsPassword[64];
  char ddnsUrl[192];
  char ddnsProvider[16];
  char aliyunAccessKeyId[64];
  char aliyunAccessKeySecret[96];
  char aliyunDomainName[96];
  char aliyunRR[64];
  char aliyunType[8];
  char cloudflareApiToken[96];
  char cloudflareZoneId[40];
  char cloudflareRecordId[40];
  char tencentSecretId[64];
  char tencentSecretKey[96];
  char tencentDomain[96];
  char tencentSubDomain[64];
  char tencentRecordType[8];
  char ddnsIpUrl[128];
  char ddnsSuccess[32];
  char ddnsMethod[8];
  char ddnsBody[256];
  char ddnsHeaders[256];
  char apPassword[65];
  uint32_t ddnsIntervalSec;
  uint32_t ddnsRetrySec;
  uint32_t ddnsForceSec;
  uint16_t webPort;
  uint8_t tlsInsecure;
  uint32_t crc;
};

enum class ConnectJob : uint8_t { None, Probe, Save };

struct ConnectAttempt {
  ConnectJob job = ConnectJob::None;
  uint32_t startAt = 0;
  uint32_t startedAt = 0;
  uint32_t restartAt = 0;
  bool active = false;
  bool started = false;
  bool resultReady = false;
  bool succeeded = false;
  bool dhcpAddresses = false;
};

DeviceConfig config{};
DeviceConfig* stagingConfig = nullptr;
ConnectAttempt connectAttempt;
ESP8266WebServer dashboardServer(DEFAULT_WEB_PORT);
ESP8266WebServer apServer(DEFAULT_WEB_PORT);
bool setupPortalActive = false;
bool serverStarted = false;
bool apRoutesReady = false;
bool dashboardRoutesReady = false;
uint32_t lastWifiRetry = 0;
uint32_t portalHoldUntil = 0;
String currentDdnsIp;
String lastDdnsIp;
uint32_t lastDdnsSync = 0;
uint32_t nextDdnsAttempt = 0;

bool hostMatches(const String& url, const char* keyword) {
  return url.indexOf(keyword) >= 0;
}

const char* rootPemForUrl(const String& url) {
  if (hostMatches(url, "ipify.org") || hostMatches(url, "icanhazip.com") || hostMatches(url, "cloudflare.com")) {
    return TLS_ROOT_GTS_R4;
  }
  if (hostMatches(url, "amazonaws.com")) {
    return TLS_ROOT_AMAZON_CA1;
  }
  if (hostMatches(url, "aliyuncs.com")) {
    return TLS_ROOT_GLOBALSIGN_R3;
  }
  if (hostMatches(url, "tencentcloudapi.com")) {
    return TLS_ROOT_DIGICERT_G2;
  }
  return TLS_ROOT_ISRG_X1;
}

struct SecureConnection {
  std::unique_ptr<BearSSL::X509List> anchors;
  std::unique_ptr<BearSSL::WiFiClientSecure> client;

  explicit SecureConnection(const String& url) {
    client.reset(new BearSSL::WiFiClientSecure);
    if (config.tlsInsecure) {
      client->setInsecure();
      return;
    }
    anchors.reset(new BearSSL::X509List(rootPemForUrl(url)));
    if (anchors->getCount() == 0) {
      Serial.println("证书信任根加载失败，本次请求将失败");
    }
    client->setTrustAnchors(anchors.get());
    client->setBufferSizes(4096, 512);
  }
};

DeviceConfig& staging() {
  if (stagingConfig == nullptr) {
    stagingConfig = new DeviceConfig(config);
  }
  return *stagingConfig;
}

void releaseStaging() {
  delete stagingConfig;
  stagingConfig = nullptr;
}

bool copyText(char* target, size_t targetSize, const String& value) {
  bool fits = value.length() + 1 <= targetSize;
  if (!fits) {
    Serial.printf("字段超长已截断: %u -> %u\n",
                  static_cast<unsigned>(value.length()),
                  static_cast<unsigned>(targetSize - 1));
  }
  value.toCharArray(target, targetSize);
  return fits;
}

void applySecretValue(char* field, size_t size, const String& submitted) {
  String value = submitted;
  value.trim();
  if (value.length() == 0) {
    return;
  }
  if (value == CLEAR_SECRET_VALUE) {
    copyText(field, size, "");
    return;
  }
  copyText(field, size, value);
}

bool isAllowedValue(const char* value, const char* const* allowed, size_t count) {
  for (size_t index = 0; index < count; index++) {
    if (strcmp(value, allowed[index]) == 0) {
      return true;
    }
  }
  return false;
}

void normalizeProvider(char* provider, size_t size) {
  if (!isAllowedValue(provider, DDNS_PROVIDERS, sizeof(DDNS_PROVIDERS) / sizeof(DDNS_PROVIDERS[0]))) {
    copyText(provider, size, DDNS_PROVIDERS[0]);
  }
}

void normalizeRecordType(char* recordType, size_t size) {
  if (!isAllowedValue(recordType, DDNS_RECORD_TYPES, sizeof(DDNS_RECORD_TYPES) / sizeof(DDNS_RECORD_TYPES[0]))) {
    copyText(recordType, size, CALLBACK_RECORD_TYPE);
  }
}

bool isCallbackRecordType(const char* recordType) {
  return strcmp(recordType, CALLBACK_RECORD_TYPE) == 0;
}

bool parsePort(const String& text, uint16_t& port) {
  String value = text;
  value.trim();
  if (value.length() == 0) {
    return false;
  }
  for (size_t index = 0; index < value.length(); index++) {
    if (!isDigit(value[index])) {
      return false;
    }
  }
  long parsed = value.toInt();
  if (parsed < 1 || parsed > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(parsed);
  return true;
}

uint32_t randomWord() {
  return RANDOM_REG32;
}

String makeNonce() {
  String nonce(ESP.getChipId(), HEX);
  nonce += '-'; nonce += String(randomWord(), HEX);
  nonce += '-'; nonce += String(micros(), HEX);
  nonce += '-'; nonce += String(ESP.getCycleCount(), HEX);
  return nonce;
}

void startClock() {
  configTime(0, 0, NTP_SERVERS[0], NTP_SERVERS[1]);
}

uint32_t crc32Of(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
  }
  return ~crc;
}

uint32_t configChecksum() {
  return crc32Of(reinterpret_cast<const uint8_t*>(&config), offsetof(DeviceConfig, crc));
}

bool saveConfig();

void setDefaults() {
  memset(&config, 0, sizeof(config));
  config.magic = CONFIG_MAGIC;
  copyText(config.staticIp, sizeof(config.staticIp), "192.168.1.50");
  copyText(config.gateway, sizeof(config.gateway), "192.168.1.1");
  copyText(config.subnet, sizeof(config.subnet), "255.255.255.0");
  copyText(config.dns, sizeof(config.dns), "8.8.8.8");
  copyText(config.ddnsProvider, sizeof(config.ddnsProvider), "generic");
  copyText(config.aliyunRR, sizeof(config.aliyunRR), "@");
  copyText(config.aliyunType, sizeof(config.aliyunType), "A");
  copyText(config.tencentRecordType, sizeof(config.tencentRecordType), "A");
  copyText(config.ddnsIpUrl, sizeof(config.ddnsIpUrl), DEFAULT_PUBLIC_IP_APIS[0]);
  copyText(config.ddnsMethod, sizeof(config.ddnsMethod), DDNS_GET);
  config.ddnsIntervalSec = DEFAULT_DDNS_INTERVAL_SEC;
  config.ddnsRetrySec = DEFAULT_DDNS_RETRY_SEC;
  config.ddnsForceSec = DEFAULT_DDNS_FORCE_SEC;
  config.webPort = DEFAULT_WEB_PORT;
  config.tlsInsecure = 0;
}

bool configRangeValid() {
  return config.ddnsIntervalSec >= DDNS_INTERVAL_MIN_SEC && config.ddnsIntervalSec <= DDNS_INTERVAL_MAX_SEC &&
         config.ddnsRetrySec >= DDNS_RETRY_MIN_SEC && config.ddnsRetrySec <= DDNS_RETRY_MAX_SEC &&
         config.ddnsForceSec >= DDNS_FORCE_MIN_SEC && config.ddnsForceSec <= DDNS_FORCE_MAX_SEC &&
         config.webPort != 0;
}

bool loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  if (config.magic != CONFIG_MAGIC || config.crc != configChecksum() || !configRangeValid()) {
    Serial.println("配置无效或已损坏，恢复默认值");
    setDefaults();
    saveConfig();
    return false;
  }
  normalizeProvider(config.ddnsProvider, sizeof(config.ddnsProvider));
  normalizeRecordType(config.aliyunType, sizeof(config.aliyunType));
  normalizeRecordType(config.tencentRecordType, sizeof(config.tencentRecordType));
  if (strcasecmp(config.ddnsMethod, DDNS_POST) != 0) {
    copyText(config.ddnsMethod, sizeof(config.ddnsMethod), DDNS_GET);
  }
  return true;
}

bool saveConfig() {
  config.magic = CONFIG_MAGIC;
  config.crc = configChecksum();
  EEPROM.put(0, config);
  if (EEPROM.commit()) {
    return true;
  }
  Serial.println("配置写入 EEPROM 失败");
  return false;
}

bool parseIp(const char* text, IPAddress& address) {
  return address.fromString(text);
}

bool configureStaticIp() {
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
  if (!parseIp(config.staticIp, ip) || !parseIp(config.gateway, gateway) ||
      !parseIp(config.subnet, subnet) || !parseIp(config.dns, dns)) {
    return false;
  }
  return WiFi.config(ip, gateway, subnet, dns);
}

uint32_t ipToNumber(const IPAddress& address) {
  return (static_cast<uint32_t>(address[0]) << 24) |
         (static_cast<uint32_t>(address[1]) << 16) |
         (static_cast<uint32_t>(address[2]) << 8) |
         address[3];
}

IPAddress numberToIp(uint32_t value) {
  return IPAddress((value >> 24) & 0xff, (value >> 16) & 0xff,
                   (value >> 8) & 0xff, value & 0xff);
}

bool findAvailableIp(IPAddress& selectedIp) {
  IPAddress preferredIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
  if (!parseIp(config.staticIp, preferredIp) || !parseIp(config.gateway, gateway) ||
      !parseIp(config.subnet, subnet) || !parseIp(config.dns, dns)) {
    return false;
  }
  uint32_t network = ipToNumber(preferredIp) & ipToNumber(subnet);
  uint32_t broadcast = network | ~ipToNumber(subnet);
  uint32_t candidate = ipToNumber(preferredIp);
  if (candidate <= network || candidate >= broadcast) {
    candidate = network + 1;
  }
  while (candidate < broadcast) {
    IPAddress candidateIp = numberToIp(candidate);
    if (candidateIp == WiFi.localIP() || !Ping.ping(candidateIp, 1)) {
      selectedIp = candidateIp;
      return true;
    }
    Serial.printf("IP %s 已占用，顺延测试\n", candidateIp.toString().c_str());
    candidate++;
  }
  return false;
}

bool connectWithAvailableStaticIp() {
  IPAddress selectedIp;
  if (!findAvailableIp(selectedIp)) {
    return false;
  }
  if (selectedIp.toString() != String(config.staticIp)) {
    Serial.printf("静态 IP 冲突，改用 %s\n", selectedIp.toString().c_str());
    copyText(config.staticIp, sizeof(config.staticIp), selectedIp.toString());
    saveConfig();
  }
  WiFi.disconnect();
  delay(100);
  if (!configureStaticIp()) {
    return false;
  }
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t index = 0; index < value.length(); index++) {
    switch (value[index]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[index];
    }
  }
  return escaped;
}

void redirectToRoot(ESP8266WebServer& target) {
  target.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  target.sendHeader("Cache-Control", "no-store");
  target.send(302, "text/plain", "");
}

void sendJson(ESP8266WebServer& target, int code, const String& body) {
  target.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  target.send(code, "application/json", body);
}

String escapeJson(const String& value);

void appendNetworkAddresses(String& json) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  json += ",\"ssid\":\"" + escapeJson(WiFi.SSID()) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += ",\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";
  json += ",\"subnet\":\"" + WiFi.subnetMask().toString() + "\"";
  json += ",\"dns\":\"" + WiFi.dnsIP().toString() + "\"";
  json += ",\"rssi\":" + String(WiFi.RSSI());
}

String currentNetworkJson(bool ok) {
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  if (ok) {
    appendNetworkAddresses(json);
  }
  json += "}";
  return json;
}

void applyDhcpAddresses() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  IPAddress ip = WiFi.localIP();
  if (static_cast<uint32_t>(ip) != 0) {
    copyText(config.staticIp, sizeof(config.staticIp), ip.toString());
  }
  IPAddress gateway = WiFi.gatewayIP();
  if (static_cast<uint32_t>(gateway) != 0) {
    copyText(config.gateway, sizeof(config.gateway), gateway.toString());
  }
  IPAddress subnet = WiFi.subnetMask();
  if (static_cast<uint32_t>(subnet) != 0) {
    copyText(config.subnet, sizeof(config.subnet), subnet.toString());
  }
  IPAddress dns = WiFi.dnsIP();
  if (static_cast<uint32_t>(dns) != 0) {
    copyText(config.dns, sizeof(config.dns), dns.toString());
  }
}

void fillInvalidAddress(char* field, size_t size, IPAddress value) {
  IPAddress parsed;
  if (static_cast<uint32_t>(value) == 0) {
    return;
  }
  String text = field;
  text.trim();
  if (text.length() == 0 || !parseIp(field, parsed)) {
    copyText(field, size, value.toString());
  }
}

void applyAddressFallback() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  fillInvalidAddress(config.staticIp, sizeof(config.staticIp), WiFi.localIP());
  fillInvalidAddress(config.gateway, sizeof(config.gateway), WiFi.gatewayIP());
  fillInvalidAddress(config.subnet, sizeof(config.subnet), WiFi.subnetMask());
  fillInvalidAddress(config.dns, sizeof(config.dns), WiFi.dnsIP());
}

String messageBlock(const String& message, const char* kind = "") {
  if (message.length() == 0) {
    return "";
  }
  return "<div class=\"alert " + String(kind) + "\">" + htmlEscape(message) + "</div>";
}

struct PageToken {
  const char* key;
  String value;
};

struct TokenCollector {
  PageToken* tokens;
  size_t count;
  size_t capacity;
};

void replaceAll(TokenCollector& collector, const char* key, const String& value) {
  if (collector.count >= collector.capacity) {
    Serial.printf("页面占位符超出容量，已忽略: %s\n", key);
    return;
  }
  collector.tokens[collector.count].key = key;
  collector.tokens[collector.count].value = value;
  collector.count++;
}

size_t matchingTokenLength(const char* text, size_t length, const PageToken* tokens, size_t count) {
  for (size_t index = 0; index < count; index++) {
    size_t keyLength = strlen(tokens[index].key);
    if (length >= keyLength && memcmp(tokens[index].key, text, keyLength) == 0) {
      return keyLength;
    }
  }
  return 0;
}

const char PAGE_HEAD[] PROGMEM = "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>ESP8266 Core</title><style>";
const char PAGE_STYLE_END[] PROGMEM = "</style><script>";
const char PAGE_BODY_OPEN[] PROGMEM = "</script></head><body>";
const char PAGE_END[] PROGMEM = "</body></html>";
const char BUSY_PAGE[] PROGMEM = "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>ESP8266 Core</title></head><body style=\"margin:0;padding:28px;background:#0c1118;color:#e7edf5;font:15px/1.7 Arial,'Microsoft YaHei',sans-serif\"><h1 style=\"font-size:18px;margin:0 0 10px\">__TITLE__</h1><p style=\"color:#9fb0c4;margin:0\">__BODY__</p></body></html>";

size_t streamSection(ESP8266WebServer* target, const char* section, const PageToken* tokens, size_t count) {
  size_t total = strlen_P(reinterpret_cast<PGM_P>(section));
  if (count == 0) {
    if (target != nullptr) {
      target->sendContent_P(section);
    }
    return total;
  }
  const size_t chunkSize = STREAM_CHUNK_SIZE;
  const size_t lookSize = STREAM_LOOKAHEAD_SIZE;
  char buffer[STREAM_CHUNK_SIZE + STREAM_LOOKAHEAD_SIZE];
  size_t position = 0;
  size_t written = 0;
  while (position < total) {
    size_t want = total - position;
    if (want > chunkSize) {
      want = chunkSize;
    }
    size_t look = total - position - want;
    if (look > lookSize) {
      look = lookSize;
    }
    size_t available = want + look;
    memcpy_P(buffer, section + position, available);
    size_t cut = want;
    size_t windowStart = want > lookSize ? want - lookSize : 0;
    for (size_t index = want; index-- > windowStart;) {
      if (buffer[index] == '_' && buffer[index + 1] == '_') {
        if (index > 0) {
          size_t keyLength = matchingTokenLength(buffer + index, available - index, tokens, count);
          if (keyLength > 0 && index + keyLength > want) {
            cut = index;
          }
        }
        break;
      }
    }
    String chunk;
    chunk.reserve(cut + 1);
    chunk.concat(buffer, static_cast<unsigned int>(cut));
    for (size_t index = 0; index < count; index++) {
      if (chunk.indexOf(tokens[index].key) >= 0) {
        chunk.replace(tokens[index].key, tokens[index].value);
      }
    }
    if (target != nullptr) {
      target->sendContent(chunk);
    }
    written += chunk.length();
    position += cut;
  }
  return written;
}

size_t measurePage(const char* body, const PageToken* tokens, size_t count) {
  size_t length = strlen_P(reinterpret_cast<PGM_P>(PAGE_HEAD));
  length += strlen_P(reinterpret_cast<PGM_P>(WEB_STYLE));
  length += strlen_P(reinterpret_cast<PGM_P>(PAGE_STYLE_END));
  length += strlen_P(reinterpret_cast<PGM_P>(WEB_SCRIPT));
  length += strlen_P(reinterpret_cast<PGM_P>(PAGE_BODY_OPEN));
  length += streamSection(nullptr, body, tokens, count);
  length += strlen_P(reinterpret_cast<PGM_P>(PAGE_END));
  return length;
}

void sendHtmlPage(ESP8266WebServer& target, int code, const char* body, const PageToken* tokens, size_t count) {
  target.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  target.sendHeader("Pragma", "no-cache");
  target.sendHeader("Expires", "0");
  target.setContentLength(measurePage(body, tokens, count));
  target.send(code, "text/html; charset=utf-8", "");
  streamSection(&target, PAGE_HEAD, nullptr, 0);
  streamSection(&target, WEB_STYLE, nullptr, 0);
  streamSection(&target, PAGE_STYLE_END, nullptr, 0);
  streamSection(&target, WEB_SCRIPT, nullptr, 0);
  streamSection(&target, PAGE_BODY_OPEN, nullptr, 0);
  streamSection(&target, body, tokens, count);
  streamSection(&target, PAGE_END, nullptr, 0);
}

void sendStandalonePage(ESP8266WebServer& target, const char* body, const PageToken* tokens, size_t count) {
  target.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  target.setContentLength(streamSection(nullptr, body, tokens, count));
  target.send(200, "text/html; charset=utf-8", "");
  streamSection(&target, body, tokens, count);
}

void sendBusyPage() {
  PageToken tokens[4];
  TokenCollector page = {tokens, 0, 4};
  replaceAll(page, "__TITLE__", "正在连接 WiFi");
  replaceAll(page, "__BODY__", "设备正在连接你选择的网络，本页会瞬时断开。请连接该 WiFi，并用设备 IP 打开管理页。");
  sendStandalonePage(apServer, BUSY_PAGE, tokens, page.count);
}

void clearConnectResult() {
  if (connectAttempt.active) {
    return;
  }
  connectAttempt.job = ConnectJob::None;
  connectAttempt.resultReady = false;
  connectAttempt.succeeded = false;
  connectAttempt.restartAt = 0;
}

const char* connectStateName() {
  if (connectAttempt.active) {
    return "pending";
  }
  if (!connectAttempt.resultReady) {
    return "idle";
  }
  return connectAttempt.succeeded ? "ok" : "fail";
}

String connectStateJson() {
  String json = "{\"state\":\"";
  json += connectStateName();
  json += "\",\"job\":\"";
  switch (connectAttempt.job) {
    case ConnectJob::Probe: json += "probe"; break;
    case ConnectJob::Save: json += "save"; break;
    default: json += "none"; break;
  }
  json += "\"";
  if (!connectAttempt.active && connectAttempt.resultReady && connectAttempt.succeeded) {
    appendNetworkAddresses(json);
  }
  json += "}";
  return json;
}

void beginConnectAttempt(ConnectJob job, bool dhcpAddresses) {
  connectAttempt.job = job;
  connectAttempt.startAt = millis() + WIFI_ATTEMPT_DELAY_MS;
  connectAttempt.startedAt = 0;
  connectAttempt.active = true;
  connectAttempt.started = false;
  connectAttempt.resultReady = false;
  connectAttempt.succeeded = false;
  connectAttempt.dhcpAddresses = dhcpAddresses;
  connectAttempt.restartAt = 0;
}

void finishConnectAttempt() {
  DeviceConfig& pendingConfig = staging();
  connectAttempt.active = false;
  connectAttempt.resultReady = true;
  if (!connectAttempt.succeeded) {
    if (strlen(config.wifiSsid) > 0) {
      WiFi.begin(config.wifiSsid, config.wifiPassword);
    }
    Serial.println("WiFi 连接失败");
    return;
  }
  Serial.printf("WiFi 连接成功: %s\n", pendingConfig.wifiSsid);
  if (connectAttempt.job == ConnectJob::Probe) {
    portalHoldUntil = millis() + PORTAL_HOLD_MS;
    return;
  }
  if (connectAttempt.job == ConnectJob::Save) {
    config = pendingConfig;
    if (connectAttempt.dhcpAddresses) {
      applyDhcpAddresses();
      Serial.println("已按路由器分配的地址信息自动填写");
    } else {
      applyAddressFallback();
    }
    saveConfig();
    portalHoldUntil = millis() + 3000;
    connectAttempt.restartAt = millis() + 3000;
  }
  releaseStaging();
}

void serviceConnectAttempt() {
  DeviceConfig& pendingConfig = staging();
  if (connectAttempt.active && !connectAttempt.started &&
      static_cast<int32_t>(millis() - connectAttempt.startAt) >= 0) {
    connectAttempt.started = true;
    connectAttempt.startedAt = millis();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(pendingConfig.wifiSsid, pendingConfig.wifiPassword);
    Serial.printf("开始连接 WiFi: %s\n", pendingConfig.wifiSsid);
    return;
  }
  if (connectAttempt.active && connectAttempt.started) {
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == String(pendingConfig.wifiSsid)) {
      connectAttempt.succeeded = true;
      finishConnectAttempt();
    } else if (millis() - connectAttempt.startedAt >= WIFI_CONNECT_TIMEOUT_MS) {
      connectAttempt.succeeded = false;
      finishConnectAttempt();
    }
  }
  if (connectAttempt.restartAt != 0 && static_cast<int32_t>(millis() - connectAttempt.restartAt) >= 0) {
    ESP.restart();
  }
}

struct PageField {
  const char* token;
  const char* value;
};

void addEscapedFields(TokenCollector& page, const PageField* fields, size_t count) {
  for (size_t index = 0; index < count; index++) {
    replaceAll(page, fields[index].token, htmlEscape(fields[index].value));
  }
}

void sendDashboardPage(ESP8266WebServer& target, int code = 200, const String& message = "", const char* kind = "") {
  PageToken* tokens = new PageToken[MAX_PAGE_TOKENS];
  TokenCollector page = {tokens, 0, MAX_PAGE_TOKENS};
  const PageField fields[] = {
    {"__SSID__", config.wifiSsid},
    {"__IP__", config.staticIp},
    {"__GATEWAY__", config.gateway},
    {"__SUBNET__", config.subnet},
    {"__DNS__", config.dns},
    {"__HOSTNAME__", config.ddnsHostname},
    {"__DDNS_USERNAME__", config.ddnsUsername},
    {"__DDNS_URL__", config.ddnsUrl},
    {"__ALIYUN_ID__", config.aliyunAccessKeyId},
    {"__ALIYUN_DOMAIN__", config.aliyunDomainName},
    {"__ALIYUN_RR__", config.aliyunRR},
    {"__CF_ZONE__", config.cloudflareZoneId},
    {"__CF_RECORD__", config.cloudflareRecordId},
    {"__TX_ID__", config.tencentSecretId},
    {"__TX_DOMAIN__", config.tencentDomain},
    {"__TX_SUBDOMAIN__", config.tencentSubDomain},
    {"__IP_URL__", config.ddnsIpUrl},
    {"__SUCCESS__", config.ddnsSuccess},
    {"__BODY__", config.ddnsBody},
    {"__HEADERS__", config.ddnsHeaders},
  };
  bool connected = WiFi.status() == WL_CONNECTED;
  replaceAll(page, "__MESSAGE__", messageBlock(message, kind));
  addEscapedFields(page, fields, sizeof(fields) / sizeof(fields[0]));
  replaceAll(page, "__AP_SSID__", SETUP_AP_SSID);
  replaceAll(page, "__STATUS__", connected ? "已连接" : "未连接");
  replaceAll(page, "__STATUS_KEY__", connected ? "val.on" : "val.off");
  replaceAll(page, "__LAN_IP__", WiFi.localIP().toString());
  replaceAll(page, "__PUBLIC_IP__", currentDdnsIp.length() ? currentDdnsIp : "未获取");
  replaceAll(page, "__WAN_KEY__", currentDdnsIp.length() ? "" : "val.none");
  replaceAll(page, "__PROVIDER__", htmlEscape(config.ddnsProvider));
  replaceAll(page, "__METHOD__", htmlEscape(config.ddnsMethod));
  replaceAll(page, "__ALIYUN_TYPE__", htmlEscape(config.aliyunType));
  replaceAll(page, "__TX_TYPE__", htmlEscape(config.tencentRecordType));
  replaceAll(page, "__PORT__", String(config.webPort));
  replaceAll(page, "__INTERVAL__", String(config.ddnsIntervalSec));
  replaceAll(page, "__RETRY__", String(config.ddnsRetrySec));
  replaceAll(page, "__FORCE__", String(config.ddnsForceSec));
  replaceAll(page, "__TLS_CHECKED__", config.tlsInsecure ? "" : " checked");
  sendHtmlPage(target, code, WEB_DASHBOARD_BODY, tokens, page.count);
  delete[] tokens;
}

void sendSetupPage(ESP8266WebServer& target, int code = 200, const String& message = "", const char* kind = "") {
  PageToken* tokens = new PageToken[MAX_PAGE_TOKENS];
  TokenCollector page = {tokens, 0, MAX_PAGE_TOKENS};
  const PageField fields[] = {
    {"__IP__", config.staticIp},
    {"__GATEWAY__", config.gateway},
    {"__SUBNET__", config.subnet},
    {"__DNS__", config.dns},
  };
  replaceAll(page, "__MESSAGE__", messageBlock(message, kind));
  addEscapedFields(page, fields, sizeof(fields) / sizeof(fields[0]));
  replaceAll(page, "__AP_SSID__", SETUP_AP_SSID);
  replaceAll(page, "__AP_IP__", WiFi.softAPIP().toString());
  replaceAll(page, "__SSID__", htmlEscape(config.wifiSsid));
  replaceAll(page, "__PORT__", String(config.webPort));
  replaceAll(page, "__CONNECT_STATE__", connectStateName());
  sendHtmlPage(target, code, WEB_SETUP_BODY, tokens, page.count);
  delete[] tokens;
}

String escapeJson(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); index++) {
    char character = value[index];
    if (character == '"' || character == '\\') {
      escaped += '\\';
      escaped += character;
      continue;
    }
    if (static_cast<uint8_t>(character) >= 0x20) {
      escaped += character;
    }
  }
  return escaped;
}

void handleApRoot() {
  String message;
  const char* kind = "";
  if (connectAttempt.resultReady && !connectAttempt.succeeded) {
    message = "上次保存失败：无法连接该 WiFi，请检查名称与密码后重试。";
    kind = "err";
  }
  clearConnectResult();
  sendSetupPage(apServer, 200, message, kind);
}

String buildScanJson() {
  String json = "[";
  String seen[MAX_SCAN_RESULTS];
  size_t seenCount = 0;
  int found = WiFi.scanNetworks(false, true);
  for (int index = 0; index < found && seenCount < MAX_SCAN_RESULTS; index++) {
    String ssid = WiFi.SSID(index);
    if (ssid.length() == 0) {
      continue;
    }
    bool duplicate = false;
    for (size_t check = 0; check < seenCount; check++) {
      if (seen[check] == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    seen[seenCount++] = ssid;
    if (seenCount > 1) {
      json += ",";
    }
    json += "{\"s\":\"" + escapeJson(ssid) + "\",\"r\":" + String(WiFi.RSSI(index)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  return json;
}

void handleApScan() {
  sendJson(apServer, 200, buildScanJson());
}

void handleDashboardScan() {
  sendJson(dashboardServer, 200, buildScanJson());
}

void handleApNotFound() {
  redirectToRoot(apServer);
}

void handleApNetInfo() {
  sendJson(apServer, 200, currentNetworkJson(WiFi.status() == WL_CONNECTED));
}

void handleApConnectState() {
  sendJson(apServer, 200, connectStateJson());
}

void handleApProbe() {
  if (apServer.method() == HTTP_GET || connectAttempt.active) {
    sendJson(apServer, 200, connectStateJson());
    return;
  }
  DeviceConfig& pendingConfig = staging();
  String ssid = apServer.arg("ssid");
  ssid.trim();
  if (ssid.length() == 0) {
    sendJson(apServer, 400, "{\"state\":\"fail\",\"reason\":\"ssid\"}");
    return;
  }
  pendingConfig = config;
  copyText(pendingConfig.wifiSsid, sizeof(pendingConfig.wifiSsid), ssid);
  applySecretValue(pendingConfig.wifiPassword, sizeof(pendingConfig.wifiPassword), apServer.arg("wifiPassword"));
  Serial.printf("测试连接 %s\n", pendingConfig.wifiSsid);  beginConnectAttempt(ConnectJob::Probe, false);
  sendJson(apServer, 200, connectStateJson());
}

void handleDashboardNetInfo() {
  sendJson(dashboardServer, 200, currentNetworkJson(WiFi.status() == WL_CONNECTED));
}

void handleApSave() {
  if (connectAttempt.active) {
    sendSetupPage(apServer, 409, "上一次连接仍在进行，请稍候。", "err");
    return;
  }
  DeviceConfig& pendingConfig = staging();
  pendingConfig = config;
  copyText(pendingConfig.wifiSsid, sizeof(pendingConfig.wifiSsid), apServer.arg("ssid"));
  applySecretValue(pendingConfig.wifiPassword, sizeof(pendingConfig.wifiPassword), apServer.arg("wifiPassword"));
  applySecretValue(pendingConfig.apPassword, sizeof(pendingConfig.apPassword), apServer.arg("apPassword"));
  copyText(pendingConfig.staticIp, sizeof(pendingConfig.staticIp), apServer.arg("ip"));
  copyText(pendingConfig.gateway, sizeof(pendingConfig.gateway), apServer.arg("gateway"));
  copyText(pendingConfig.subnet, sizeof(pendingConfig.subnet), apServer.arg("subnet"));
  copyText(pendingConfig.dns, sizeof(pendingConfig.dns), apServer.arg("dns"));
  uint16_t port = 0;
  if (!parsePort(apServer.arg("port"), port)) {
    sendSetupPage(apServer, 400, "Web 端口需为 1-65535 之间的整数。", "err");
    return;
  }
  pendingConfig.webPort = port;
  String ssid = pendingConfig.wifiSsid;
  ssid.trim();
  if (ssid.length() == 0) {
    sendSetupPage(apServer, 400, "请填写或选择 WiFi 名称。", "err");
    return;
  }
  if (strlen(pendingConfig.apPassword) < AP_PASSWORD_MIN_LENGTH) {
    sendSetupPage(apServer, 400, "配置 AP 密码至少需要 8 位。", "err");
    return;
  }
  Serial.printf("尝试连接 WiFi: %s\n", pendingConfig.wifiSsid);
  beginConnectAttempt(ConnectJob::Save, apServer.arg("autoNet") == "1");
  sendBusyPage();
}

void startSetupPortal() {
  if (setupPortalActive) {
    return;
  }
  if (serverStarted) {
    dashboardServer.stop();
    serverStarted = false;
  }
  WiFi.mode(WIFI_AP_STA);
  portalHoldUntil = 0;
  bool secured = strlen(config.apPassword) >= 8;
  WiFi.softAP(SETUP_AP_SSID, secured ? config.apPassword : nullptr);
  delay(SETUP_AP_CHANNEL_DELAY_MS);
  if (!apRoutesReady) {
    apServer.on("/", HTTP_GET, handleApRoot);
    apServer.on("/scan", HTTP_GET, handleApScan);
    apServer.on("/netinfo", HTTP_GET, handleApNetInfo);
    apServer.on("/connectstate", HTTP_GET, handleApConnectState);
    apServer.on("/probe", HTTP_POST, handleApProbe);
    apServer.on("/probe", HTTP_GET, handleApProbe);
    apServer.on("/save", HTTP_POST, handleApSave);
    apServer.onNotFound(handleApNotFound);
    apRoutesReady = true;
  }
  apServer.begin();
  setupPortalActive = true;
  Serial.printf("配置模式: 连接 %s%s，打开 http://%s\n",
                SETUP_AP_SSID,
                secured ? "（使用已设置的 AP 密码）" : "（无需密码）",
                WiFi.softAPIP().toString().c_str());
}

void stopSetupPortal() {
  if (!setupPortalActive) {
    return;
  }
  apServer.stop();
  WiFi.softAPdisconnect(true);
  setupPortalActive = false;
}

void handleRoot() {
  sendDashboardPage(dashboardServer, 200);
}

bool applyFormConfig(ESP8266WebServer& source, String& error) {
  DeviceConfig& pendingConfig = staging();
  pendingConfig = config;
  copyText(pendingConfig.wifiSsid, sizeof(pendingConfig.wifiSsid), source.arg("ssid"));
  applySecretValue(pendingConfig.wifiPassword, sizeof(pendingConfig.wifiPassword), source.arg("wifiPassword"));
  copyText(pendingConfig.staticIp, sizeof(pendingConfig.staticIp), source.arg("ip"));
  copyText(pendingConfig.gateway, sizeof(pendingConfig.gateway), source.arg("gateway"));
  copyText(pendingConfig.subnet, sizeof(pendingConfig.subnet), source.arg("subnet"));
  copyText(pendingConfig.dns, sizeof(pendingConfig.dns), source.arg("dns"));
  copyText(pendingConfig.ddnsHostname, sizeof(pendingConfig.ddnsHostname), source.arg("hostname"));
  copyText(pendingConfig.ddnsUsername, sizeof(pendingConfig.ddnsUsername), source.arg("ddnsUsername"));
  applySecretValue(pendingConfig.ddnsPassword, sizeof(pendingConfig.ddnsPassword), source.arg("ddnsPassword"));
  copyText(pendingConfig.ddnsUrl, sizeof(pendingConfig.ddnsUrl), source.arg("ddnsUrl"));
  copyText(pendingConfig.ddnsProvider, sizeof(pendingConfig.ddnsProvider), source.arg("ddnsProvider"));
  copyText(pendingConfig.aliyunAccessKeyId, sizeof(pendingConfig.aliyunAccessKeyId), source.arg("aliyunAccessKeyId"));
  applySecretValue(pendingConfig.aliyunAccessKeySecret, sizeof(pendingConfig.aliyunAccessKeySecret), source.arg("aliyunAccessKeySecret"));
  copyText(pendingConfig.aliyunDomainName, sizeof(pendingConfig.aliyunDomainName), source.arg("aliyunDomainName"));
  copyText(pendingConfig.aliyunRR, sizeof(pendingConfig.aliyunRR), source.arg("aliyunRR"));
  copyText(pendingConfig.aliyunType, sizeof(pendingConfig.aliyunType), source.arg("aliyunType"));
  applySecretValue(pendingConfig.cloudflareApiToken, sizeof(pendingConfig.cloudflareApiToken), source.arg("cloudflareApiToken"));
  copyText(pendingConfig.cloudflareZoneId, sizeof(pendingConfig.cloudflareZoneId), source.arg("cloudflareZoneId"));
  copyText(pendingConfig.cloudflareRecordId, sizeof(pendingConfig.cloudflareRecordId), source.arg("cloudflareRecordId"));
  copyText(pendingConfig.tencentSecretId, sizeof(pendingConfig.tencentSecretId), source.arg("tencentSecretId"));
  applySecretValue(pendingConfig.tencentSecretKey, sizeof(pendingConfig.tencentSecretKey), source.arg("tencentSecretKey"));
  copyText(pendingConfig.tencentDomain, sizeof(pendingConfig.tencentDomain), source.arg("tencentDomain"));
  copyText(pendingConfig.tencentSubDomain, sizeof(pendingConfig.tencentSubDomain), source.arg("tencentSubDomain"));
  copyText(pendingConfig.tencentRecordType, sizeof(pendingConfig.tencentRecordType), source.arg("tencentRecordType"));
  copyText(pendingConfig.ddnsIpUrl, sizeof(pendingConfig.ddnsIpUrl), source.arg("ddnsIpUrl"));
  copyText(pendingConfig.ddnsSuccess, sizeof(pendingConfig.ddnsSuccess), source.arg("ddnsSuccess"));
  copyText(pendingConfig.ddnsBody, sizeof(pendingConfig.ddnsBody), source.arg("ddnsBody"));
  copyText(pendingConfig.ddnsHeaders, sizeof(pendingConfig.ddnsHeaders), source.arg("ddnsHeaders"));
  normalizeProvider(pendingConfig.ddnsProvider, sizeof(pendingConfig.ddnsProvider));
  normalizeRecordType(pendingConfig.aliyunType, sizeof(pendingConfig.aliyunType));
  normalizeRecordType(pendingConfig.tencentRecordType, sizeof(pendingConfig.tencentRecordType));
  String method = source.arg("ddnsMethod");
  method.toUpperCase();
  copyText(pendingConfig.ddnsMethod, sizeof(pendingConfig.ddnsMethod), method == DDNS_POST ? DDNS_POST : DDNS_GET);
  pendingConfig.tlsInsecure = source.arg("tlsInsecure") == "1" ? 1 : 0;
  uint16_t port = 0;
  if (!parsePort(source.arg("port"), port)) {
    error = "Web 端口需为 1-65535 之间的整数。";
    return false;
  }
  pendingConfig.webPort = port;
  long interval = source.arg("ddnsIntervalSec").toInt();
  if (interval < static_cast<long>(DDNS_INTERVAL_MIN_SEC) || interval > static_cast<long>(DDNS_INTERVAL_MAX_SEC)) {
    error = "DDNS 检查间隔需在 10-604800 秒之间。";
    return false;
  }
  long retry = source.arg("ddnsRetrySec").toInt();
  if (retry < static_cast<long>(DDNS_RETRY_MIN_SEC) || retry > static_cast<long>(DDNS_RETRY_MAX_SEC)) {
    error = "DDNS 失败重试间隔需在 10-3600 秒之间。";
    return false;
  }
  long force = source.arg("ddnsForceSec").toInt();
  if (force < static_cast<long>(DDNS_FORCE_MIN_SEC) || force > static_cast<long>(DDNS_FORCE_MAX_SEC)) {
    error = "DDNS 强制更新间隔需在 600-2592000 秒之间。";
    return false;
  }
  pendingConfig.ddnsIntervalSec = static_cast<uint32_t>(interval);
  pendingConfig.ddnsRetrySec = static_cast<uint32_t>(retry);
  pendingConfig.ddnsForceSec = static_cast<uint32_t>(force);
  applySecretValue(pendingConfig.apPassword, sizeof(pendingConfig.apPassword), source.arg("apPassword"));
  if (strlen(pendingConfig.apPassword) < AP_PASSWORD_MIN_LENGTH) {
    error = "配置 AP 密码至少需要 8 位。";
    return false;
  }
  config = pendingConfig;
  return true;
}

void handleSave() {
  String error;
  if (!applyFormConfig(dashboardServer, error)) {
    sendDashboardPage(dashboardServer, 400, error, "err");
    return;
  }
  applyAddressFallback();
  saveConfig();
  releaseStaging();
  sendDashboardPage(dashboardServer, 200, "配置已保存，设备正在重启。", "ok");
  delay(RESTART_DELAY_MS);
  ESP.restart();
}

void startWebServer() {
  if (serverStarted) {
    return;
  }
  if (!dashboardRoutesReady) {
    dashboardServer.on("/", HTTP_GET, handleRoot);
    dashboardServer.on("/netinfo", HTTP_GET, handleDashboardNetInfo);
    dashboardServer.on("/scan", HTTP_GET, handleDashboardScan);
    dashboardServer.on("/save", HTTP_POST, handleSave);
    dashboardRoutesReady = true;
  }
  dashboardServer.begin(config.webPort);
  serverStarted = true;
  Serial.printf("Web 服务: http://%s:%u\n", WiFi.localIP().toString().c_str(), config.webPort);
}

bool waitForNetworkTime(uint32_t timeoutMs);

String replacePlaceholders(String url) {
  url.replace("{hostname}", config.ddnsHostname);
  url.replace("{ip}", currentDdnsIp);
  url.replace("{user}", config.ddnsUsername);
  url.replace("{password}", config.ddnsPassword);
  url.replace("#{domain}", config.ddnsHostname);
  url.replace("#{ip}", currentDdnsIp);
  url.replace("#{recordType}", CALLBACK_RECORD_TYPE);
  url.replace("#{ttl}", CALLBACK_RECORD_TTL);
  return url;
}

String httpGetBody(const String& url, int& status) {
  SecureConnection secure(url);
  HTTPClient http;
  if (!http.begin(*secure.client, url)) {
    status = -1;
    return "";
  }
  status = http.GET();
  if (status < 0 && !config.tlsInsecure) {
    char message[128];
    secure.client->getLastSSLError(message, sizeof(message));
    Serial.printf("[tls] %s => %s\n", url.c_str(), message);
  }
  String body = http.getString();
  http.end();
  return body;
}

String fetchPublicIp() {
  if (!config.tlsInsecure && !waitForNetworkTime(5000)) {
    Serial.println("系统时间未就绪，跳过公网 IP 获取");
    return "";
  }
  String urls[DEFAULT_PUBLIC_IP_API_COUNT + 1];
  size_t urlCount = 0;
  if (strlen(config.ddnsIpUrl) > 0) {
    urls[urlCount++] = config.ddnsIpUrl;
  }
  for (size_t index = 0; index < DEFAULT_PUBLIC_IP_API_COUNT; index++) {
    if (urlCount == 0 || urls[0] != DEFAULT_PUBLIC_IP_APIS[index]) {
      urls[urlCount++] = DEFAULT_PUBLIC_IP_APIS[index];
    }
  }
  for (size_t index = 0; index < urlCount; index++) {
    int status = 0;
    String address = httpGetBody(urls[index], status);
    address.trim();
    IPAddress parsed;
    if (status == 200 && parsed.fromString(address)) {
      Serial.printf("公网 IP: %s <- %s\n", address.c_str(), urls[index].c_str());
      return address;
    }
    Serial.printf("公网 IP 接口失败: %s (%d)\n", urls[index].c_str(), status);
  }
  return "";
}

bool ddnsResponseOk(int status, const String& response) {
  if (status < 200 || status >= 300) {
    return false;
  }
  return strlen(config.ddnsSuccess) == 0 || response.indexOf(config.ddnsSuccess) >= 0;
}

void addCallbackHeaders(HTTPClient& http) {
  String headers = replacePlaceholders(config.ddnsHeaders);
  int start = 0;
  while (start < static_cast<int>(headers.length())) {
    int end = headers.indexOf('\n', start);
    if (end < 0) {
      end = headers.length();
    }
    String line = headers.substring(start, end);
    line.trim();
    int separator = line.indexOf(':');
    if (separator > 0) {
      String name = line.substring(0, separator);
      String value = line.substring(separator + 1);
      name.trim();
      value.trim();
      http.addHeader(name, value);
    }
    start = end + 1;
  }
}

String percentEncode(const String& value) {
  const char hex[] = "0123456789ABCDEF";
  String encoded;
  for (size_t index = 0; index < value.length(); index++) {
    unsigned char character = value[index];
    if (isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
      encoded += static_cast<char>(character);
    } else {
      encoded += '%';
      encoded += hex[(character >> 4) & 0x0f];
      encoded += hex[character & 0x0f];
    }
  }
  return encoded;
}

String base64Encode(const uint8_t* data, size_t length) {
  const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String result;
  for (size_t index = 0; index < length; index += 3) {
    uint32_t block = static_cast<uint32_t>(data[index]) << 16;
    if (index + 1 < length) {
      block |= static_cast<uint32_t>(data[index + 1]) << 8;
    }
    if (index + 2 < length) {
      block |= data[index + 2];
    }
    result += alphabet[(block >> 18) & 0x3f];
    result += alphabet[(block >> 12) & 0x3f];
    result += index + 1 < length ? alphabet[(block >> 6) & 0x3f] : '=';
    result += index + 2 < length ? alphabet[block & 0x3f] : '=';
  }
  return result;
}

String hmacSha1Base64(const String& key, const String& message) {
  br_hmac_key_context keyContext;
  br_hmac_context hmacContext;
  uint8_t digest[20];
  br_hmac_key_init(&keyContext, &br_sha1_vtable, key.c_str(), key.length());
  br_hmac_init(&hmacContext, &keyContext, sizeof(digest));
  br_hmac_update(&hmacContext, message.c_str(), message.length());
  br_hmac_out(&hmacContext, digest);
  return base64Encode(digest, sizeof(digest));
}

void hmacSha256(const String& key, const String& message, uint8_t* output) {
  br_hmac_key_context keyContext;
  br_hmac_context hmacContext;
  br_hmac_key_init(&keyContext, &br_sha256_vtable, key.c_str(), key.length());
  br_hmac_init(&hmacContext, &keyContext, 32);
  br_hmac_update(&hmacContext, message.c_str(), message.length());
  br_hmac_out(&hmacContext, output);
}

String toHex(const uint8_t* data, size_t length) {
  const char hex[] = "0123456789abcdef";
  String result;
  for (size_t index = 0; index < length; index++) {
    result += hex[(data[index] >> 4) & 0x0f];
    result += hex[data[index] & 0x0f];
  }
  return result;
}

String sha256Hex(const String& value) {
  br_sha256_context context;
  uint8_t digest[32];
  br_sha256_init(&context);
  br_sha256_update(&context, value.c_str(), value.length());
  br_sha256_out(&context, digest);
  return toHex(digest, sizeof(digest));
}

String hmacSha256Hex(const String& key, const String& message) {
  uint8_t digest[32];
  hmacSha256(key, message, digest);
  return toHex(digest, sizeof(digest));
}

String utcTimestamp() {
  time_t now = time(nullptr);
  if (now < MIN_VALID_EPOCH) {
    return "";
  }
  struct tm* utc = gmtime(&now);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", utc);
  return String(buffer);
}

bool waitForNetworkTime(uint32_t timeoutMs) {
  time_t now = time(nullptr);
  uint32_t startedAt = millis();
  while (now < MIN_VALID_EPOCH && millis() - startedAt < timeoutMs) {
    delay(250);
    now = time(nullptr);
  }
  return now >= MIN_VALID_EPOCH;
}

String aliyunCommonParams(const char* action) {
  String params = "AccessKeyId=" + percentEncode(config.aliyunAccessKeyId);
  params += "&Action=";
  params += action;
  params += "&Format=JSON&SignatureMethod=HMAC-SHA1&SignatureNonce=";
  params += makeNonce();
  params += "&SignatureVersion=1.0&Timestamp=" + percentEncode(utcTimestamp());
  params += "&Version=";
  params += ALIYUN_API_VERSION;
  return params;
}

String aliyunRequest(const String& canonicalQuery, int& status) {
  String stringToSign = "GET&%2F&" + percentEncode(canonicalQuery);
  String signature = percentEncode(hmacSha1Base64(String(config.aliyunAccessKeySecret) + "&", stringToSign));
  String url = String(ALIYUN_API_HOST) + canonicalQuery + "&Signature=" + signature;
  return httpGetBody(url, status);
}

String aliyunFindRecordId(int& status) {
  String query = aliyunCommonParams("DescribeDomainRecords");
  query += "&DomainName=" + percentEncode(config.aliyunDomainName);
  query += "&RRKeyWord=" + percentEncode(config.aliyunRR);
  query += "&Type=" + percentEncode(config.aliyunType);
  String response = aliyunRequest(query, status);
  if (status != 200) {
    return "";
  }
  DynamicJsonDocument document(4096);
  if (deserializeJson(document, response)) {
    return "";
  }
  for (JsonObject record : document["DomainRecords"]["Record"].as<JsonArray>()) {
    if (String(record["RR"].as<const char*>()) == config.aliyunRR &&
        String(record["Type"].as<const char*>()) == config.aliyunType) {
      return record["RecordId"].as<String>();
    }
  }
  return "";
}

bool aliyunWriteRecord(const String& recordId) {
  String query = aliyunCommonParams("UpdateDomainRecord");
  query += "&RR=" + percentEncode(config.aliyunRR);
  query += "&RecordId=" + percentEncode(recordId);
  query += "&Type=" + percentEncode(config.aliyunType);
  query += "&Value=" + percentEncode(currentDdnsIp);
  int status = 0;
  String response = aliyunRequest(query, status);
  Serial.printf("阿里云 DDNS 更新状态: %d\n", status);
  return status == 200 && response.indexOf("ErrorCode") < 0;
}

bool updateAliyunDdns() {
  if (strlen(config.aliyunAccessKeyId) == 0 || strlen(config.aliyunAccessKeySecret) == 0 ||
      strlen(config.aliyunDomainName) == 0 || strlen(config.aliyunRR) == 0) {
    Serial.println("阿里云 DDNS 配置不完整");
    return false;
  }
  if (!waitForNetworkTime(5000)) {
    Serial.println("尚未获取网络时间，跳过阿里云 DDNS");
    return false;
  }
  int status = 0;
  String recordId = aliyunFindRecordId(status);
  if (recordId.length() == 0) {
    Serial.printf("阿里云未找到匹配 DNS 记录: %d\n", status);
    return false;
  }
  return aliyunWriteRecord(recordId);
}

bool updateCloudflareDdns() {
  if (strlen(config.cloudflareApiToken) == 0 || strlen(config.cloudflareZoneId) == 0 ||
      strlen(config.cloudflareRecordId) == 0 || strlen(config.ddnsHostname) == 0) {
    Serial.println("Cloudflare DDNS 配置不完整");
    return false;
  }
  String url = "https://api.cloudflare.com/client/v4/zones/";
  url += config.cloudflareZoneId;
  url += "/dns_records/";
  url += config.cloudflareRecordId;
  SecureConnection secure(url);
  HTTPClient http;
  if (!http.begin(*secure.client, url)) {
    Serial.println("Cloudflare API 无法连接");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + String(config.cloudflareApiToken));
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<512> payload;
  payload["type"] = CALLBACK_RECORD_TYPE;
  payload["name"] = config.ddnsHostname;
  payload["content"] = currentDdnsIp;
  payload["ttl"] = 120;
  payload["proxied"] = false;
  String body;
  serializeJson(payload, body);
  int status = http.sendRequest("PATCH", body);
  String response = http.getString();
  http.end();
  Serial.printf("Cloudflare DDNS 更新状态: %d\n", status);
  return status == 200 && response.indexOf("\"success\":true") >= 0;
}

String tencentAuthorization(const String& payload, uint32_t timestamp) {
  const String host = DNSPOD_API_NAME;
  const String service = DNSPOD_SERVICE;
  char dateBuffer[12];
  time_t currentTime = static_cast<time_t>(timestamp);
  strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", gmtime(&currentTime));
  const String date = dateBuffer;
  String canonicalHeaders = "content-type:application/json; charset=utf-8\n";
  canonicalHeaders += "host:" + host + "\n";
  String signedHeaders = "content-type;host";
  String canonicalRequest = "POST\n/\n\n" + canonicalHeaders + "\n" + signedHeaders + "\n" + sha256Hex(payload);
  String credentialScope = date + "/" + service + "/tc3_request";
  String stringToSign = "TC3-HMAC-SHA256\n" + String(timestamp) + "\n" + credentialScope + "\n" + sha256Hex(canonicalRequest);
  String secretDate = hmacSha256Hex("TC3" + String(config.tencentSecretKey), date);
  String secretService = hmacSha256Hex(secretDate, service);
  String secretSigning = hmacSha256Hex(secretService, "tc3_request");
  String signature = hmacSha256Hex(secretSigning, stringToSign);
  return "TC3-HMAC-SHA256 Credential=" + String(config.tencentSecretId) + "/" + credentialScope +
         ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;
}

String tencentRequest(const String& action, const String& payload, int& status) {
  uint32_t timestamp = static_cast<uint32_t>(time(nullptr));
  SecureConnection secure(DNSPOD_API_HOST);
  HTTPClient http;
  if (!http.begin(*secure.client, DNSPOD_API_HOST)) {
    status = -1;
    return "";
  }
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Host", DNSPOD_API_NAME);
  http.addHeader("X-TC-Action", action);
  http.addHeader("X-TC-Version", DNSPOD_API_VERSION);
  http.addHeader("X-TC-Timestamp", String(timestamp));
  http.addHeader("Authorization", tencentAuthorization(payload, timestamp));
  status = http.POST(payload);
  String response = http.getString();
  http.end();
  return response;
}

bool updateTencentDdns() {
  if (strlen(config.tencentSecretId) == 0 || strlen(config.tencentSecretKey) == 0 ||
      strlen(config.tencentDomain) == 0 || strlen(config.tencentSubDomain) == 0) {
    Serial.println("腾讯云 DDNS 配置不完整");
    return false;
  }
  if (!waitForNetworkTime(5000)) {
    Serial.println("尚未获取网络时间，跳过腾讯云 DDNS");
    return false;
  }
  StaticJsonDocument<384> query;
  query["Domain"] = config.tencentDomain;
  query["Subdomain"] = config.tencentSubDomain;
  query["RecordType"] = config.tencentRecordType;
  String payload;
  serializeJson(query, payload);
  int status = 0;
  String response = tencentRequest("DescribeRecordList", payload, status);
  if (status != 200) {
    Serial.printf("腾讯云查询失败: %d\n", status);
    return false;
  }
  DynamicJsonDocument document(4096);
  if (deserializeJson(document, response) || document["Response"]["Error"].is<JsonObject>()) {
    Serial.println("腾讯云响应解析或 API 返回失败");
    return false;
  }
  JsonArray records = document["Response"]["RecordList"].as<JsonArray>();
  if (records.isNull() || records.size() == 0) {
    Serial.println("腾讯云未找到匹配 DNS 记录");
    return false;
  }
  int recordId = records[0]["RecordId"] | 0;
  StaticJsonDocument<512> update;
  update["Domain"] = config.tencentDomain;
  update["RecordId"] = recordId;
  update["SubDomain"] = config.tencentSubDomain;
  update["RecordLine"] = "默认";
  update["Value"] = currentDdnsIp;
  serializeJson(update, payload);
  response = tencentRequest("ModifyRecord", payload, status);
  Serial.printf("腾讯云 DDNS 更新状态: %d\n", status);
  return status == 200 && response.indexOf("\"Error\"") < 0;
}

bool updateGenericDdns() {
  if (strlen(config.ddnsUrl) == 0) {
    return false;
  }
  String url = replacePlaceholders(config.ddnsUrl);
  SecureConnection secure(url);
  HTTPClient http;
  if (!http.begin(*secure.client, url)) {
    Serial.println("DDNS URL 无法连接");
    return false;
  }
  addCallbackHeaders(http);
  if (strlen(config.ddnsUsername) > 0) {
    http.setAuthorization(config.ddnsUsername, config.ddnsPassword);
  }
  String method = config.ddnsMethod;
  method.toUpperCase();
  String body = replacePlaceholders(config.ddnsBody);
  int status = http.sendRequest(method.c_str(), body);
  String response = http.getString();
  http.end();
  bool success = ddnsResponseOk(status, response);
  Serial.printf("通用 DDNS 状态: %d\n", status);
  return success;
}

void scheduleDdnsAttempt(bool success) {
  uint32_t delaySeconds = success ? config.ddnsIntervalSec : config.ddnsRetrySec;
  nextDdnsAttempt = millis() + delaySeconds * 1000UL;
}

bool providerSupportsRecordType() {
  if (strcmp(config.ddnsProvider, "aliyun") == 0) {
    return isCallbackRecordType(config.aliyunType);
  }
  if (strcmp(config.ddnsProvider, "dnspod") == 0) {
    return isCallbackRecordType(config.tencentRecordType);
  }
  return true;
}

void updateDdns() {
  if (WiFi.status() != WL_CONNECTED) {
    scheduleDdnsAttempt(false);
    return;
  }
  currentDdnsIp = fetchPublicIp();
  if (currentDdnsIp.length() == 0) {
    Serial.println("公网 IP 获取失败");
    scheduleDdnsAttempt(false);
    return;
  }
  if (!providerSupportsRecordType()) {
    Serial.println("当前记录类型不支持自动更新，已跳过写入");
    lastDdnsIp = currentDdnsIp;
    lastDdnsSync = millis();
    scheduleDdnsAttempt(true);
    return;
  }
  bool forceUpdate = lastDdnsSync == 0 || millis() - lastDdnsSync >= config.ddnsForceSec * 1000UL;
  if (currentDdnsIp == lastDdnsIp && !forceUpdate) {
    Serial.printf("公网 IP 未变化: %s\n", currentDdnsIp.c_str());
    scheduleDdnsAttempt(true);
    return;
  }
  bool success = false;
  if (strcmp(config.ddnsProvider, "aliyun") == 0) {
    success = updateAliyunDdns();
  } else if (strcmp(config.ddnsProvider, "cloudflare") == 0) {
    success = updateCloudflareDdns();
  } else if (strcmp(config.ddnsProvider, "dnspod") == 0) {
    success = updateTencentDdns();
  } else {
    success = updateGenericDdns();
  }
  if (success) {
    lastDdnsIp = currentDdnsIp;
    lastDdnsSync = millis();
    Serial.println("DDNS 同步成功");
  } else {
    Serial.println("DDNS 同步失败，稍后重试");
  }
  scheduleDdnsAttempt(success);
}

bool connectWifi() {
  if (strlen(config.wifiSsid) == 0) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  return connectWithAvailableStaticIp();
}
}

void setup() {
  Serial.begin(115200);
  delay(100);
  loadConfig();
  if (connectWifi()) {
    startClock();
    startWebServer();
    updateDdns();
  } else {
    startSetupPortal();
  }
}

void loop() {
  serviceConnectAttempt();
  if (setupPortalActive) {
    apServer.handleClient();
    if (!connectAttempt.active) {
      bool hold = portalHoldUntil != 0 && static_cast<int32_t>(millis() - portalHoldUntil) < 0;
      bool backToConfiguredNetwork = strlen(config.wifiSsid) > 0 &&
                                     WiFi.status() == WL_CONNECTED &&
                                     WiFi.SSID() == String(config.wifiSsid);
      if (!hold && backToConfiguredNetwork) {
        stopSetupPortal();
      }
    }
  } else if (connectAttempt.active) {
    startSetupPortal();
    apServer.handleClient();
  } else if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted) {
      startClock();
      startWebServer();
      updateDdns();
    }
    dashboardServer.handleClient();
    if (static_cast<int32_t>(millis() - nextDdnsAttempt) >= 0) {
      updateDdns();
    }
  } else {
    if (serverStarted) {
      dashboardServer.stop();
      serverStarted = false;
    }
    if (millis() - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetry = millis();
      Serial.println("WiFi 已断开，尝试重连");
      WiFi.reconnect();
    }
    if (millis() - lastWifiRetry >= PORTAL_FALLBACK_DELAY_MS) {
      startSetupPortal();
    }
  }
  delay(2);
}
