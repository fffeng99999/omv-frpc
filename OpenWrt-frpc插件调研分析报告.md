# OpenWrt frpc Web 管理插件调研分析报告

> 调研日期：2026-09-28
> 调研目的：为 OMV 8.x 自研 frpc 管理插件（openmediavault-frpc）提供功能与架构参考
> 上游背景：frp 最新版本 v0.71.0（2026-08-14）；本 NAS 现有部署为 Docker 官方镜像 `fatedier/frpc:v0.71.0`（host 网络，配置 `data/appdata/frpc/frpc.toml`）；Debian 13 (trixie) 官方源**不提供** frp/frpc 软件包（已核实 packages.debian.org）。

---

## 一、调研对象

| # | 项目 | 仓库 | 许可证 | 定位 |
|---|---|---|---|---|
| 1 | **kuoruan luci-app-frpc** | github.com/kuoruan/luci-app-frpc（配 kuoruan/openwrt-frp 二进制 ipk） | MIT | 2019–2021 事实标准的第三方 frpc 图形插件，中文教程几乎全部指它，iStoreOS 等固件内置 |
| 2 | **OpenWrt 官方 luci-app-frpc** | github.com/openwrt/luci `applications/luci-app-frpc` + github.com/openwrt/packages `net/frp` | Apache-2.0 | 2024 年起进入官方源的现代版重写（kuoruan 本人参与），支持 TOML 时代全部新特性；同时有 luci-app-frps |
| 3 | SakuraFrp / OpenFrp 启动器 | doc.natfrp.com | 闭源 SaaS | 商业 frp 服务商的账号型启动器（含 OpenWrt ipk/LuCI），隧道来自云端面板，**不是通用 frpc 配置管理**，仅作体验参考 |
| 4 | xfrpc / luci-app-xfrpc | github.com/liudf0716/xfrpc | GPL | 用 C 重写的第三方 frpc 协议实现，非上游 frp，生态小，不予采用 |

调研深度：#1、#2 逐文件读源码（init 脚本、CBI 表单、官方版 1531 行 JS、约 1000 行配置生成 shell、i18n 模板）。

---

## 二、kuoruan 版架构剖析（INI 时代经典）

### 2.1 数据流

```
LuCI 表单 ──写入──▶ UCI 数据库 /etc/config/frpc
                     ├─ section frpc(main)：实例开关、frpc 二进制路径、日志、心跳、admin、TLS…
                     ├─ section server：服务器档案（alias / addr / port / token / tcp_mux）
                     └─ section rule：代理规则
/etc/init.d/frpc start
   └─ shell 校验 UCI（uci_validate_section，带类型 bool/port/host/uinteger）
   └─ 渲染 /var/etc/frpc/frpc.<实例>.ini（[common] + [规则名] 段）
   └─ procd 拉起 "<client_file> -c <ini>"，respawn 崩溃重启
   └─ procd_add_reload_trigger：配置变更自动重启；file watch 配置文件
```

### 2.2 界面结构

- **Common Settings**：3 个 Tab（General / Advanced / Manage）
  - General：启用开关、**frpc 客户端文件路径（可配，自动 chmod 755 并执行 `-h` 校验是不是真 frpc）**、所属服务器（下拉来自 server 段，显示 alias 或 addr:port）、运行用户（从 /etc/passwd 枚举）、日志开关/文件/级别（trace~error)/天数/去色
  - Advanced：连接池池化数、代理名前缀 user、login_fail_exit、通信协议（tcp/kcp/websocket）、HTTP 代理、TLS、自定义 DNS、心跳间隔/超时
  - Manage：frpc 自带 admin 管理界面 addr/port/user/password
- **Servers**：服务器档案列表 + 详情页（支持多个 frps 切换）
- **Rules**：规则列表 + 详情页
- **状态**：页面顶部 XHR 每 5 秒轮询 `/admin/services/frpc/status`，显示绿色 Running / 红色 Not Running；表单内显示 `frpc -v` 版本号
- 中文语言包独立 ipk（luci-i18n-frpc-zh-cn）

### 2.3 规则表单覆盖的字段

- 类型：**tcp / udp / http / https / stcp / xtcp**；每条规则有 Disabled 开关
- 基础：local_ip、local_port、remote_port（TCP/UDP）、use_encryption、use_compression
- HTTP/HTTPS：subdomain、custom_domains、locations、host_header_rewrite、http_user/http_pwd、proxy_protocol_version
- 内网穿透插件（frp 内置 plugin）：unix_domain_socket、http_proxy、socks5、static_file、https2http——每种插件按选择显示各自参数（本地路径/前缀/认证/证书/host 改写）
- STCP/XTCP：role、server_name、sk（密钥）、bind_addr、bind_port
- 其他：group/group_key（负载均衡）、health_check（tcp/http、url、超时、失败次数、间隔）
- **extra_options 逃生舱**：任意 `key=value` 原样追加，支持新字段不必升级插件

---

## 三、OpenWrt 官方版架构剖析（TOML 时代标杆）

### 3.1 数据流

```
LuCI 客户端 JS 单页 ──▶ UCI /etc/config/frpc
                        ├─ section conf(common)：全局客户端配置（8 个 Tab 的字段）
                        ├─ section conf(其余每个)：一条 proxy 或 visitor（字段 role 区分）
                        └─ section init：stdout/stderr、运行用户/组、respawn、环境变量、include 片段
/etc/init.d/frpc（约 1000 行 POSIX shell）
   ├─ 带类型校验的 TOML 发射器（bool/int/port/数组/kv 对/HTTP headers，全部转义）
   ├─ 生成 /var/etc/frpc.toml（root 键 + [[proxies]] / [[visitors]] 数组）
   ├─ include 的外部文件加入 procd file watch（热更新）
   └─ procd: /usr/bin/frpc -c frpc.toml [--allow-unsafe=TokenSourceExec]，支持 run user/group、respawn、stdout/stderr 重定向
```

关键工程细节：所有字符串经 `_toml_escape`，端口/整数非法即报错中止；visitor 的 bind_port 允许负数；配置文件原子生成（先 mktemp 再落盘）。

### 3.2 界面结构（对标 frp v0.52–v0.71 全部能力）

- **状态条**：标准 `service.list` RPC 轮询，RUNNING / NOT RUNNING
- **全局配置 8 Tab**：Common Settings / Authentication / Transport / TLS·QUIC / Web Server / Advanced / Log / Startup
- **Proxy / Visitor 表格**：GridSection，可增删、可拖拽排序、弹窗编辑、8 个 Tab、字段按类型 `depends` 条件显隐

### 3.3 功能台账（相比 kuoruan 版新增）

- **认证**：token；OIDC（clientID/secret/audience/scope/tokenEndpoint/额外参数）；TokenSourceExec（外部命令取 token，含环境变量/参数/自定义 CA）
- **传输**：protocol（tcp/kcp/quic/websocket/wss）、dialServerTimeout/keepalive、poolCount、tcpMux、heartbeat、proxyURL、connectServerLocalIP、QUIC keepalive/idleTimeout/streams
- **TLS**：tls.enable、disableCustomTLSFirstByte、serverName、cert/key/ca/trustedCa
- **Web Server（frpc 自带管理面）**：addr/port/user/password、pprof、HTTPS（cert/key）
- **其他全局**：clientID、NAT 打洞 STUN、dnsServer、start（只启动指定代理）、udpPacketSize、includes 外部配置、meta、featureGates（VirtualNet 等实验特性）、日志级别/留天数/最大体积
- **代理类型**：tcp/udp/http/https/tcpmux/stcp/xtcp/sudp；remote_port 允许 0（随机）
- **代理传输**：bandwidthLimit + 限制端（client/server）、加密、压缩、proxyProtocol v1/v2
- **HTTP 域名**：customDomains（多选）、subdomain、locations、hostHeaderRewrite、请求/响应头设置
- **认证/路由**：httpUser/httpPwd、routeByHTTPUser、tcpmux 专用字段
- **健康检查**：类型/path/间隔/超时/最大失败/自定义请求头
- **负载均衡**：group/groupKey
- **插件（7 种）**：http_proxy、socks5、unix_domain_socket、static_file、https2http、http2https、tls2https + HTTP/2 开关
- **STCP/XTCP/SUDP 访问者（visitor）**：serverName、sk、bindAddr/bindPort、fallbackTo/fallbackTimeoutMs、disableNatHoleAssistedAddress、virtualNet visitor 参数
- **元数据**：每代理 metadata、annotations（显示在 frps dashboard）
- 每条 proxy/visitor 有 enabled 开关；原始配置片段逃生口（root-level raw fragments + includes）

---

## 四、两款主流插件的共同优点（设计模式总结）

1. **结构化表单即文档**：字段名/默认值/可选值/类型校验（端口、主机、整数）内置，用户不必记 TOML 键名，写错在提交前就被拦下。
2. **"数据库 → 渲染器 → 原生配置文件"解耦**：插件不碰 frpc 二进制、不 fork、不补丁，只做 OMV 侧集成（与本工作区红线一致）；frpc 升级不受插件制约。
3. **全局 / 规则两级模型**：服务器与日志等只配一次，隧道以"记录"为单位增删改。
4. **多服务器档案**（kuoruan）：一套界面管多个 frps，下拉切换。
5. **按代理类型条件显隐**：选 TCP 只显示 remote_port，选 HTTP 才显示域名/locations，stcp 才显示 sk/visitor 参数。
6. **单条启停**：每条规则独立 enabled/disabled，不必删除。
7. **运行状态可视化**：轮询 Running / Not Running + 版本号显示，不用 SSH 查进程。
8. **服务生命周期闭环**：开机自启、启动/停止/重启、崩溃 respawn、配置文件变更自动 reload。
9. **逃生舱设计**：extra_options / raw TOML / includes 保证上游新特性落地前插件不会卡死用户。
10. **i18n 本地化**：语言包随系统语言切换。
11. **二进制路径可配 + 有效性校验**（kuoruan）：适配不同安装方式（ipk/手动上传/Docker 外挂）。

### 它们的不足（我们的机会点）

- kuoruan 停留在 INI，frp v0.52 后的新能力（OIDC、QUIC、tcpmux、sudp、virtualNet…）完全不支持；
- 官方版字段 200+ 全平铺，新手认知负担大；
- 都**只能看到"进程在不在"，看不到每条代理是否真正在线**（frpc admin API `/api/status` 实际提供分代理 working/error/remoteAddr 信息，可做成状态列）；
- 没有**生成配置预览**、**接管现有 frpc.toml 的备份/导入**、**连通性测试**；
- 无独立日志查看页（只能去系统日志翻）。

---

## 五、向 OMV 8.x 插件的映射设计

### 5.1 架构对应关系

| OpenWrt | OMV 8.x 本插件 |
|---|---|
| UCI `/etc/config/frpc` | OMV 配置数据库 `/etc/openmediavault/config.xml`（confdb：`conf.service.frpc` 单例 + `conf.service.frpc.proxy` iterable） |
| init.d shell 渲染 INI/TOML | Salt state（`srv/salt/omv/deploy/frpc`）+ Jinja2 模板渲染 `frpc.toml`；PHP RPC 负责热 reload |
| procd 进程管理 | 后端模式 A：Docker CLI 控制现有 frpc 容器；模式 B：systemd unit（同 clouddrive2） |
| LuCI CBI/JS 页面 | Workbench 声明式 YAML（formPage + datatablePage + create/edit formPage，零 TypeScript） |
| XHR 状态轮询 | RPC get 计算字段（statusText/version）+ 代理状态列 |
| ipk + 中文 po | deb（Architecture: all，纯 PHP/YAML）+ zh_CN/zh_TW po，GitHub Actions 打 tag 出包 |

### 5.2 后端模式选型（需拍板）

- **模式 A：Docker 后端（推荐）**——符合项目红线"额外软件优先用 Docker，不装宿主机"，直接接管 NAS 上现存的 `fatedier/frpc:v0.71.0` 容器：
  - Salt 把数据库渲染为 frpc.toml（默认写回现有挂载路径 `…/data/appdata/frpc/frpc.toml`，路径可配），**首次写入前自动备份**为 `frpc.toml.omv-bak`；不碰 compose 栈 yml（红线）；
  - RPC 控制：`docker start/stop/restart <容器名>`、热加载 `docker exec <容器名> frpc reload -c /etc/frp/frpc.toml`（已验证可用）；状态 `docker inspect`；版本 `docker exec … frpc -v`；日志 `docker logs --tail`；
  - 插件纯解释型，**Architecture: all，CI 极简单，不绑架构**。
- **模式 B：原生后端**——build-deb.sh 像 clouddrive2 一样 CI 下载 frp 三架构 tarball 内置 /usr/bin/frpc，Salt 生成 /etc/frp/frpc.toml + systemd unit；与现存 Docker 实例二选一，用户需自行移除容器；违反"不装宿主机"约定。

### 5.3 数据模型草案

- 全局 `conf.service.frpc`：enable、backend（docker/native）、containerName、configPath（宿主渲染目标）、containerConfigPath（容器内 -c 路径）；serverAddr/Port/user/token；protocol、tlsEnable、tlsServerName、tcpMux、poolCount、heartbeatInterval/Timeout、dialTimeout、proxyURL、dnsServer、loginFailExit；logLevel/logDays/maxSize；webServer 开关/addr/port/user/password；meta（key=value 列表）；rawConfig（高级：原样 TOML 片段）。
- 代理 `conf.service.frpc.proxy`（iterable, uuid）：enable、name、**kind**（tcp/udp/http/https/tcpmux/stcp/xtcp/sudp 八种；stcp/xtcp/sudp 再用 role 区分服务端/访问者，或访问者独立成表）、localIP/localPort/remotePort、encryption/compression/bandwidthLimit、subdomain/customDomains/locations/hostHeaderRewrite/httpUser/httpPwd、sk/serverName/bindAddr/bindPort、group/groupKey、healthCheck 参数、extra（原始键值逃生舱）。
- 访问者可并入同表（官方版做法，role=visitor）以减少页面数。

### 5.4 页面草案（Services → FRP Client）

1. **设置页 formPage**：状态区（运行状态/版本/渲染目标）+ 分组（服务器与认证 / 传输与 TLS / 日志 / 高级逃生舱）；按钮：保存、应用（走标准 dirty/apply）、Start/Stop/Restart/**Reload（不重启热加载）**、**预览 frpc.toml**、打开 frpc 自带管理面（如启用）。
2. **代理页 datatablePage**：列 = 启用/名称/类型/本地→远程/域名/加密/压缩/状态；增（create 路由）/改（edit 路由，复用表单）/删（多选）；编辑表单字段按 kind 用 `modifiers: visible` + Constraint（in/eq）条件显隐。
3. **日志查看**：taskDialog 展示最近日志。
4. **中文适配**：全部界面字符串 zh_CN/zh_TW po。

### 5.5 v1 功能范围建议（推荐档：常用 90%）

- 代理全八种类型 + 访问者；token 认证；tcp/kcp/quic/websocket 传输 + TLS；加密/压缩/带宽/PP；域名三件套；stcp 四件套；group 负载均衡；健康检查；meta；热 reload；状态/版本/日志；TOML 预览；首次备份；extra 逃生舱。
- **延后项**：OIDC/TokenSourceExec、插件（http_proxy/socks5/static_file 等 frp plugin）、virtualNet、pprof、请求/响应头改写——均可先用 rawConfig/extra 覆盖，按反馈迭代。
- 版本号按规则：首版 `8.0.1`，Release tag `v8.0.1`。

---

## 六、参考来源

- kuoruan 插件源码：https://github.com/kuoruan/luci-app-frpc （init.d/frpc、model/cbi/frpc/*.lua、po）
- kuoruan 二进制打包：https://github.com/kuoruan/openwrt-frp
- OpenWrt 官方插件：https://github.com/openwrt/luci/tree/master/applications/luci-app-frpc （view/frpc.js 1531 行）
- OpenWrt 官方 frp 包（配置生成器）：https://github.com/openwrt/packages/tree/master/net/frp （files/frpc.init）
- frp 上游：https://github.com/fatedier/frp （最新 v0.71.0；配置参考 frpc-full-example.toml）
- SakuraFrp 启动器（体验参考）：https://doc.natfrp.com/launcher/usage.html
- Debian 包搜索确认无 frpc：https://packages.debian.org/trixie/
