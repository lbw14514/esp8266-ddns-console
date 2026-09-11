# ESP8266 固件

功能：

- 首次启动通过 `ESP8266-Setup` AP 配置 WiFi、静态 IP 和 DDNS
- 配网页与运行页使用同一套自研深色界面，可扫描附近 WiFi
- 连接成功后在配置的 IP 和端口提供 Web 服务
- WiFi 断开时后台自动重连，同时重新开放配置 AP
- 静态 IP 冲突时先使用 DHCP 探测，并在同一子网内按最后一段地址顺延，保存实际可用地址
- 支持通用 DDNS 更新 URL，可使用 `{hostname}`、`{ip}`、`{user}`、`{password}` 占位符
- 原生支持阿里云 DNS、Cloudflare DNS 和腾讯云 DNSPod API
- 配置保存到 ESP8266 EEPROM，重启后保留
- 不使用第三方配网库，配网页无外部链接与第三方品牌

## 使用

1. 使用 VS Code + PlatformIO 打开本目录并上传固件。
2. 首次启动连接 WiFi：`ESP8266-Setup`，首次无需密码。串口日志也会输出设备实际创建的网络名称。
3. 连接成功后不要等待自动跳转，手动打开 `http://192.168.4.1`。页面会引导填写家庭 WiFi 和至少 8 位的配置 AP 新密码。
4. 保存后设备重启，在配置的静态 IP 和端口访问服务。
5. DDNS 厂商可填写 `aliyun`、`cloudflare`、`dnspod` 或 `generic`。

## Web 服务教程

### 首次配网

1. 给 ESP8266 上电，连接串口监视器，波特率设置为 `115200`。
2. 手机或电脑连接配置网络 `ESP8266-Setup`，首次无需密码；之后使用你设置的 AP 密码。
3. 连接后手动打开 `http://192.168.4.1`，页面为深色配置向导。
4. 点击“扫描附近 WiFi”选择网络，填写 WiFi 密码和至少 8 位配置 AP 密码。
5. 点击“测试连接并获取信息”，设备会连接该 WiFi 并把路由器分配的 IP、网关、子网掩码和 DNS 自动填入表单。
6. 保持“自动获取并填写网络信息”勾选，点击保存，设备写入 EEPROM 并自动重启。

### 自动获取网络信息

- 配网页：点击“测试连接并获取信息”，连接成功后自动填入地址；勾选“自动获取并填写网络信息”时保存会直接采用路由器分配的值。
- 仪表盘：点击“自动获取网络信息”，从 `/netinfo` 读取当前地址并填入表单。
- 保存兜底：地址字段为空或格式非法时，设备会自动使用当前网络信息补齐，避免写入无效地址。
- 取消勾选后使用手动填写的地址，空字段仍会自动补齐。

如果 WiFi 连接失败，页面会提示失败原因并保留当前配置，无需重新上电。

### 正常访问

设备重启后，在同一局域网内访问：

```text
http://设备实际IP:端口/
```

例如：

```text
http://192.168.1.50:80/
```

端口为 `80` 时可以省略端口号。配置页顶部会显示设备实际地址；如果静态 IP 冲突，设备会自动顺延到同一网段中的可用地址，必须使用页面或串口输出的新地址。

### WiFi 断开后的处理

WiFi 断开后，设备会先尝试重连；重连失败时会停止 Web 服务并重新开启配置 AP。重新连接 `ESP8266-Setup` 后手动访问 `http://192.168.4.1`，修改 WiFi 或网络参数并保存。

### 安全注意事项

当前 Web 服务使用 HTTP，配置页没有登录认证，只建议在可信局域网内访问。不要把 ESP8266 的 Web 端口直接映射到公网；如需公网访问，应在路由器或反向代理上增加 HTTPS、认证和访问控制。当前页面用于设备配置，不包含 Lucky 的反向代理、端口转发、Web 重定向或黑白名单功能。

Cloudflare 需要填写 API Token、Zone ID、Record ID，并将完整记录名填写到域名字段。阿里云需要填写 AccessKey、根域名和 RR。腾讯云 DNSPod 需要填写 SecretId、SecretKey、主域名和子域名。

通用 DDNS 更新 URL 示例：

```text
https://dynupdate.no-ip.com/nic/update?hostname={hostname}&myip={ip}
```

## 通用模式教程

通用模式适用于没有原生适配的 DDNS 服务。设备每 10 分钟发起一次 HTTPS GET 请求，并替换以下占位符：

| 占位符 | 替换内容 |
| --- | --- |
| `{hostname}` | 配置页面中的域名 |
| `{ip}` | ESP8266 当前 WiFi 地址 |
| `{user}` | 配置页面中的用户名 |
| `{password}` | 配置页面中的密码 |

同时兼容 Lucky Callback 占位符：`#{ip}`、`#{domain}`、`#{recordType}`、`#{ttl}`。其中 `#{recordType}` 当前为 `A`，`#{ttl}` 当前为 `600`。

### 配置步骤

1. DDNS 厂商填写 `generic`。
2. 域名填写完整域名，例如 `home.example.com`。
3. 用户名和密码填写服务商要求的账号、Token 或更新密码。
4. 更新 URL 填写服务商接口，并把固定值替换成占位符。
5. 保存重启，串口出现 `DDNS 更新状态: 200` 表示 HTTP 请求成功。

用户名非空时，固件还会发送 HTTP Basic Auth。服务商要求 Basic Auth 时填写用户名和密码；服务商要求 Token 放在 URL 中时，使用 `{password}`。

### POST、请求体和 Header

Callback 默认方法是 `GET`，也可以填写 `POST`。请求体和 Header 支持同样的占位符：

```text
Callback 方法: POST
Callback 请求体: {"domain":"#{domain}","ip":"#{ip}"}
Callback Headers: Content-Type: application/json
Authorization: Bearer #{password}
```

Header 每行一个，格式为 `名称:值`。方法、请求体和 Header 只对 `generic` 模式生效。

### No-IP

```text
https://dynupdate.no-ip.com/nic/update?hostname={hostname}&myip={ip}
```

用户名填写 No-IP 账号，密码填写 No-IP 密码或更新 Token。

### DuckDNS

DuckDNS 通常把 Token 放在 URL 中：

```text
https://www.duckdns.org/update?domains={hostname}&token={password}&ip={ip}
```

### Dynv6

```text
https://dynv6.com/api/update?hostname={hostname}&token={password}&ipv4={ip}
```

### 自建接口

接口格式为 `/update?domain=域名&address=IP` 时：

```text
https://ddns.example.com/update?domain={hostname}&address={ip}
```

服务端返回 HTTP `200` 即可。也可以在“成功关键字”中填写 `good`、`nochg`、`ok` 等服务商响应内容；填写后必须同时满足 HTTP 状态码成功和正文包含该关键字。

### 测试方法

先把占位符替换成真实值，在浏览器或命令行测试完整 URL：

```text
https://www.duckdns.org/update?domains=example.duckdns.org&token=你的Token&ip=1.2.3.4
```

确认服务商返回成功后，再改回 `{hostname}`、`{password}` 和 `{ip}` 写入配置页面。Token 如果包含 `+`、`&`、`=` 等特殊字符，应按服务商要求进行 URL 编码。

### 公网 IP 注意事项

当前固件默认按顺序使用多个公网 IP API 获取公网 IPv4：

1. `https://api.ipify.org`
2. `https://checkip.amazonaws.com`
3. `https://ipv4.icanhazip.com`
4. `https://ifconfig.me/ip`

配置页填写“公网 IP 查询 URL”后，该地址会优先使用；如果自定义地址失败，仍会自动切换到上述默认 API。所有接口失败时，本次 DDNS 更新会跳过并按失败重试周期再次尝试。`{ip}` 是路由器公网出口地址。

如果固定 IP 已被其他设备占用，设备会依次测试同一子网中的后续地址，并在串口输出和配置页显示最终地址。旧 IP 已被其他设备占用时，设备无法从旧 IP 接收 HTTP 请求，因此不能由旧地址执行重定向；当前配网已关闭自动 captive redirect，需要手动打开 `http://192.168.4.1`。

带用户名密码的服务可以填写 URL 中的占位符，设备会使用 HTTP Basic Auth；HTTPS 更新需要证书校验策略，当前代码使用 BearSSL 的安全客户端并允许服务端证书验证失败，以兼容自定义 DDNS 服务。

## 默认值

- 静态 IP：`192.168.1.50`
- 网关：`192.168.1.1`
- 子网掩码：`255.255.255.0`
- DNS：`8.8.8.8`
- Web 端口：`80`
- DDNS 更新间隔：10 分钟
- DDNS 失败重试间隔：60 秒
- DDNS 强制更新间隔：24 小时

## 重启恢复与兜底机制

### 重启后会保留的内容

以下配置会通过 EEPROM 持久化，正常重启、断电再上电后都会恢复：

- WiFi 名称和密码
- 静态 IP、网关、子网掩码、DNS 和 Web 端口
- DDNS 厂商、域名、Token、AccessKey、SecretKey
- 公网 IP 查询 URL、Callback 方法、请求体、Header 和成功关键字

保存配置时会执行 EEPROM 提交。只有配置无效、存储内容损坏，或固件升级了配置版本时，才会回到默认配置并重新进入配网。

### 重启后会重新执行的流程

运行时状态不会写入 EEPROM：

- 重新获取 DHCP 地址
- 重新测试静态 IP 是否冲突
- 冲突时在同一网段顺延地址
- 重新连接 WiFi
- 重新获取公网 IP
- DDNS 首次强制更新

这样做可以避免把旧的运行状态当成真实网络状态。设备重启后即使公网 IP 没有变化，也会至少执行一次 DDNS 更新。

### DDNS 自动同步策略

- 获取公网 IP 失败：60 秒后重试。
- DNS API 或 Callback 调用失败：60 秒后重试，不会等完整的 10 分钟周期。
- 更新成功：默认每 10 分钟检查一次。
- 公网 IP 未变化：跳过实际写入，降低服务商 API 请求次数。
- 连续长时间未变化：每 24 小时强制执行一次更新。
- 设备重启或重新联网：清空运行时缓存并立即重新同步。
- Callback 配置了成功关键字时，必须同时满足 HTTP 成功状态和响应正文包含关键字。

以上三个周期都可以在 Web 配置页修改，单位为秒：

- `DDNS 检查间隔秒`：成功后的下一次检查，允许 `10` 到 `604800` 秒。
- `DDNS 失败重试秒`：获取公网 IP 或更新接口失败后的重试间隔，允许 `10` 到 `3600` 秒。
- `DDNS 强制更新秒`：即使公网 IP 没变也重新调用一次接口，允许 `600` 到 `2592000` 秒。

参数会保存到 EEPROM，重启后继续使用；超出范围的值会自动恢复默认值。

DNS 解析存在 TTL 和缓存延迟，固件不会在刚写入后立即用本地 DNS 结果判定失败，避免把正常的 DNS 生效延迟误判成 DDNS 错误；失败请求会按上述重试策略继续执行。

### 连接失败时的兜底顺序

1. 读取 EEPROM 中的最近配置。
2. 通过 DHCP 连接 WiFi，用于探测局域网和 IP 冲突。
3. 测试配置静态 IP；若占用，则向同一子网后续地址顺延。
4. 静态 IP 配置失败或 WiFi 连接失败时，开启 `ESP8266-Setup` 配网 AP。
5. WiFi 运行中断线时，先尝试自动重连；失败后再次开启配网 AP。

旧 IP 被其他设备占用时，ESP8266 无法从旧 IP 接收请求，因此不能依赖旧地址进行 HTTP 重定向。此时请连接配网 AP，并手动打开 `http://192.168.4.1`。
