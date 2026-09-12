# ESP8266 DDNS 控制台固件

把 ESP8266 做成一台带自研配网页面的 DDNS 小主机：手机连上它的热点即可配好 WiFi，之后设备固定在同一网段的可用地址上提供管理页，并持续把公网 IP 同步到域名；WiFi 掉线自动重连，必要时重新开放配网热点。

## 解决了什么问题

- 无屏幕设备配网麻烦：自带 `ESP8266-Setup` 热点与深色配置页，手机即可完成 WiFi、地址、DDNS 配置
- 静态 IP 冲突后设备失联：在同一子网内自动顺延到可用地址，页面与串口都会给出实际地址
- 动态公网 IP 需要 DDNS：原生支持阿里云 DNS、Cloudflare、腾讯云 DNSPod，其它服务商可用通用 Callback
- WiFi 掉线后要重新烧写：自动重连，失败后重新开放配置 AP
- 断电后配置丢失：WiFi、地址、DDNS 参数全部写入 EEPROM
- 第三方配网库会显示自己的品牌页与外链：本固件页面完全自研，无外链、无第三方品牌

## 怎么使用

### 烧写

用 VS Code + PlatformIO 打开本目录，接好串口后直接 Upload；首次烧写建议先执行 Erase Flash。命令行等价操作：

```bash
pio run -t erase
pio run -t upload
```

### 首次配网

1. 给设备上电，手机或电脑连接热点 `ESP8266-Setup`（首次无密码）
2. 手动打开 `http://192.168.4.1`，页面不会自动弹出
3. 扫描或直接选择 WiFi，填写 WiFi 密码，并设置至少 8 位的配置 AP 密码
4. 点击“测试连接并获取信息”，自动填入路由器分配的 IP、网关、子网掩码和 DNS
5. 点击“保存并连接”，页面会显示连接进度（连接在后台进行，页面不会卡住），成功后设备自动重启，在同一局域网访问 `http://设备IP/`（端口 80 可省略）

配网失败时页面会提示原因并保留原配置，不需要重新上电。

### 日常使用

| 页面 | 用途 |
| --- | --- |
| 运行概览 | 连接状态、局域网地址、公网地址、DDNS 服务商 |
| 网络配置 | 修改 WiFi、设备 IP、网关、掩码、DNS、Web 端口 |
| DDNS 服务 | 选择服务商、填写参数、设置同步周期 |
| 安全与访问 | 修改配置 AP 密码 |

### DDNS 怎么填

| 服务商 | 需要填写 |
| --- | --- |
| `generic` 通用 Callback | 域名、用户名、密码、更新 URL |
| 阿里云 DNS | AccessKey ID / Secret、根域名、RR 主机记录、记录类型 |
| Cloudflare | API Token、Zone ID、Record ID，域名填完整记录名 |
| 腾讯云 DNSPod | SecretId、SecretKey、主域名、子域名、记录类型 |

通用 Callback 的更新 URL 支持 `{hostname}`、`{ip}`、`{user}`、`{password}`，也兼容 `#{ip}`、`#{domain}`、`#{recordType}`、`#{ttl}`：

```text
https://dynupdate.no-ip.com/nic/update?hostname={hostname}&myip={ip}
https://www.duckdns.org/update?domains={hostname}&token={password}&ip={ip}
https://dynv6.com/api/update?hostname={hostname}&token={password}&ipv4={ip}
```

### 默认值

| 项目 | 默认值 |
| --- | --- |
| 静态 IP | `192.168.1.50` |
| 网关 | `192.168.1.1` |
| 子网掩码 | `255.255.255.0` |
| DNS | `8.8.8.8` |
| Web 端口 | `80` |
| DDNS 检查间隔 | 600 秒 |
| DDNS 失败重试 | 60 秒 |
| DDNS 强制更新 | 86400 秒 |

### 安全提示

Web 服务使用 HTTP 且没有登录认证，只建议在可信局域网内访问，不要把端口直接映射到公网。

## 配置与安全约定

- HTTPS 默认校验证书，内置信任根：`ISRG Root X1`、`GTS Root R4`、`DigiCert Global Root G2`、`Amazon Root CA 1`、`GlobalSign Root CA - R3`。自建、自签证书或服务端不支持 TLS 最大分片长度扩展时，可在 DDNS 页取消勾选“校验 HTTPS 证书”（勾选时串口会输出 `[tls]` 开头的失败原因）
- 证书校验依赖系统时间，设备启动后通过 NTP 校时；时间未就绪时本轮 DDNS 会跳过并在下一周期重试
- 密码与密钥不再回显：表单留空表示保持原值，输入 `-` 表示清空该项（WiFi 密码、DDNS 密码、AccessKey Secret、API Token、SecretKey、配置 AP 密码）
- 服务商与记录类型做白名单校验，端口、同步周期等参数非法时会直接报错，不会静默改写
- 配置存入 EEPROM 时带上 CRC 校验，写入失败会输出日志，读取到损坏内容时回退默认值

---

运行原理、DDNS 调度与兜底机制、厂商接口实现细节、通用模式完整教程见 [TECHNICAL.md](TECHNICAL.md)。
