<p align="center">
  <img alt="UniCtrl" src="../src/apps/res/icons/deskflow-dark/apps/64/io.github.batmunkh0612.unictrl.svg" width="96">
</p>

<h1 align="center">UniCtrl</h1>

<p align="center">
  Share one keyboard and mouse across your computers. A software KVM, without the video.
</p>

UniCtrl is a fork of [Deskflow](https://github.com/deskflow/deskflow) with a reworked connection flow,
a new interface and a tighter default security posture. Move the cursor off the edge of one screen and it
appears on the next computer. Clipboard sharing and TLS encryption are on by default.

## What is different from Deskflow

- **Host / Join** – two buttons instead of manual server and client setup. One computer hosts and shows a
  pairing code, the other joins with it. Pairing uses SPAKE2, so the code is never sent over the network.
- **LAN discovery** – computers that are hosting are found automatically on the local network.
- **New interface** – dark emerald theme, new icon set and a simple view next to the classic advanced view.
- **Hardening** – safer daemon policy, trusted-path checks and network fixes, with tests.
- **No phoning home** – the upstream update check and its links were removed.
- **Renamed** – the app is `UniCtrl`, the headless process is `unictrl-core`.

The advanced view still exposes the full Deskflow server configuration (screen layout, hotkeys, options).
Network compatibility with Deskflow, Synergy 1, Barrier and Input Leap is kept.

## Build

You need CMake 3.24+, Qt 6.7+ and OpenSSL 3.0+. On Linux you also need `libei` and `libportal`
(fetched automatically when missing).

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j8
```

On macOS this produces `build-release/bin/UniCtrl.app`. To make a self-contained, ad-hoc signed bundle and DMG:

```bash
bash docs/dev/tools/make-dmg.sh
```

More options are listed in [docs/dev/build.md](../docs/dev/build.md).

> [!NOTE]
> The macOS build is not notarized. After copying it to Applications run `xattr -c /Applications/UniCtrl.app`,
> then allow **Accessibility** and **Local Network** access for `UniCtrl` and `unictrl-core`
> in Privacy & Security.

## Quick start

1. Install and open UniCtrl on both computers, on the same network.
2. On the computer whose keyboard and mouse you want to share, press **Host**. A pairing code appears.
3. On the other computer press **Join**, pick the host and enter the code.
4. Arrange the screens with **Arrange** on the host, then move the mouse over the edge.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/apps/deskflow-gui` | the GUI application |
| `src/apps/unictrl-core` | headless server and client |
| `src/lib/gui` | windows, theme, host/join widgets, LAN session |
| `src/lib/server`, `src/lib/client` | input sharing logic |
| `docs/dev` | architecture, build notes and design documents |

Some directories and internal identifiers still carry the upstream `deskflow` name to keep merges
with upstream manageable.

## Contributing

Issues and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first.

## Credits and license

UniCtrl is built on [Deskflow](https://github.com/deskflow/deskflow), which grew out of Synergy
(created by Chris Schoeneman in 2001) and carries the work of the Deskflow, Synergy and Barrier contributors.
Thank you to all of them.

Licensed under [GPL-2.0](../LICENSE) with an [OpenSSL exception](../LICENSES/LicenseRef-OpenSSL-Exception.txt).
Original copyright and license headers are kept in the source files.
