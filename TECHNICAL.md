# 运行原理与机制

本文说明固件的运行流程、DDNS 调度策略、持久化与兜底机制、各厂商接口实现细节，以及通用模式的完整教程。安装与日常使用请看 [README.md](README.md)。

## 启动与配网流程

1. 上电后读取 EEPROM 中的配置（WiFi 名称密码、地址、DDNS 参数、Web 端口）。配置有效则直接进入联网流程；配置无效或版本不符则使用默认值并进入配网。
2. 先用 DHCP 连接配置中的 WiFi，用路由器分配的地址探测局域网是否可用。
3. 按配置测试静态 IP：如果该地址已被占用，则在同一子网内从最后一段依次向后顺延，直到找到可应答的地址。
4. 静态 IP 可用则启动 Web 服务；静态 IP 配置失败或 WiFi 连接失败时，开启 `ESP8266-Setup` 配网 AP，同时后台继续尝试重连 WiFi。
5. 运行中 WiFi 掉线：先自动重连，重连失败后停止 Web 服务并重新开放配网 AP。重新连接 `ESP8266-Setup` 后手动访问 `http://192.168.4.1`，修改参数并保存即可。

配网模式下设备处于 AP+STA 双模，STA 侧连上目标 WiFi 时会切换信道，连在 AP 上的客户端可能瞬时断开，重新连接 AP 后继续操作即可。

## 静态 IP 冲突与顺延

- 冲突探测基于 ping 应答：只有在线且响应 ICMP 的主机才会被判定为占用。
- 顺延只在同一子网内进行，按最后一段地址递增，网关、子网掩码、DNS 保持不变。
- 顺延后的实际地址会写入配置、显示在配置页，并输出到串口。
- 旧 IP 被别的设备占用时，ESP8266 无法再从旧 IP 收到 HTTP 请求，因此不可能由旧地址做 HTTP 重定向；此时只能连接配网 AP 并手动打开 `http://192.168.4.1`（当前固件不做自动 captive 跳转，避免第三方跳转页）。

## 页面与接口

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/` | GET | 运行页（仪表盘）或配网页，按当前模式返回 |
| `/netinfo` | GET | 返回当前 SSID、IP、网关、掩码、DNS、RSSI 的 JSON |
| `/scan` | GET | 扫描附近 WiFi，返回 `[{"s":"名称","r":信号}]` |
| `/probe` | POST | 配网页使用：提交 WiFi 账号，立即返回 `pending`，连接在后台进行 |
| `/probe` | GET | 查询测试连接结果，`state` 为 `pending` / `ok` / `fail` |
| `/connectstate` | GET | 查询保存任务的连接状态，成功后设备保存配置并重启 |
| `/save` | POST | 仪表盘：校验并保存配置后重启 |
| `/save` | POST | 配网页：校验参数后启动后台连接任务 |

页面为单文件自研样式，不引用任何外部资源，并带 `Cache-Control: no-store` 防止浏览器缓存旧页面。

### 非阻塞连接

配网的“测试连接”和“保存”不再在 HTTP 处理函数里阻塞：提交后立即返回，连接在 `loop()` 里的状态机中推进，页面每 1.5 秒轮询一次状态。好处是连接过程中配置页仍可响应、可重复扫描，不会出现 15 秒无响应。

状态机字段：`job`（probe/save）、`active`、`resultReady`、`succeeded`、`dhcpAddresses`、`restartAt`。保存成功后会保持 AP 3 秒再重启，让页面能显示“连接成功”。

### 页面为什么按分块发送

整页（样式、脚本、正文）约 24KB。如果先在内存里拼成一个大 `String` 再发送，需要一次性申请约 25KB 连续堆内存；ESP8266 在联网状态下没有这么多连续空间，拼接会静默失败，结果是只发出页面外壳、正文为空（浏览器表现为深色空白页）。因此固件改为：

1. 先扫描一次正文，按占位符实际替换后的长度算出精确的 `Content-Length`；
2. 再按 448 字节为一块从 flash 读取，块内替换占位符后逐块发送；
3. 跨块边界的占位符会整块留到下一块处理，避免被截断。

这样发送期间的内存峰值从约 25KB 降到约 1KB。

## DDNS 实现

### 公网 IP 获取

按顺序尝试，前一个成功即停止：

1. 配置页填写的“公网 IP 查询 URL”
2. `https://api.ipify.org`
3. `https://checkip.amazonaws.com`
4. `https://ipv4.icanhazip.com`
5. `https://ifconfig.me/ip`

全部失败则本次更新跳过，按失败重试周期再试。`{ip}` 是路由器公网出口地址，不是设备局域网地址。

### 调度与重试策略

- 获取公网 IP 失败：按“失败重试秒”重试。
- DNS API 或 Callback 调用失败：按“失败重试秒”重试，不会等完整的检查周期。
- 更新成功：按“检查间隔秒”进行下一次检查。
- 公网 IP 未变化：跳过实际写入，减少服务商 API 调用。
- 公网 IP 长时间未变化：按“强制更新秒”强制执行一次。
- 设备重启或重新联网：清空运行时缓存并立即重新同步，即使公网 IP 没变也至少更新一次。
- 配置了“成功关键字”时：必须同时满足 HTTP 成功状态码和响应正文包含该关键字，才判定为成功。

三个周期都可以在 Web 页面修改，单位为秒，取值超出范围会回落到默认值：

| 参数 | 默认 | 允许范围 |
| --- | --- | --- |
| DDNS 检查间隔秒 | 600 | 10 - 604800 |
| DDNS 失败重试秒 | 60 | 10 - 3600 |
| DDNS 强制更新秒 | 86400 | 600 - 2592000 |

DNS 解析存在 TTL 和缓存延迟，固件不会在写入后立刻用本地 DNS 结果判定失败，避免把正常的生效延迟误判成错误。

### 厂商接口

- 阿里云 DNS：`DescribeDomainRecords` 查询记录 → `UpdateDomainRecord` 写入，使用 AccessKey 与 HMAC-SHA1 签名。
- 腾讯云 DNSPod：`DescribeRecordList` 查询 → `ModifyRecord` 写入，使用 SecretId/SecretKey 与 TC3-HMAC-SHA256 签名。
- Cloudflare：通过 API Token 调用 `PATCH /zones/{zone}/dns_records/{record}` 写入，记录类型固定 A。
- 通用 Callback：自定义更新 URL、方法、请求体、Header 与成功关键字。

只有 A 记录会被自动更新，选择 AAAA 或 CNAME 时固件会集中判定并跳过写入（不算失败，不触发重试风暴）。

### HTTPS 证书校验

默认开启证书校验，信任根放在 `src/tls_roots.h`：`ISRG Root X1`、`GTS Root R4`（Google Trust Services，覆盖 api.ipify.org / ipv4.icanhazip.com / api.cloudflare.com）、`DigiCert Global Root G2`（腾讯云 DNSPod）、`Amazon Root CA 1`（checkip.amazonaws.com）、`GlobalSign Root CA - R3`（阿里云 alidns）。

- 校验需要准确的系统时间，因此发起 HTTPS 前会等待 NTP 校时；时间未就绪时本轮 DDNS 跳过
- 时间随机数由硬件 RNG、`micros()` 与 CPU 周期计数器组合生成，不再可预测
- 自建或自签证书的服务，可在 DDNS 页取消勾选“校验 HTTPS 证书”切换为不校验模式

### 输入与输出转义

- HTML 上下文（表单 value、文本节点）统一走 `htmlEscape`，转义 `& < > " '`
- JSON 上下文（扫描结果、WiFi 名称）统一走 `escapeJson`，转义反斜杠与双引号并丢弃控制字符
- 服务商、记录类型、Callback 方法做白名单归一化
- 页面占位符写入带容量上限检查，超出会在串口输出日志而不会越界

### 配置持久化细节

- 结构体末尾存放 CRC32，加载时校验 `magic` + `crc` + 取值范围，任一不满足即恢复默认值
- `EEPROM.commit()` 返回值会被检查，失败时输出日志
- 字段超长会被截断，并在串口输出截断前后的长度
- 密码与密钥不回显到页面：提交值为空则保持原值，提交 `-` 则清空

### 实现要点（代码内不写注释）

- 分块发送：先扫一遍正文算出替换后的准确 `Content-Length`，再按 448 字节从 flash 读取、替换占位符后逐块发送；跨块边界的占位符整块延后，避免被截断
- 阿里云签名：公共参数 + 业务参数拼成规范化查询串，`GET&%2F&` + 百分号编码后 HMAC-SHA1，再对签名做百分号编码
- 腾讯云签名：`sha256(canonicalRequest)` → `TC3-HMAC-SHA256` 待签串 → `TC3+SecretKey` 逐层派生签名密钥
- ArduinoJson 固定在 6.x：v7 改了 `StaticJsonDocument`/`DynamicJsonDocument` 接口，升级需要同步改代码

## 配置持久化与重启恢复

### 重启后保留

- WiFi 名称和密码
- 静态 IP、网关、子网掩码、DNS、Web 端口
- DDNS 厂商、域名、Token、AccessKey、SecretKey
- 公网 IP 查询 URL、Callback 方法、请求体、Header、成功关键字
- 三个同步周期参数、配置 AP 密码、HTTPS 证书校验开关

保存时会提交 EEPROM。只有配置校验失败、存储内容损坏或配置版本变化时，才会回到默认值并重新进入配网。

### 重启后重新执行

运行时状态不写入 EEPROM，每次重启都会重新执行：

- 重新通过 DHCP 获取地址
- 重新探测静态 IP 是否冲突，必要时顺延
- 重新连接 WiFi
- 重新获取公网 IP
- 至少执行一次 DDNS 更新

这样做避免把旧的运行状态当成真实网络状态。

## 连接失败时的兜底顺序

1. 读取 EEPROM 中的最近配置。
2. 用 DHCP 连接 WiFi，探测局域网与地址占用情况。
3. 测试配置的静态 IP；被占用时在同一子网内顺延。
4. 静态 IP 或 WiFi 连接失败时，开启 `ESP8266-Setup` 配网 AP。
5. 运行中断线时先自动重连，失败后再次开启配网 AP。

## 通用模式教程

通用模式适用于没有原生适配的 DDNS 服务。设备按检查间隔发起 HTTPS 请求，并替换以下占位符：

| 占位符 | 替换内容 |
| --- | --- |
| `{hostname}` | 配置页面中的域名 |
| `{ip}` | 设备当前获取到的公网 IP |
| `{user}` | 配置页面中的用户名 |
| `{password}` | 配置页面中的密码 |

同时兼容 Lucky 风格的 Callback 占位符：`#{ip}`、`#{domain}`、`#{recordType}`、`#{ttl}`，其中 `#{recordType}` 当前为 `A`，`#{ttl}` 当前为 `600`。

### 配置步骤

1. 服务商选择 `generic`。
2. 域名填写完整域名，例如 `home.example.com`。
3. 用户名和密码填写服务商要求的账号、Token 或更新密码。
4. 更新 URL 填写服务商接口，把固定值替换成占位符。
5. 保存重启，串口出现 `DDNS 更新状态: 200` 表示请求成功。

用户名非空时会发送 HTTP Basic Auth。服务商要求 Token 放在 URL 中时，把 Token 写到 `{password}` 的位置。

### POST、请求体和 Header

Callback 默认方法是 `GET`，也可以选择 `POST`。请求体和 Header 支持同样的占位符：

```text
Callback 方法: POST
Callback 请求体: {"domain":"#{domain}","ip":"#{ip}"}
Callback Headers: Content-Type: application/json
Authorization: Bearer #{password}
```

Header 每行一个，格式为 `名称:值`。方法、请求体和 Header 只对 `generic` 模式生效。

### 各服务商示例

No-IP：

```text
https://dynupdate.no-ip.com/nic/update?hostname={hostname}&myip={ip}
```

用户名为 No-IP 账号，密码为 No-IP 密码或更新 Token。

DuckDNS：

```text
https://www.duckdns.org/update?domains={hostname}&token={password}&ip={ip}
```

Dynv6：

```text
https://dynv6.com/api/update?hostname={hostname}&token={password}&ipv4={ip}
```

自建接口（形如 `/update?domain=域名&address=IP`）：

```text
https://ddns.example.com/update?domain={hostname}&address={ip}
```

服务端返回 HTTP `200` 即可；也可以在“成功关键字”中填写 `good`、`nochg`、`ok` 等响应内容。

### 测试方法

先把占位符替换成真实值，用浏览器或命令行测试完整 URL：

```text
https://www.duckdns.org/update?domains=example.duckdns.org&token=你的Token&ip=1.2.3.4
```

确认服务商返回成功后，再改回 `{hostname}`、`{password}`、`{ip}` 填入配置页面。Token 中如果包含 `+`、`&`、`=` 等字符，需要按服务商要求进行 URL 编码。

## 安全说明

- 当前 Web 服务使用 HTTP，配置页没有登录认证，只建议在可信局域网内访问。
- 不要把 ESP8266 的 Web 端口直接映射到公网；需要公网访问时，应在路由器或反向代理上增加 HTTPS、认证和访问控制。
- 页面只用于设备配置与状态查看，不包含反向代理、端口转发、Web 重定向或黑白名单功能。
