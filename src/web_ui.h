#pragma once

const char WEB_STYLE[] PROGMEM = R"CSS(
:root{--bg:#0c1118;--panel:#121a24;--line:#263446;--text:#e7edf5;--muted:#8695a8;--cyan:#43c6e8;--green:#35c98b;--amber:#f2b45b;--red:#ef6b73}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 Arial,"Microsoft YaHei",sans-serif}
a{color:var(--cyan)}
header{height:66px;border-bottom:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;padding:0 26px;background:#0e151f}
header strong{font-size:17px;letter-spacing:.05em}
header small{display:block;color:var(--muted);font-size:12px;margin-top:2px}
.right{display:flex;align-items:center;gap:16px}
.live{display:flex;align-items:center;gap:8px;color:var(--green);font-size:12px}
.live i{width:8px;height:8px;border-radius:50%;background:currentColor;box-shadow:0 0 10px currentColor}
.live.off{color:var(--amber)}
.lang{background:transparent;border:1px solid var(--line);color:var(--text);border-radius:4px;padding:7px 11px;cursor:pointer;font:inherit}
.layout{display:flex;min-height:calc(100vh - 66px)}
aside{width:222px;background:#0e151f;border-right:1px solid var(--line);padding:22px 13px}
aside .label{color:#536477;font-size:11px;letter-spacing:.12em;padding:0 12px 10px}
aside button{display:flex;align-items:center;gap:11px;width:100%;border:0;background:transparent;color:#8e9caf;padding:11px 12px;margin:3px 0;border-radius:4px;text-align:left;cursor:pointer;font:inherit}
aside button b{width:7px;height:7px;border:1px solid currentColor;border-radius:50%}
aside button.active,aside button:hover{background:#172534;color:var(--cyan)}
main{width:100%;max-width:1180px;margin:0 auto;padding:26px 30px 48px}
.topline{display:flex;justify-content:space-between;align-items:flex-end;margin-bottom:22px;gap:14px}
.topline h1{font-size:24px;margin:0 0 4px}
.topline p{color:var(--muted);margin:0}
.note{color:var(--muted);font-size:12px}
.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:16px}
.metric{background:var(--panel);border:1px solid var(--line);border-radius:5px;padding:16px 17px;min-height:100px}
.metric .cap{font-size:11px;color:var(--muted);letter-spacing:.08em}
.metric strong{display:block;font-size:20px;margin-top:12px;word-break:break-all}
.metric .sub{color:var(--muted);font-size:12px;margin-top:5px}
.metric.ok strong{color:var(--green)}.metric.cyan strong{color:var(--cyan)}.metric.warn strong{color:var(--amber)}
.section{background:var(--panel);border:1px solid var(--line);border-radius:5px;margin-bottom:14px}
.section-head{padding:15px 19px;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:center}
.section-head h2{font-size:15px;margin:0}
.section-head span{font-size:11px;color:var(--muted);letter-spacing:.1em}
.section-body{padding:19px}
.form{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:15px 19px}
label{display:grid;gap:7px;color:#a9b7c7;font-size:12px}
label.check{display:flex;align-items:center;gap:9px;margin-bottom:15px;color:#c3d2e2;font-size:13px}
label.check input{width:auto;margin:0}
input,select,textarea{width:100%;background:#0d141d;border:1px solid #2a3a4e;color:var(--text);border-radius:4px;padding:10px 11px;font:inherit}
textarea{min-height:76px;resize:vertical}
input:focus,select:focus,textarea:focus{outline:0;border-color:var(--cyan)}
.full{grid-column:1/-1}
.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}
.hidden{display:none}
.actions{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-top:16px}
.btn{border:1px solid var(--line);background:#172534;color:var(--text);border-radius:4px;padding:10px 16px;cursor:pointer;font:inherit}
.save{border:0;border-radius:4px;background:var(--cyan);color:#071018;padding:11px 24px;font-weight:700;cursor:pointer}
.alert{border-left:3px solid var(--cyan);background:#112330;color:#b9d7e1;padding:13px 16px;margin-bottom:16px;font-size:13px}
.alert.err{border-left-color:var(--red)}
.alert.ok{border-left-color:var(--green)}
.steps{margin:6px 0 0 18px;padding:0}
.steps li{margin:3px 0}
@media(max-width:780px){
header{padding:0 15px}
aside{width:54px;padding:18px 6px}
aside .label,aside button span{display:none}
aside button{justify-content:center;padding:12px 0}
.metrics{grid-template-columns:repeat(2,1fr)}
main{padding:18px 14px}
.form{grid-template-columns:1fr}
.topline{flex-direction:column;align-items:flex-start}
.actions{flex-direction:column;align-items:stretch}
}
)CSS";

const char WEB_SCRIPT[] PROGMEM = R"JS(
var DICT={
'nav.overview':['运行概览','Overview'],'nav.network':['网络配置','Network'],'nav.ddns':['DDNS 服务','DDNS service'],'nav.security':['安全与访问','Security'],
'desc.dash':['设备连接、地址与动态域名状态','Connection, addresses and dynamic DNS status'],
'desc.network':['配置 WiFi、设备地址和 Web 服务端口','WiFi, device address and web service port'],
'desc.ddns':['配置动态域名服务商与同步策略','DDNS provider and sync strategy'],
'desc.security':['配置模式访问密码与安全选项','Setup AP password and security'],
'title.dash':['运行概览','Overview'],'title.setup':['连接 WiFi 网络','Connect to WiFi'],
'desc.setup':['选择要连接的网络并自动获取地址信息','Select a network and fetch address automatically'],
'sub.dash':['网络服务与 DDNS 管理台','Network and DDNS console'],'sub.setup':['初始配置向导','Initial setup wizard'],
'state.online':['设备在线','Online'],'state.setup':['配置模式','Setup mode'],
'metric.conn':['连接状态','Connection'],'metric.lan':['局域网地址','LAN address'],'metric.wan':['公网地址','Public address'],'metric.provider':['DDNS 服务商','DDNS provider'],
'metric.connSub':['WiFi station','WiFi station'],'metric.lanSub':['Web 服务地址','Web service address'],'metric.wanSub':['DDNS 上报值','DDNS source'],'metric.providerSub':['自动同步任务','Sync worker'],
'val.on':['已连接','Connected'],'val.off':['未连接','Offline'],'val.none':['未获取','Unavailable'],
'sec.wifi':['无线网络','Wireless'],'sec.addr':['网络地址','Network address'],'sec.network':['网络配置','Network'],'sec.ddns':['DDNS 服务','DDNS service'],
'sec.aliyun':['阿里云 DNS','Alibaba Cloud DNS'],'sec.cloudflare':['Cloudflare DNS','Cloudflare DNS'],'sec.dnspod':['腾讯云 DNSPod','Tencent DNSPod'],'sec.callback':['Callback 与调度','Callback and schedule'],'sec.security':['安全与访问','Security'],
'lab.ssid':['WiFi 名称','WiFi name'],'lab.ssidManual':['手动输入 WiFi 名称','Enter WiFi name manually'],'lab.wifipass':['WiFi 密码','WiFi password'],'lab.ap':['配置 AP 密码','Setup AP password'],'lab.port':['Web 端口','Web port'],
'lab.ip':['设备 IP','Device IP'],'lab.gateway':['网关','Gateway'],'lab.subnet':['子网掩码','Subnet mask'],'lab.dns':['DNS','DNS'],
'lab.autoNet':['自动获取并填写网络信息（推荐）','Auto fetch and fill network information (recommended)'],
'lab.provider':['服务商','Provider'],'lab.domain':['域名','Domain'],'lab.user':['用户名','Username'],'lab.pass':['密码','Password'],'lab.url':['更新 URL','Update URL'],
'lab.akid':['AccessKey ID','AccessKey ID'],'lab.aksecret':['AccessKey Secret','AccessKey Secret'],'lab.rootdomain':['根域名','Root domain'],'lab.rr':['RR 主机记录','RR host record'],'lab.rtype':['记录类型','Record type'],
'lab.cftoken':['API Token','API Token'],'lab.cfzone':['Zone ID','Zone ID'],'lab.cfrecord':['Record ID','Record ID'],
'lab.txid':['SecretId','SecretId'],'lab.txsecret':['SecretKey','SecretKey'],'lab.txdomain':['主域名','Root domain'],'lab.txsub':['子域名','Subdomain'],'lab.txtype':['记录类型','Record type'],
'lab.ipurl':['公网 IP 查询 URL','Public IP lookup URL'],'lab.success':['成功关键字','Success keyword'],'lab.method':['Callback 方法','Callback method'],
'lab.interval':['检查间隔秒','Check interval (sec)'],'lab.retry':['失败重试秒','Failure retry (sec)'],'lab.force':['强制更新秒','Force update (sec)'],
'lab.body':['Callback 请求体','Callback body'],'lab.headers':['Callback Headers','Callback Headers'],'lab.appass':['配置 AP 新密码','New setup AP password'],
'opt.generic':['通用 Callback','Generic Callback'],'opt.aliyun':['阿里云 DNS','Alibaba Cloud DNS'],'opt.cloudflare':['Cloudflare','Cloudflare'],'opt.dnspod':['腾讯云 DNSPod','Tencent DNSPod'],'opt.pickNetwork':['请先扫描或选择手动输入','Scan or choose manual input'],'opt.manualInput':['手动输入名称…','Manual input...'],
'btn.save':['保存并重启','Save and restart'],'btn.connect':['保存并连接','Save and connect'],'btn.scan':['扫描附近 WiFi','Scan WiFi'],'btn.probe':['测试连接并获取信息','Test and fetch info'],'btn.auto':['自动获取网络信息','Auto fetch network info'],'btn.fill':['一键填入地址','Fill addresses'],
'hint.scan':['扫描结果会填入下方下拉列表','Scan results are added to the list below'],
'hint.steps':['操作步骤','Steps'],'step1':['连接无线网络','Connect to wireless network'],'step1b':['（首次无密码）','(no password on first boot)'],
'step2':['手动打开','Open manually'],'step3':['选择家庭 WiFi，设置配置密码并保存','Select your WiFi, set the setup password and save'],
'note.ap':['配置 AP：','Setup AP: '],'note.port':['Web 端口：','Web port: '],'note.restart':['配置成功后设备会重启','The device restarts after a successful setup'],
'hint.filled':['已填入获取到的地址信息','Address information filled'],'hint.fetchFail':['获取失败，请检查网络后重试','Fetch failed, check the network and retry'],
'hint.onlyA':['仅 A 记录支持自动更新','Only A records are updated automatically'],
'hint.ipAccess':['此 IP 为用户连接该网络后访问本设备所需的地址，同网络的设备用 http://该IP 打开管理页','This IP is the address users need to visit after connecting to that network, e.g. http://the-ip'],
'hint.probing':['正在测试连接，请稍候...','Testing connection, please wait...'],'hint.probeOk':['获取成功，已填入地址：','Fetched, address filled:'],'hint.probeWarn':['（若配网页断开，请重新连接 ESP8266-Setup 后再保存）','(If this page disconnects, reconnect to ESP8266-Setup and save)'],'hint.probeFail':['连接失败，请确认 WiFi 名称和密码','Connection failed, verify the WiFi name and password'],
'hint.manual':['取消勾选后使用下方手动填写的地址','When unchecked, the addresses below are used'],
'hint.secret':['密码与密钥不再回显：留空保持原值，输入 - 清空已保存内容','Passwords and keys are never shown: leave blank to keep, enter - to clear'],
'hint.secretShort':['留空保持不变','Leave blank to keep'],
'hint.connecting':['正在连接 WiFi，请稍候…','Connecting to WiFi, please wait...'],
'hint.connectOk':['连接成功，正在保存并重启设备','Connected, saving and restarting the device'],
'hint.connectFail':['连接失败，请确认 WiFi 名称与密码后重试','Connection failed, check the WiFi name and password'],
'lab.tls':['校验 HTTPS 证书（自建或自签证书可取消勾选）','Verify HTTPS certificates (uncheck for self-signed services)']
};
var EN=false;
function L(k){var v=DICT[k];return v?(EN?v[1]:v[0]):k}
function applyLang(){
 document.querySelectorAll('[data-i18n]').forEach(function(el){var k=el.dataset.i18n;if(!k||!DICT[k])return;if(el.children.length>0)return;el.textContent=L(k)});
 document.querySelectorAll('[data-i18n-ph]').forEach(function(el){var k=el.dataset.i18nPh;if(k&&DICT[k])el.placeholder=L(k)});
 var lb=document.getElementById('language'); if(lb)lb.textContent=EN?'中文':'English';
 document.documentElement.lang=EN?'en':'zh-CN';
 document.title=EN?'ESP8266 Console':'ESP8266 控制台';
}
function setText(id,text){var el=document.getElementById(id);if(el)el.textContent=text}
function fillAddress(d){
 ['ip','gateway','subnet','dns'].forEach(function(name){
  var el=document.querySelector('[name="'+name+'"]');
  if(el&&d[name])el.value=d[name];
 });
}
function getJson(url,options){return fetch(url,options).then(function(r){return r.json()})}
function formBody(ids){var p=new URLSearchParams();ids.forEach(function(id){var el=document.getElementById(id);if(el)p.append(el.name,el.value)});return p}
)JS";

const char WEB_SETUP_BODY[] PROGMEM = R"HTML(
<header><div><strong>ESP8266 CORE</strong><small id="subtitle" data-i18n="sub.setup">初始配置向导</small></div><div class="right"><div class="live off"><i></i><span id="stateText" data-i18n="state.setup">配置模式</span></div><button class="lang" id="language" type="button">English</button></div></header>
<main style="max-width:900px">
<div class="topline"><div><h1 id="pageTitle" data-i18n="title.setup">连接 WiFi 网络</h1><p id="pageDesc" data-i18n="desc.setup">选择要连接的网络并自动获取地址信息</p></div><div class="note" data-i18n="note.restart">配置成功后设备会重启</div></div>
<div class="alert"><b data-i18n="hint.steps">操作步骤</b>
<ol class="steps">
<li><span data-i18n="step1">连接无线网络</span> <b>__AP_SSID__</b> <span data-i18n="step1b">（首次无密码）</span></li>
<li><span data-i18n="step2">手动打开</span> <b>http://__AP_IP__</b></li>
<li data-i18n="step3">选择家庭 WiFi，设置配置密码并保存</li>
</ol></div>
__MESSAGE__
<form method="post" action="/save">
<section class="section"><div class="section-head"><h2 data-i18n="sec.wifi">无线网络</h2><span>WIFI</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.ssid">WiFi 名称</span><select id="ssidSelect"></select></label>
<label id="ssidManualWrap" class="hidden"><span data-i18n="lab.ssidManual">手动输入 WiFi 名称</span><input id="ssidManual" value="__SSID__"></label>
<input type="hidden" id="ssid" name="ssid" value="__SSID__">
<label><span data-i18n="lab.wifipass">WiFi 密码</span><input id="wifiPassword" name="wifiPassword" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label><span data-i18n="lab.ap">配置 AP 密码</span><input name="apPassword" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label><span data-i18n="lab.port">Web 端口</span><input name="port" type="number" value="__PORT__"></label>
<div class="row full"><button class="btn" type="button" id="scanBtn" data-i18n="btn.scan">扫描附近 WiFi</button><span id="scanInfo" class="note"></span></div>
</div></div></section>
<section class="section"><div class="section-head"><h2 data-i18n="sec.addr">网络地址</h2><span>ADDRESS</span></div><div class="section-body">
<label class="check"><input type="checkbox" id="autoNet" name="autoNet" value="1" checked><span data-i18n="lab.autoNet">自动获取并填写网络信息（推荐）</span></label>
<div class="form">
<label><span data-i18n="lab.ip">设备 IP</span><input id="ip" name="ip" value="__IP__"></label>
<label><span data-i18n="lab.gateway">网关</span><input id="gateway" name="gateway" value="__GATEWAY__"></label>
<label><span data-i18n="lab.subnet">子网掩码</span><input id="subnet" name="subnet" value="__SUBNET__"></label>
<label><span data-i18n="lab.dns">DNS</span><input id="dns" name="dns" value="__DNS__"></label>
<span class="note full" data-i18n="hint.ipAccess">此 IP 为用户连接该网络后访问本设备所需的地址，同网络的设备用 http://该IP 打开管理页</span>
</div>
<div class="row" style="margin-top:15px"><button class="btn" type="button" id="probeBtn" data-i18n="btn.probe">测试连接并获取信息</button><span id="probeInfo" class="note"></span></div>
<span id="probeWarn" class="note"></span>
<span class="note full" data-i18n="hint.secret">密码与密钥不再回显：留空保持原值，输入 - 清空已保存内容</span>
</div></section>
<div class="actions"><span id="saveInfo" class="note"></span><button class="save" type="submit" data-i18n="btn.connect">保存并连接</button></div>
</form>
<span id="connectState" class="hidden" data-state="__CONNECT_STATE__"></span></main>
<script>
(function(){
var scan=document.getElementById('scanBtn'),info=document.getElementById('scanInfo'),probe=document.getElementById('probeBtn'),probeInfo=document.getElementById('probeInfo');
var scanState={kind:'idle',count:0};
var probeState={kind:'idle',ip:''};
function renderScan(){if(scanState.kind==='probing'){info.textContent=L('hint.probing')}else if(scanState.kind==='ok'){info.textContent=EN?(scanState.count+' networks found'):('发现 '+scanState.count+' 个网络，请在下拉框中选择')}else if(scanState.kind==='fail'){info.textContent=L('hint.fetchFail')}else{info.textContent=L('hint.scan')}}
function renderProbe(){var w=document.getElementById('probeWarn');if(probeState.kind==='probing'){probeInfo.textContent=L('hint.probing');w.textContent=''}else if(probeState.kind==='ok'){probeInfo.textContent=L('hint.probeOk')+probeState.ip;w.textContent=L('hint.probeWarn')}else if(probeState.kind==='fail'){probeInfo.textContent=L('hint.probeFail');w.textContent=''}else if(probeState.kind==='error'){probeInfo.textContent=L('hint.fetchFail');w.textContent=''}else{probeInfo.textContent=L('hint.manual');w.textContent=''}}
var sel=document.getElementById('ssidSelect'),manualWrap=document.getElementById('ssidManualWrap'),manual=document.getElementById('ssidManual'),hidden=document.getElementById('ssid');
var lastScan=null;
function currentSsid(){return hidden.value}
function syncSsid(){
 if(sel.value==='__manual__'){manualWrap.classList.remove('hidden');hidden.value=manual.value;}
 else{manualWrap.classList.add('hidden');hidden.value=sel.value;}
}
function fillList(list,keep){
 sel.innerHTML='';
 var ph=document.createElement('option');ph.value='';ph.textContent=L('opt.pickNetwork');sel.appendChild(ph);
 (list||[]).forEach(function(n){var o=document.createElement('option');o.value=n.s;o.textContent=n.s+'  ('+n.r+' dBm)';sel.appendChild(o)});
 var m=document.createElement('option');m.value='__manual__';m.textContent=L('opt.manualInput');sel.appendChild(m);
 var want=keep||'';
 if(want){
  var hit=false;
  for(var i=0;i<sel.options.length;i++){if(sel.options[i].value===want){sel.selectedIndex=i;hit=true;break;}}
  if(!hit){var e=document.createElement('option');e.value=want;e.textContent=want;sel.insertBefore(e,sel.firstChild.nextSibling);sel.value=want;}
 }
 syncSsid();
}
sel.onchange=syncSsid;
manual.oninput=function(){hidden.value=manual.value};
scan.onclick=function(){scanState.kind='probing';renderScan();getJson('/scan').then(function(a){lastScan=a;fillList(a,currentSsid());scanState.kind='ok';scanState.count=a.length;renderScan()}).catch(function(){scanState.kind='fail';renderScan()})};
function probeResult(s){if(s.state==='ok'){probeState.kind='ok';probeState.ip=s.ip||'';fillAddress(s)}else{probeState.kind='fail'}renderProbe()}
function pollProbe(){getJson('/probe').then(function(s){if(s.state==='pending'){setTimeout(pollProbe,1500);return}probeResult(s)}).catch(function(){probeState.kind='error';renderProbe()})}
probe.onclick=function(){probeState.kind='probing';renderProbe();getJson('/probe',{method:'POST',body:formBody(['ssid','wifiPassword'])}).then(function(s){if(s.state==='pending'){setTimeout(pollProbe,1500)}else{probeResult(s)}}).catch(function(){probeState.kind='error';renderProbe()})};
var connectEl=document.getElementById('connectState');
var connectKind=connectEl?connectEl.dataset.state:'idle';
function renderConnect(){var el=document.getElementById('saveInfo');if(!el)return;if(connectKind==='pending'){el.textContent=L('hint.connecting')}else if(connectKind==='ok'){el.textContent=L('hint.connectOk')}else if(connectKind==='fail'){el.textContent=L('hint.connectFail')}else{el.textContent=''}}
function pollConnect(){getJson('/connectstate').then(function(s){connectKind=s.state;renderConnect();if(s.state==='pending'){setTimeout(pollConnect,1500)}}).catch(function(){})}
if(connectKind==='pending'){setTimeout(pollConnect,1500)}
document.getElementById('language').onclick=function(){EN=!EN;applyLang();fillList(lastScan,currentSsid());renderScan();renderProbe();renderConnect()};
fillList(null,currentSsid());
applyLang();
renderScan();
renderProbe();
renderConnect();
})();
</script>
)HTML";

const char WEB_DASHBOARD_BODY[] PROGMEM = R"HTML(
<header><div><strong>ESP8266 CORE</strong><small id="subtitle" data-i18n="sub.dash">网络服务与 DDNS 管理台</small></div><div class="right"><div class="live"><i></i><span id="stateText" data-i18n="state.online">设备在线</span></div><button class="lang" id="language" type="button">English</button></div></header>
<div class="layout"><aside><div class="label">WORKSPACE</div>
<button class="active" data-view="overview" data-desc="desc.dash" data-i18n="nav.overview"><b></b><span data-i18n="nav.overview">运行概览</span></button>
<button data-view="network" data-desc="desc.network" data-i18n="nav.network"><b></b><span data-i18n="nav.network">网络配置</span></button>
<button data-view="ddns" data-desc="desc.ddns" data-i18n="nav.ddns"><b></b><span data-i18n="nav.ddns">DDNS 服务</span></button>
<button data-view="security" data-desc="desc.security" data-i18n="nav.security"><b></b><span data-i18n="nav.security">安全与访问</span></button>
</aside>
<main><div class="topline"><div><h1 id="pageTitle" data-i18n="title.dash">运行概览</h1><p id="pageDesc" data-i18n="desc.dash">设备连接、地址与动态域名状态</p></div><div class="note"><span data-i18n="note.ap">配置 AP：</span>__AP_SSID__ · <span data-i18n="note.port">Web 端口：</span>__PORT__</div></div>
<div class="metrics">
<div class="metric ok"><div class="cap" data-i18n="metric.conn">连接状态</div><strong data-i18n="__STATUS_KEY__">__STATUS__</strong><div class="sub" data-i18n="metric.connSub">WiFi station</div></div>
<div class="metric cyan"><div class="cap" data-i18n="metric.lan">局域网地址</div><strong>__LAN_IP__</strong><div class="sub" data-i18n="metric.lanSub">Web 服务地址</div></div>
<div class="metric cyan"><div class="cap" data-i18n="metric.wan">公网地址</div><strong data-i18n="__WAN_KEY__">__PUBLIC_IP__</strong><div class="sub" data-i18n="metric.wanSub">DDNS 上报值</div></div>
<div class="metric warn"><div class="cap" data-i18n="metric.provider">DDNS 服务商</div><strong>__PROVIDER__</strong><div class="sub" data-i18n="metric.providerSub">自动同步任务</div></div>
</div>
__MESSAGE__
<form method="post" action="/save">
<section class="section view overview network"><div class="section-head"><h2 data-i18n="sec.network">网络配置</h2><span>NETWORK</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.ssid">WiFi 名称</span><select id="ssidSelect"></select></label>
<label id="ssidManualWrap" class="hidden"><span data-i18n="lab.ssidManual">手动输入 WiFi 名称</span><input id="ssidManual" value="__SSID__"></label>
<input type="hidden" id="ssid" name="ssid" value="__SSID__">
<label><span data-i18n="lab.wifipass">WiFi 密码</span><input name="wifiPassword" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<div class="row full"><button class="btn" type="button" id="scanBtn" data-i18n="btn.scan">扫描附近 WiFi</button><span id="scanInfo" class="note"></span></div>
<label><span data-i18n="lab.ip">设备 IP</span><input name="ip" value="__IP__"></label>
<label><span data-i18n="lab.gateway">网关</span><input name="gateway" value="__GATEWAY__"></label>
<label><span data-i18n="lab.subnet">子网掩码</span><input name="subnet" value="__SUBNET__"></label>
<label><span data-i18n="lab.dns">DNS</span><input name="dns" value="__DNS__"></label>
<label><span data-i18n="lab.port">Web 端口</span><input name="port" type="number" value="__PORT__"></label>
<span class="note full" data-i18n="hint.ipAccess">此 IP 为用户连接该网络后访问本设备所需的地址，同网络的设备用 http://该IP 打开管理页</span>
<div class="row full"><button class="btn" type="button" id="autoBtn" data-i18n="btn.auto">自动获取网络信息</button><span id="autoInfo" class="note"></span></div>
</div></div></section>
<section class="section view overview ddns"><div class="section-head"><h2 data-i18n="sec.ddns">DDNS 服务</h2><span>DOMAIN UPDATE</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.provider">服务商</span><select name="ddnsProvider" id="provider"><option value="generic" data-i18n="opt.generic">通用 Callback</option><option value="aliyun" data-i18n="opt.aliyun">阿里云 DNS</option><option value="cloudflare" data-i18n="opt.cloudflare">Cloudflare</option><option value="dnspod" data-i18n="opt.dnspod">腾讯云 DNSPod</option></select></label>
<label><span data-i18n="lab.domain">域名</span><input name="hostname" value="__HOSTNAME__"></label>
<label><span data-i18n="lab.user">用户名</span><input name="ddnsUsername" value="__DDNS_USERNAME__"></label>
<label><span data-i18n="lab.pass">密码</span><input name="ddnsPassword" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label class="full"><span data-i18n="lab.url">更新 URL</span><input name="ddnsUrl" value="__DDNS_URL__"></label>
</div></div></section>
<section class="section view ddns hidden" id="aliyun"><div class="section-head"><h2 data-i18n="sec.aliyun">阿里云 DNS</h2><span>ALIYUN API</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.akid">AccessKey ID</span><input name="aliyunAccessKeyId" value="__ALIYUN_ID__"></label>
<label><span data-i18n="lab.aksecret">AccessKey Secret</span><input name="aliyunAccessKeySecret" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label><span data-i18n="lab.rootdomain">根域名</span><input name="aliyunDomainName" value="__ALIYUN_DOMAIN__"></label>
<label><span data-i18n="lab.rr">RR 主机记录</span><input name="aliyunRR" value="__ALIYUN_RR__"></label>
<label><span data-i18n="lab.rtype">记录类型</span><select name="aliyunType" id="aliyunType"><option value="A">A</option><option value="AAAA">AAAA</option><option value="CNAME">CNAME</option></select></label>
<span class="note full" data-i18n="hint.onlyA">仅 A 记录支持自动更新</span>
</div></div></section>
<section class="section view ddns hidden" id="cloudflare"><div class="section-head"><h2 data-i18n="sec.cloudflare">Cloudflare DNS</h2><span>CLOUDFLARE API</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.cftoken">API Token</span><input name="cloudflareApiToken" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label><span data-i18n="lab.cfzone">Zone ID</span><input name="cloudflareZoneId" value="__CF_ZONE__"></label>
<label><span data-i18n="lab.cfrecord">Record ID</span><input name="cloudflareRecordId" value="__CF_RECORD__"></label>
</div></div></section>
<section class="section view ddns hidden" id="dnspod"><div class="section-head"><h2 data-i18n="sec.dnspod">腾讯云 DNSPod</h2><span>DNSPOD API</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.txid">SecretId</span><input name="tencentSecretId" value="__TX_ID__"></label>
<label><span data-i18n="lab.txsecret">SecretKey</span><input name="tencentSecretKey" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<label><span data-i18n="lab.txdomain">主域名</span><input name="tencentDomain" value="__TX_DOMAIN__"></label>
<label><span data-i18n="lab.txsub">子域名</span><input name="tencentSubDomain" value="__TX_SUBDOMAIN__"></label>
<label><span data-i18n="lab.txtype">记录类型</span><select name="tencentRecordType" id="tencentRecordType"><option value="A">A</option><option value="AAAA">AAAA</option><option value="CNAME">CNAME</option></select></label>
<span class="note full" data-i18n="hint.onlyA">仅 A 记录支持自动更新</span>
</div></div></section>
<section class="section view ddns hidden"><div class="section-head"><h2 data-i18n="sec.callback">Callback 与调度</h2><span>CALLBACK WORKER</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.ipurl">公网 IP 查询 URL</span><input name="ddnsIpUrl" value="__IP_URL__"></label>
<label><span data-i18n="lab.success">成功关键字</span><input name="ddnsSuccess" value="__SUCCESS__"></label>
<label><span data-i18n="lab.method">Callback 方法</span><select id="method" name="ddnsMethod"><option value="GET">GET</option><option value="POST">POST</option></select></label>
<label><span data-i18n="lab.interval">检查间隔秒</span><input name="ddnsIntervalSec" type="number" value="__INTERVAL__"></label>
<label><span data-i18n="lab.retry">失败重试秒</span><input name="ddnsRetrySec" type="number" value="__RETRY__"></label>
<label><span data-i18n="lab.force">强制更新秒</span><input name="ddnsForceSec" type="number" value="__FORCE__"></label>
<label class="full"><span data-i18n="lab.body">Callback 请求体</span><textarea name="ddnsBody">__BODY__</textarea></label>
<label class="full"><span data-i18n="lab.headers">Callback Headers</span><textarea name="ddnsHeaders">__HEADERS__</textarea></label>
<label class="check full"><input type="checkbox" name="tlsInsecure" value="1"__TLS_CHECKED__><span data-i18n="lab.tls">校验 HTTPS 证书（自建或自签证书可取消勾选）</span></label>
<span class="note full" data-i18n="hint.secret">密码与密钥不再回显：留空保持原值，输入 - 清空已保存内容</span>
</div></div></section>
<section class="section view security hidden"><div class="section-head"><h2 data-i18n="sec.security">安全与访问</h2><span>ACCESS CONTROL</span></div><div class="section-body"><div class="form">
<label><span data-i18n="lab.appass">配置 AP 新密码</span><input name="apPassword" type="password" value="" data-i18n-ph="hint.secretShort"></label>
<span class="note full" data-i18n="hint.secret">密码与密钥不再回显：留空保持原值，输入 - 清空已保存内容</span>
</div></div></section>
<div class="actions"><span class="note"></span><button class="save" type="submit" data-i18n="btn.save">保存并重启</button></div>
</form></main></div>
<script>
(function(){
var provider='__PROVIDER__';
document.getElementById('provider').value=provider;
var methodSel=document.getElementById('method');
methodSel.value='__METHOD__';
if(!methodSel.value){var mo=document.createElement('option');mo.value='__METHOD__';mo.textContent='__METHOD__';methodSel.appendChild(mo);methodSel.value='__METHOD__';}
function vendor(){['aliyun','cloudflare','dnspod'].forEach(function(id){document.getElementById(id).classList.toggle('hidden',document.getElementById('provider').value!==id)})}
document.getElementById('provider').onchange=vendor;
function showView(b){
 document.querySelectorAll('.view').forEach(function(x){x.classList.add('hidden')});
 document.querySelectorAll('[data-view]').forEach(function(x){x.classList.remove('active')});
 b.classList.add('active');
 document.querySelectorAll('.'+b.dataset.view).forEach(function(x){x.classList.remove('hidden')});
 vendor();
 var pt=document.getElementById('pageTitle'),pd=document.getElementById('pageDesc');
 pt.dataset.i18n=b.dataset.i18n; pd.dataset.i18n=b.dataset.desc; applyLang();
}
document.querySelectorAll('[data-view]').forEach(function(b){b.onclick=function(){showView(b)}});
var autoState={kind:'idle'};
function renderAuto(){var el=document.getElementById('autoInfo');if(autoState.kind==='busy'){el.textContent=L('hint.probing')}else if(autoState.kind==='ok'){el.textContent=L('hint.filled')}else if(autoState.kind==='fail'){el.textContent=L('hint.fetchFail')}else{el.textContent=''}}
document.getElementById('autoBtn').onclick=function(){autoState.kind='busy';renderAuto();getJson('/netinfo').then(function(d){fillAddress(d);autoState.kind='ok';renderAuto()}).catch(function(){autoState.kind='fail';renderAuto()})};
function pickSelect(id,value){var s=document.getElementById(id);if(!s)return;s.value=value;if(!s.value&&value){var o=document.createElement('option');o.value=value;o.textContent=value;s.appendChild(o);s.value=value}}
pickSelect('aliyunType','__ALIYUN_TYPE__');
pickSelect('tencentRecordType','__TX_TYPE__');
var sel=document.getElementById('ssidSelect'),manualWrap=document.getElementById('ssidManualWrap'),manual=document.getElementById('ssidManual'),hidden=document.getElementById('ssid');
var lastScan=null,scanState={kind:'idle',count:0};
function currentSsid(){return hidden.value}
function syncSsid(){if(sel.value==='__manual__'){manualWrap.classList.remove('hidden');hidden.value=manual.value}else{manualWrap.classList.add('hidden');hidden.value=sel.value}}
function fillList(list,keep){sel.innerHTML='';var ph=document.createElement('option');ph.value='';ph.textContent=L('opt.pickNetwork');sel.appendChild(ph);(list||[]).forEach(function(n){var o=document.createElement('option');o.value=n.s;o.textContent=n.s+'  ('+n.r+' dBm)';sel.appendChild(o)});var m=document.createElement('option');m.value='__manual__';m.textContent=L('opt.manualInput');sel.appendChild(m);var want=keep||'';if(want){var hit=false;for(var i=0;i<sel.options.length;i++){if(sel.options[i].value===want){sel.selectedIndex=i;hit=true;break}}if(!hit){var e=document.createElement('option');e.value=want;e.textContent=want;sel.insertBefore(e,sel.firstChild.nextSibling);sel.value=want}}syncSsid()}
function renderScan(){var el=document.getElementById('scanInfo');if(scanState.kind==='busy'){el.textContent=L('hint.probing')}else if(scanState.kind==='ok'){el.textContent=EN?(scanState.count+' networks found'):('发现 '+scanState.count+' 个网络，请在下拉框中选择')}else if(scanState.kind==='fail'){el.textContent=L('hint.fetchFail')}else{el.textContent=''}}
sel.onchange=syncSsid;
manual.oninput=function(){hidden.value=manual.value};
document.getElementById('scanBtn').onclick=function(){scanState.kind='busy';renderScan();getJson('/scan').then(function(a){lastScan=a;fillList(a,currentSsid());scanState.kind='ok';scanState.count=a.length;renderScan()}).catch(function(){scanState.kind='fail';renderScan()})};
document.getElementById('language').onclick=function(){EN=!EN;applyLang();renderAuto();fillList(lastScan,currentSsid());renderScan()};
vendor();
fillList(null,currentSsid());
applyLang();
renderAuto();
renderScan();
})();
</script>
)HTML";
