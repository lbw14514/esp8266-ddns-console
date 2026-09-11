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
#include <time.h>
#include "web_ui.h"

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x4553503C;
constexpr size_t EEPROM_SIZE = 4096;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
constexpr uint32_t DEFAULT_DDNS_INTERVAL_SEC = 600;
constexpr uint32_t DEFAULT_DDNS_RETRY_SEC = 60;
constexpr uint32_t DEFAULT_DDNS_FORCE_SEC = 86400;
constexpr uint16_t SETUP_AP_CHANNEL_DELAY_MS = 300;
const char SETUP_AP_SSID[] = "ESP8266-Setup";

const char* const DEFAULT_PUBLIC_IP_APIS[] = {
  "https://api.ipify.org",
  "https://checkip.amazonaws.com",
  "https://ipv4.icanhazip.com",
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
};

DeviceConfig config{};
ESP8266WebServer server(80);
ESP8266WebServer* activeServer = &server;
ESP8266WebServer apServer(80);
bool setupPortalActive = false;
bool serverStarted = false;
bool apRoutesReady = false;
uint32_t lastWifiRetry = 0;
uint32_t portalHoldUntil = 0;
String currentDdnsIp;
String lastDdnsIp;
uint32_t lastDdnsSync = 0;
uint32_t nextDdnsAttempt = 0;

void copyText(char* target, size_t targetSize, const String& value) {
  value.toCharArray(target, targetSize);
}

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
  copyText(config.ddnsMethod, sizeof(config.ddnsMethod), "GET");
  config.ddnsIntervalSec = DEFAULT_DDNS_INTERVAL_SEC;
  config.ddnsRetrySec = DEFAULT_DDNS_RETRY_SEC;
  config.ddnsForceSec = DEFAULT_DDNS_FORCE_SEC;
  config.webPort = 80;
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  if (config.magic != CONFIG_MAGIC || config.webPort == 0) {
    setDefaults();
    return;
  }
  if (config.ddnsIntervalSec < 10 || config.ddnsIntervalSec > 604800) {
    config.ddnsIntervalSec = DEFAULT_DDNS_INTERVAL_SEC;
  }
  if (config.ddnsRetrySec < 10 || config.ddnsRetrySec > 3600) {
    config.ddnsRetrySec = DEFAULT_DDNS_RETRY_SEC;
  }
  if (config.ddnsForceSec < 600 || config.ddnsForceSec > 2592000) {
    config.ddnsForceSec = DEFAULT_DDNS_FORCE_SEC;
  }
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
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
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

String htmlEscape(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
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

String currentNetworkJson(bool ok) {
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  if (ok) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";
    json += ",\"subnet\":\"" + WiFi.subnetMask().toString() + "\"";
    json += ",\"dns\":\"" + WiFi.dnsIP().toString() + "\"";
    json += ",\"rssi\":" + String(WiFi.RSSI());
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
};

void replaceAll(TokenCollector& collector, const char* key, const String& value) {
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

size_t streamSection(ESP8266WebServer* target, const char* section, const PageToken* tokens, size_t count) {
  size_t total = strlen_P(reinterpret_cast<PGM_P>(section));
  if (count == 0) {
    if (target != nullptr) {
      target->sendContent_P(section);
    }
    return total;
  }
  const size_t chunkSize = 448;
  const size_t lookSize = 32;
  char buffer[chunkSize + lookSize];
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

void sendDashboardPage(ESP8266WebServer& target, int code = 200, const String& message = "", const char* kind = "") {
  static PageToken tokens[48];
  TokenCollector page = {tokens, 0};
  replaceAll(page, "__MESSAGE__", messageBlock(message, kind));
  replaceAll(page, "__AP_SSID__", SETUP_AP_SSID);
  replaceAll(page, "__STATUS__", WiFi.status() == WL_CONNECTED ? "已连接" : "未连接");
  replaceAll(page, "__STATUS_KEY__", WiFi.status() == WL_CONNECTED ? "val.on" : "val.off");
  replaceAll(page, "__LAN_IP__", WiFi.localIP().toString());
  replaceAll(page, "__PUBLIC_IP__", currentDdnsIp.length() ? currentDdnsIp : "未获取");
  replaceAll(page, "__WAN_KEY__", currentDdnsIp.length() ? "" : "val.none");
  replaceAll(page, "__PROVIDER__", config.ddnsProvider);
  replaceAll(page, "__SSID__", htmlEscape(config.wifiSsid));
  replaceAll(page, "__WIFI_PASSWORD__", htmlEscape(config.wifiPassword));
  replaceAll(page, "__IP__", config.staticIp);
  replaceAll(page, "__GATEWAY__", config.gateway);
  replaceAll(page, "__SUBNET__", config.subnet);
  replaceAll(page, "__DNS__", config.dns);
  replaceAll(page, "__PORT__", String(config.webPort));
  replaceAll(page, "__HOSTNAME__", htmlEscape(config.ddnsHostname));
  replaceAll(page, "__DDNS_USERNAME__", htmlEscape(config.ddnsUsername));
  replaceAll(page, "__DDNS_PASSWORD__", htmlEscape(config.ddnsPassword));
  replaceAll(page, "__DDNS_URL__", htmlEscape(config.ddnsUrl));
  replaceAll(page, "__ALIYUN_ID__", htmlEscape(config.aliyunAccessKeyId));
  replaceAll(page, "__ALIYUN_SECRET__", htmlEscape(config.aliyunAccessKeySecret));
  replaceAll(page, "__ALIYUN_DOMAIN__", htmlEscape(config.aliyunDomainName));
  replaceAll(page, "__ALIYUN_RR__", htmlEscape(config.aliyunRR));
  replaceAll(page, "__ALIYUN_TYPE__", htmlEscape(config.aliyunType));
  replaceAll(page, "__CF_TOKEN__", htmlEscape(config.cloudflareApiToken));
  replaceAll(page, "__CF_ZONE__", htmlEscape(config.cloudflareZoneId));
  replaceAll(page, "__CF_RECORD__", htmlEscape(config.cloudflareRecordId));
  replaceAll(page, "__TX_ID__", htmlEscape(config.tencentSecretId));
  replaceAll(page, "__TX_SECRET__", htmlEscape(config.tencentSecretKey));
  replaceAll(page, "__TX_DOMAIN__", htmlEscape(config.tencentDomain));
  replaceAll(page, "__TX_SUBDOMAIN__", htmlEscape(config.tencentSubDomain));
  replaceAll(page, "__TX_TYPE__", htmlEscape(config.tencentRecordType));
  replaceAll(page, "__IP_URL__", htmlEscape(config.ddnsIpUrl));
  replaceAll(page, "__SUCCESS__", htmlEscape(config.ddnsSuccess));
  replaceAll(page, "__METHOD__", htmlEscape(config.ddnsMethod));
  replaceAll(page, "__INTERVAL__", String(config.ddnsIntervalSec));
  replaceAll(page, "__RETRY__", String(config.ddnsRetrySec));
  replaceAll(page, "__FORCE__", String(config.ddnsForceSec));
  replaceAll(page, "__BODY__", htmlEscape(config.ddnsBody));
  replaceAll(page, "__HEADERS__", htmlEscape(config.ddnsHeaders));
  replaceAll(page, "__AP_PASSWORD__", htmlEscape(config.apPassword));
  sendHtmlPage(target, code, WEB_DASHBOARD_BODY, tokens, page.count);
}

void sendSetupPage(ESP8266WebServer& target, int code = 200, const String& message = "", const char* kind = "") {
  static PageToken tokens[16];
  TokenCollector page = {tokens, 0};
  replaceAll(page, "__MESSAGE__", messageBlock(message, kind));
  replaceAll(page, "__AP_SSID__", SETUP_AP_SSID);
  replaceAll(page, "__AP_IP__", WiFi.softAPIP().toString());
  replaceAll(page, "__SSID__", htmlEscape(config.wifiSsid));
  replaceAll(page, "__WIFI_PASSWORD__", htmlEscape(config.wifiPassword));
  replaceAll(page, "__AP_PASSWORD__", htmlEscape(config.apPassword));
  replaceAll(page, "__IP__", config.staticIp);
  replaceAll(page, "__GATEWAY__", config.gateway);
  replaceAll(page, "__SUBNET__", config.subnet);
  replaceAll(page, "__DNS__", config.dns);
  replaceAll(page, "__PORT__", String(config.webPort));
  sendHtmlPage(target, code, WEB_SETUP_BODY, tokens, page.count);
}

String escapeJsonText(const String& value) {
  String escaped;
  for (size_t index = 0; index < value.length(); index++) {
    char character = value[index];
    if (character == '"' || character == '\\') {
      escaped += '\\';
    }
    if (static_cast<uint8_t>(character) >= 0x20) {
      escaped += character;
    }
  }
  return escaped;
}

void handleApRoot() {
  sendSetupPage(apServer, 200);
}

String buildScanJson() {
  String json = "[";
  String seen[24];
  size_t seenCount = 0;
  int found = WiFi.scanNetworks(false, true);
  for (int index = 0; index < found && seenCount < 24; index++) {
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
    json += "{\"s\":\"" + escapeJsonText(ssid) + "\",\"r\":" + String(WiFi.RSSI(index)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  return json;
}

void handleApScan() {
  sendJson(apServer, 200, buildScanJson());
}

void handleDashboardScan() {
  sendJson(*activeServer, 200, buildScanJson());
}

void handleApNotFound() {
  redirectToRoot(apServer);
}

void handleApNetInfo() {
  sendJson(apServer, 200, currentNetworkJson(WiFi.status() == WL_CONNECTED));
}

void handleApProbe() {
  String ssid = apServer.arg("ssid");
  String password = apServer.arg("wifiPassword");
  if (ssid.length() == 0) {
    sendJson(apServer, 200, "{\"ok\":false}");
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    portalHoldUntil = millis() + 600000;
  }
  Serial.printf("测试连接 %s: %s\n", ssid.c_str(), connected ? "成功" : "失败");
  sendJson(apServer, 200, currentNetworkJson(connected));
}

void handleDashboardNetInfo() {
  sendJson(*activeServer, 200, currentNetworkJson(WiFi.status() == WL_CONNECTED));
}

void handleApSave() {
  DeviceConfig previous = config;
  copyText(config.wifiSsid, sizeof(config.wifiSsid), apServer.arg("ssid"));
  copyText(config.wifiPassword, sizeof(config.wifiPassword), apServer.arg("wifiPassword"));
  copyText(config.apPassword, sizeof(config.apPassword), apServer.arg("apPassword"));
  copyText(config.staticIp, sizeof(config.staticIp), apServer.arg("ip"));
  copyText(config.gateway, sizeof(config.gateway), apServer.arg("gateway"));
  copyText(config.subnet, sizeof(config.subnet), apServer.arg("subnet"));
  copyText(config.dns, sizeof(config.dns), apServer.arg("dns"));
  config.webPort = static_cast<uint16_t>(constrain(apServer.arg("port").toInt(), 1L, 65535L));

  if (strlen(config.wifiSsid) == 0) {
    config = previous;
    sendSetupPage(apServer, 400, "请填写或选择 WiFi 名称。", "err");
    return;
  }
  if (strlen(config.apPassword) < 8) {
    config = previous;
    sendSetupPage(apServer, 400, "配置 AP 密码至少需要 8 位。", "err");
    return;
  }

  Serial.printf("尝试连接 WiFi: %s\n", config.wifiSsid);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    config = previous;
    WiFi.disconnect();
    sendSetupPage(apServer, 400, "连接 WiFi 失败，请确认名称和密码后重试。", "err");
    return;
  }

  if (apServer.arg("autoNet") == "1") {
    applyDhcpAddresses();
    Serial.println("已按路由器分配的地址信息自动填写");
  } else {
    applyAddressFallback();
  }

  saveConfig();
  sendSetupPage(apServer, 200, "配置成功，设备正在重启并应用新的网络设置。", "ok");
  delay(900);
  ESP.restart();
}

void startSetupPortal() {
  if (setupPortalActive) {
    return;
  }
  if (serverStarted) {
    activeServer->stop();
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
    apServer.on("/probe", HTTP_POST, handleApProbe);
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
  sendDashboardPage(*activeServer, 200);
}

bool applyFormConfig(ESP8266WebServer& source) {
  copyText(config.wifiSsid, sizeof(config.wifiSsid), source.arg("ssid"));
  copyText(config.wifiPassword, sizeof(config.wifiPassword), source.arg("wifiPassword"));
  copyText(config.staticIp, sizeof(config.staticIp), source.arg("ip"));
  copyText(config.gateway, sizeof(config.gateway), source.arg("gateway"));
  copyText(config.subnet, sizeof(config.subnet), source.arg("subnet"));
  copyText(config.dns, sizeof(config.dns), source.arg("dns"));
  copyText(config.ddnsHostname, sizeof(config.ddnsHostname), source.arg("hostname"));
  copyText(config.ddnsUsername, sizeof(config.ddnsUsername), source.arg("ddnsUsername"));
  copyText(config.ddnsPassword, sizeof(config.ddnsPassword), source.arg("ddnsPassword"));
  copyText(config.ddnsUrl, sizeof(config.ddnsUrl), source.arg("ddnsUrl"));
  copyText(config.ddnsProvider, sizeof(config.ddnsProvider), source.arg("ddnsProvider"));
  copyText(config.aliyunAccessKeyId, sizeof(config.aliyunAccessKeyId), source.arg("aliyunAccessKeyId"));
  copyText(config.aliyunAccessKeySecret, sizeof(config.aliyunAccessKeySecret), source.arg("aliyunAccessKeySecret"));
  copyText(config.aliyunDomainName, sizeof(config.aliyunDomainName), source.arg("aliyunDomainName"));
  copyText(config.aliyunRR, sizeof(config.aliyunRR), source.arg("aliyunRR"));
  copyText(config.aliyunType, sizeof(config.aliyunType), source.arg("aliyunType"));
  copyText(config.cloudflareApiToken, sizeof(config.cloudflareApiToken), source.arg("cloudflareApiToken"));
  copyText(config.cloudflareZoneId, sizeof(config.cloudflareZoneId), source.arg("cloudflareZoneId"));
  copyText(config.cloudflareRecordId, sizeof(config.cloudflareRecordId), source.arg("cloudflareRecordId"));
  copyText(config.tencentSecretId, sizeof(config.tencentSecretId), source.arg("tencentSecretId"));
  copyText(config.tencentSecretKey, sizeof(config.tencentSecretKey), source.arg("tencentSecretKey"));
  copyText(config.tencentDomain, sizeof(config.tencentDomain), source.arg("tencentDomain"));
  copyText(config.tencentSubDomain, sizeof(config.tencentSubDomain), source.arg("tencentSubDomain"));
  copyText(config.tencentRecordType, sizeof(config.tencentRecordType), source.arg("tencentRecordType"));
  copyText(config.ddnsIpUrl, sizeof(config.ddnsIpUrl), source.arg("ddnsIpUrl"));
  copyText(config.ddnsSuccess, sizeof(config.ddnsSuccess), source.arg("ddnsSuccess"));
  copyText(config.ddnsMethod, sizeof(config.ddnsMethod), source.arg("ddnsMethod"));
  copyText(config.ddnsBody, sizeof(config.ddnsBody), source.arg("ddnsBody"));
  copyText(config.ddnsHeaders, sizeof(config.ddnsHeaders), source.arg("ddnsHeaders"));
  config.ddnsIntervalSec = constrain(source.arg("ddnsIntervalSec").toInt(), 10L, 604800L);
  config.ddnsRetrySec = constrain(source.arg("ddnsRetrySec").toInt(), 10L, 3600L);
  config.ddnsForceSec = constrain(source.arg("ddnsForceSec").toInt(), 600L, 2592000L);
  config.webPort = static_cast<uint16_t>(constrain(source.arg("port").toInt(), 1L, 65535L));
  String apPassword = source.arg("apPassword");
  if (apPassword.length() == 0 && strlen(config.apPassword) >= 8) {
    return true;
  }
  if (apPassword.length() < 8) {
    return false;
  }
  copyText(config.apPassword, sizeof(config.apPassword), apPassword);
  return true;
}

void handleSave() {
  DeviceConfig previous = config;
  if (!applyFormConfig(*activeServer)) {
    config = previous;
    sendDashboardPage(*activeServer, 400, "配置 AP 密码至少需要 8 位。", "err");
    return;
  }
  applyAddressFallback();
  saveConfig();
  sendDashboardPage(*activeServer, 200, "配置已保存，设备正在重启。", "ok");
  delay(900);
  ESP.restart();
}

void startWebServer() {
  if (serverStarted) {
    return;
  }
  if (config.webPort == 80) {
    activeServer = &server;
  } else {
    activeServer = new ESP8266WebServer(config.webPort);
  }
  activeServer->on("/", HTTP_GET, handleRoot);
  activeServer->on("/netinfo", HTTP_GET, handleDashboardNetInfo);
  activeServer->on("/scan", HTTP_GET, handleDashboardScan);
  activeServer->on("/save", HTTP_POST, handleSave);
  activeServer->begin();
  serverStarted = true;
  Serial.printf("Web 服务: http://%s:%u\n", WiFi.localIP().toString().c_str(), config.webPort);
}

String replacePlaceholders(String url) {
  url.replace("{hostname}", config.ddnsHostname);
  url.replace("{ip}", currentDdnsIp);
  url.replace("{user}", config.ddnsUsername);
  url.replace("{password}", config.ddnsPassword);
  url.replace("#{domain}", config.ddnsHostname);
  url.replace("#{ip}", currentDdnsIp);
  url.replace("#{recordType}", "A");
  url.replace("#{ttl}", "600");
  return url;
}

String httpGetBody(const String& url, int& status) {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  if (!http.begin(*client, url)) {
    status = -1;
    return "";
  }
  status = http.GET();
  String body = http.getString();
  http.end();
  return body;
}

String fetchPublicIp() {
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
  if (now < 1000000000) {
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
  while (now < 1000000000 && millis() - startedAt < timeoutMs) {
    delay(250);
    now = time(nullptr);
  }
  return now >= 1000000000;
}

String aliyunRequest(const String& canonicalQuery, int& status) {
  String stringToSign = "GET&%2F&" + percentEncode(canonicalQuery);
  String signature = percentEncode(hmacSha1Base64(String(config.aliyunAccessKeySecret) + "&", stringToSign));
  String url = "https://alidns.aliyuncs.com/?" + canonicalQuery + "&Signature=" + signature;
  return httpGetBody(url, status);
}

bool updateAliyunDdns() {
  if (strlen(config.aliyunAccessKeyId) == 0 || strlen(config.aliyunAccessKeySecret) == 0 ||
      strlen(config.aliyunDomainName) == 0 || strlen(config.aliyunRR) == 0) {
    Serial.println("阿里云 DDNS 配置不完整");
    return false;
  }
  if (strcmp(config.aliyunType, "A") != 0) {
    Serial.printf("阿里云记录类型 %s 暂不支持自动更新，已跳过\n", config.aliyunType);
    return true;
  }
  if (!waitForNetworkTime(5000)) {
    Serial.println("尚未获取网络时间，跳过阿里云 DDNS");
    return false;
  }
  String timestamp = utcTimestamp();
  String nonce = String(ESP.getChipId(), HEX) + String(millis());
  String query = "AccessKeyId=" + percentEncode(config.aliyunAccessKeyId);
  query += "&Action=DescribeDomainRecords";
  query += "&DomainName=" + percentEncode(config.aliyunDomainName);
  query += "&Format=JSON";
  query += "&RRKeyWord=" + percentEncode(config.aliyunRR);
  query += "&SignatureMethod=HMAC-SHA1&SignatureNonce=" + nonce;
  query += "&SignatureVersion=1.0&Timestamp=" + percentEncode(timestamp);
  query += "&Type=" + percentEncode(config.aliyunType);
  query += "&Version=2015-01-09";
  int status = 0;
  String response = aliyunRequest(query, status);
  if (status != 200) {
    Serial.printf("阿里云查询失败: %d\n", status);
    return false;
  }
  DynamicJsonDocument document(4096);
  if (deserializeJson(document, response)) {
    Serial.println("阿里云响应解析失败");
    return false;
  }
  String recordId;
  for (JsonObject record : document["DomainRecords"]["Record"].as<JsonArray>()) {
    if (String(record["RR"].as<const char*>()) == config.aliyunRR &&
        String(record["Type"].as<const char*>()) == config.aliyunType) {
      recordId = record["RecordId"].as<String>();
      break;
    }
  }
  if (recordId.length() == 0) {
    Serial.println("阿里云未找到匹配 DNS 记录");
    return false;
  }
  timestamp = utcTimestamp();
  nonce = String(ESP.getChipId(), HEX) + String(millis());
  query = "AccessKeyId=" + percentEncode(config.aliyunAccessKeyId);
  query += "&Action=UpdateDomainRecord";
  query += "&Format=JSON";
  query += "&RR=" + percentEncode(config.aliyunRR);
  query += "&RecordId=" + percentEncode(recordId);
  query += "&SignatureMethod=HMAC-SHA1&SignatureNonce=" + nonce;
  query += "&SignatureVersion=1.0&Timestamp=" + percentEncode(timestamp);
  query += "&Type=" + percentEncode(config.aliyunType);
  query += "&Value=" + percentEncode(currentDdnsIp);
  query += "&Version=2015-01-09";
  response = aliyunRequest(query, status);
  Serial.printf("阿里云 DDNS 更新状态: %d\n", status);
  return status == 200 && response.indexOf("ErrorCode") < 0;
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
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  if (!http.begin(*client, url)) {
    Serial.println("Cloudflare API 无法连接");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + String(config.cloudflareApiToken));
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<512> payload;
  payload["type"] = "A";
  payload["name"] = config.ddnsHostname;
  payload["content"] = currentDdnsIp;
  payload["ttl"] = 120;
  payload["proxied"] = false;
  String body;
  serializeJson(payload, body);
  int status = http.PUT(body);
  String response = http.getString();
  http.end();
  Serial.printf("Cloudflare DDNS 更新状态: %d\n", status);
  return status == 200 && response.indexOf("\"success\":true") >= 0;
}

String tencentAuthorization(const String& payload, uint32_t timestamp) {
  const String host = "dnspod.tencentcloudapi.com";
  const String service = "dnspod";
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
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  if (!http.begin(*client, "https://dnspod.tencentcloudapi.com")) {
    status = -1;
    return "";
  }
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Host", "dnspod.tencentcloudapi.com");
  http.addHeader("X-TC-Action", action);
  http.addHeader("X-TC-Version", "2021-03-23");
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
  if (strcmp(config.tencentRecordType, "A") != 0) {
    Serial.printf("腾讯云记录类型 %s 暂不支持自动更新，已跳过\n", config.tencentRecordType);
    return true;
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
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;
  if (!http.begin(*client, url)) {
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
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
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
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    startWebServer();
    updateDdns();
  } else {
    startSetupPortal();
  }
}

void loop() {
  if (setupPortalActive) {
    apServer.handleClient();
    bool hold = portalHoldUntil != 0 && static_cast<int32_t>(millis() - portalHoldUntil) < 0;
    bool backToConfiguredNetwork = strlen(config.wifiSsid) > 0 &&
                                   WiFi.status() == WL_CONNECTED &&
                                   WiFi.SSID() == String(config.wifiSsid);
    if (!hold && backToConfiguredNetwork) {
      stopSetupPortal();
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      startWebServer();
      updateDdns();
    }
    activeServer->handleClient();
    if (static_cast<int32_t>(millis() - nextDdnsAttempt) >= 0) {
      updateDdns();
    }
  } else {
    if (serverStarted) {
      activeServer->stop();
      serverStarted = false;
    }
    if (millis() - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetry = millis();
      Serial.println("WiFi 已断开，尝试重连");
      WiFi.reconnect();
    }
    if (millis() - lastWifiRetry >= 5000) {
      startSetupPortal();
    }
  }
  delay(2);
}
