# omv-frpc

An [OpenMediaVault](https://www.openmediavault.org/) 8.x plugin that manages
[frp](https://github.com/fatedier/frp) client (`frpc`) reverse-proxy tunnels from the
OMV web interface, modeled after OpenWrt's `luci-app-frpc`.

Debian package name: `openmediavault-frpc`

Versioning: the plugin's major version follows the OpenMediaVault major version
(OMV 8.x → plugin `8.x.y`).

The Chinese design study this plugin was built from is kept in
`OpenWrt-frpc插件调研分析报告.md`.

## Features

Two pages under **Services → FRP Client**: **Settings** and **Proxies**.

### FRP Client → Settings

**Status** (read-only)

- Service status
- `frpc` version
- Path of the generated configuration file

**Settings**

- **Enabled** – start at boot (native service or Docker restart policy)
- **Backend** – where frpc actually runs:
  - `Native systemd service` (default) – uses the `frpc` binary bundled in this package plus a
    generated `frpc.service` unit.
  - `Docker container` – manages an **already existing** frpc container
    through the Docker CLI. The plugin never creates or removes the container.
  - Only one backend should be running at a time; switching backends clears the other
    side's unit so frpc cannot run twice.
- Docker backend: container name, the **host path** of `frpc.toml` (must be
  bind-mounted into the container) and the container-side path (used for `-c` and for
  hot reload)
- **Server**: address, port, optional proxy-name prefix (`userPrefix`)
- **Authentication**: shared token
- **Transport**: `tcp` / `kcp` / `quic` / `websocket` / `wss`, TLS with optional SNI
  server name, TCP multiplexing, connection pool, heartbeat interval/timeout, dial
  timeout, HTTP/SOCKS5 proxy URL, custom DNS server, exit-on-login-failure
- **Log**: level, retention in days
- **Management**: frpc admin web server (address / port / user / password) — required
  for hot reload and for the live per-proxy status column
- **Advanced**: client metadata (`key=value`), include files, and a raw TOML escape
  hatch appended after the generated tables

### FRP Client → Proxies

- List columns: enabled, name, type, role, local IP, local port, remote port,
  subdomain, custom domains, encryption, compression and **live status** (read from the
  frpc admin API, refreshed every 10 s)
- Actions: create, edit, delete
- Types: `tcp`, `udp`, `http`, `https`, `tcpmux`, `stcp`, `xtcp`, `sudp`
- Role `server` exposes a local service; role `visitor` (stcp / xtcp / sudp only)
  connects to a peer's secret proxy and binds it locally
- Per-entry options: local IP/port, remote port, secret key, HTTP/HTTPS domains
  (subdomain, custom domains, locations, host header rewrite, basic auth),
  encryption, compression, bandwidth limit with client/server enforcement, PROXY
  protocol v1/v2, load-balancer group and group key, TCP/HTTP health checks (path,
  interval, timeout, failures before offline) and an extra-TOML escape hatch

Settings page buttons: `Save` / `Apply`, `Start`, `Stop`, `Restart`, `Reload` (hot
reload through the admin web server), `View logs`, `View generated config`,
`Open admin UI`.

Internationalization: Simplified (`zh_CN`) and Traditional (`zh_TW`) Chinese catalogs
are shipped. When the system language is Chinese, the navigation entry
(**Services → FRP Client**, **Proxies**) and every web interface label are translated.
Add more languages by dropping a `<locale>/openmediavault-frpc.po` file under
`usr/share/openmediavault/locale/`.

Access control: the navigation entries and every RPC method require the `admin` role.

## Architecture

- Configuration is stored in the OMV database as `conf.service.frpc` (single instance)
  plus `conf.service.frpc.proxy` (iterable proxy/visitor entries); the RPC service name
  is `Frpc`.
- `frpc.toml` is rendered by the OMV Salt state (`/srv/salt/omv/deploy/frpc/`) — never
  edited by hand.
- All service control goes through `/usr/sbin/omv-frpc-ctl`, which is backend
  agnostic.
- Native backend: `frpc.service` runs `/usr/bin/frpc -c /etc/frp/frpc.toml`.
- The `frpc` binary is the official upstream release, downloaded at build time and
  staged under `usr/bin/`; it is not committed to this repository.

## Installation

The default backend is the native systemd service using the `frpc` binary
bundled in this package; no container is required. The Docker backend
manages an already existing frpc container instead.

1. Download `openmediavault-frpc_<version>_<arch>.deb` from the latest
   [GitHub Release](https://github.com/fffeng99999/omv-frpc/releases)
   (or build it yourself, see below).
2. Install it via CLI on the NAS:

   ```bash
   apt install ./openmediavault-frpc_<version>_<arch>.deb
   ```

3. Open **Services → FRP Client → Settings** and fill in:
   - *Backend* – `Native systemd service` (default), or `Docker container`
     (then also set the *Docker container name*, the *Configuration file
     (host path)* bind-mounted into the container, and the *Configuration
     file (container path)*)
   - *Server address*, *Server port* and *Token*
   - tick **Enabled**, then click **Save** and **Apply**

   Enabling the **admin web server** is recommended: without it there is no hot reload
   and no live per-proxy status.
4. Add your tunnels under **Proxies**.

Uninstall:

```bash
apt purge openmediavault-frpc
```

## Build

Built in the cloud with GitHub Actions
([.github/workflows/build.yml](.github/workflows/build.yml)):

- Push a tag `v*` → build for `amd64`, `arm64` and `armhf`, then create a GitHub
  **Release** with the `.deb` files attached (the tag version must match
  `debian/changelog`).
- `workflow_dispatch` → manual build with an optional explicit upstream frp tag.

Local build (on any Debian-based system, e.g. a container):

```bash
sh ./render-vars.sh                 # render the fffeng99999 placeholders
./build-deb.sh amd64                # download and stage the upstream frpc binary
dpkg-buildpackage -us -uc -b -aamd64 -d
```

## Development notes

- The plugin is pure declarative: a PHP RPC service plus workbench YAML — there is no
  JavaScript/TypeScript build step.
- The author name in `debian/*` and in the source headers is a `fffeng99999`
  placeholder. `render-vars.sh` renders it from `debian/variables.env`; CI runs the
  script before `dpkg-buildpackage` and refuses to publish a package that still
  contains a placeholder. **Run it before any local build.**
- Cross builds skip `dh_strip` / `dh_dwz` / `dh_shlibdeps` / `dh_makeshlibs` for the
  prebuilt static binary.
- CI inspects the bundled binary with `file` for every architecture, but only the
  `amd64` build is actually executed (`frpc -v`): the other architectures fail on the
  x86 runner with `Exec format error`.
- `.gitattributes` forces LF: a CRLF `postinst` breaks package installation.
- Quick debugging without packaging: copy the files to the matching paths on a test
  VM/container, run `omv-mkworkbench all` and restart `omv-engined`.
- **Never install untested builds on a production NAS** — use a VM or container first.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
