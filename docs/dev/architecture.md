# Architecture

Deskflow ships three executables built from the static libraries in `src/lib`:

| Executable | Source | Role |
|---|---|---|
| `deskflow` (GUI) | `src/apps/unictrl-gui` | Qt Widgets configuration UI; starts and monitors the core. |
| `unictrl-core` | `src/apps/unictrl-core` | Headless server or client that shares the keyboard and mouse. |
| `unictrl-daemon` | `src/apps/unictrl-daemon` | Windows service that runs the core in the user's session. |

## Library layering

Each row is a component: a CMake target and the source files that belong to it. A component may
include headers from itself and from the components it depends on, directly or transitively; its
CMake target links exactly the components in the "Depends on" column. The graph has no cycles.

<!-- layering:components:begin -->
| Layer | Component (CMake target) | Sources (path prefixes under src/) | Depends on | Contents |
|---|---|---|---|---|
| 0 | `common` | `lib/common/` | | Settings, constants, enums, IPC helpers, the status sink. Qt Core (+ DBus on Linux) only. |
| 1 | `arch` | `lib/arch/` | `common` | OS abstraction: sockets, threads, sleep, daemon/service helpers. |
| 2 | `base` | `lib/base/` `lib/mt/` | `arch` | Logging, strings, the event queue; legacy `Thread`/`Mutex`/`CondVar`. |
| 3 | `io` | `lib/io/` | `base` | Streams. |
| 4 | `net` | `lib/net/` | `io` | TCP and TLS sockets, fingerprints, certificate generation. |
| 4 | `domain` | `lib/deskflow/` | `io` | Protocol, key maps and key state, clipboard, `Screen`, `IPlatformScreen`, `AppUtil`. |
| 5 | `server` | `lib/server/` | `domain` `net` | Server state machine and client proxies. |
| 5 | `client` | `lib/client/` | `domain` `net` | Client state machine and server proxies. |
| 5 | `platform` | `lib/platform/` | `domain` | `IPlatformScreen` per OS: X11, libei/portal, macOS, Windows; the X11 keyboard-layout lookup (`XkbLayoutsParser`). |
| 6 | `app` | `lib/deskflow/App.` `lib/deskflow/ServerApp.` `lib/deskflow/ClientApp.` `lib/deskflow/ipc/` `lib/deskflow/unix/AppUtilUnix.` `lib/deskflow/win32/AppUtilWindows.` | `server` `client` `platform` | Process wiring: create a platform screen, run the server or client, IPC servers. |
| 6 | `gui` | `lib/gui/` | `net` | Qt Widgets UI, IPC clients. |
| 7 | `unictrl-core` | `apps/unictrl-core/` | `app` | Executable. |
| 7 | `unictrl-daemon` | `apps/unictrl-daemon/` | `app` | Executable (Windows). |
| 7 | `unictrl-gui` | `apps/unictrl-gui/` | `gui` | Executable. |
<!-- layering:components:end -->

`src/lib/deskflow` builds two targets: `app` takes the files listed in its row (the longest
matching prefix wins) and `domain` takes the rest. `src/lib/mt` has no target of its own; its files
are built into `base` because the event queue locks with `mt` types and `mt` logs through `base`.

Rules:

- The domain talks to the OS only through interfaces such as `IPlatformScreen`, `IEventQueue` and
  `IArchMultithread`. It must not include `platform/`, `server/` or `client/` headers, spawn
  processes, or call into the GUI. Side effects that belong to the application are injected; for
  example `Screen::setEnterLeaveHandler` lets `ServerApp`/`ClientApp` run the user's screen
  enter/exit commands (`App::runScreenCommand`).
- `server`, `client` and `platform` are siblings and do not include each other.
- Status for the GUI (`connectionState`, `peerFingerprint`, ...) is reported through
  `deskflow::reportStatus` (`common/StatusSink.h`). The lower layers never see IPC: the core's
  `CoreIpcServer` installs itself as the `IStatusSink` and forwards each message to the GUI.
  Without a sink, as in unit tests, reports are dropped; tests install a fake sink to assert them.
- `gui` must not link `platform`, `server`, `client` or `app`; the core is reached over IPC. TLS
  certificate policy and the trusted-fingerprint databases live in `gui/TlsUtility`, not in
  `MainWindow`.
- Executables only compose libraries; they contain no logic beyond argument parsing and startup.

### Enforcement

`cmake/CheckLayering.cmake` reads the tables in this section and runs as the `LayeringCheck` ctest
test (registered in `src/unittests`, so it runs locally and in CI with the unit tests). It fails when
a source file under `src/lib` or `src/apps` is in no component, when a file includes
(`#include`/`#import`, quotes or angle brackets) a header of a component it may not use, when the
graph has a cycle, when a component has no CMake target (only `unictrl-daemon` may be missing,
off Windows), when a target's links to other components differ from "Depends on", when a target
builds a source file that the table gives to another component, or when an exception below is no
longer needed. A quoted include is resolved against the including file's directory first
(`"ServerApp.h"`, `"ipc/CoreIpcServer.h"`, `"../server/Server.h"`), then against `src/lib`.
Only direct includes are checked: a header that is allowed may itself include headers of a higher
component, which the exceptions below rely on. Run it alone with
`ctest --test-dir build/src/unittests -R LayeringCheck --output-on-failure`.

To add a dependency, change the table and the target's `target_link_libraries` together. Adding an
exception is a last resort; remove exceptions rather than extend them.

### Remaining exceptions

These includes cross the layering and are allowed file by file.

<!-- layering:exceptions:begin -->
| File (under src/) | Includes | Why |
|---|---|---|
| `lib/arch/win32/ArchDaemonWindows.cpp` | `base/Log.h` | Windows `arch` code logs through `base`, which links `arch`. Log through `IArchLog` instead. |
| `lib/arch/win32/ArchMiscWindows.cpp` | `base/Log.h` `base/String.h` | As above. |
| `lib/arch/win32/ArchNetworkWinsock.cpp` | `base/Log.h` | As above. |
| `lib/platform/MSWindowsScreen.cpp` | `deskflow/App.h` | Calls `AppUtilWindows::instance()` (`app`) for the current keyboard layout, so `platform` needs a symbol from `app`, which comes before it on the link line. MSVC's linker resolves that; GNU ld (MinGW) would not. Move that query into `platform`. |
| `lib/platform/MSWindowsDesks.cpp` | `deskflow/win32/AppUtilWindows.h` | As above. |
<!-- layering:exceptions:end -->

## Legacy threading versus Qt

The core started as a Qt-free C++ code base and is migrating to Qt and the standard library.

| Concern | Legacy (`arch` to `platform`) | Qt / std |
|---|---|---|
| Threads | `Thread`, `IArchMultithread` | `QThread`, `std::thread` |
| Locks | `Mutex`, `Lock`, `CondVar` | `QMutex`, `std::mutex`, `std::condition_variable` |
| Events | `IEventQueue` / `EventQueue`, `EventTypes` | signals and slots, `QMetaObject::invokeMethod` |
| Logging | `LOG_*` macros (`CLOG`) | `qDebug()` etc., routed into `CLOG` by `unictrl-core` |

Rule for new code: use Qt or the standard library. Extend the legacy types only when changing code
that already uses them, and keep each class on one side; do not mix a `Mutex` and a `QMutex` in
the same class. Code in `common` and `gui` is Qt only.

Threading in `unictrl-core`: the Qt main thread runs the Qt event loop and the core IPC server.
`App::run` moves the `App` to a dedicated `QThread` that runs the legacy `EventQueue` and the
server or client. Platform code may add its own threads (for example the glib main loop for
portals, or the macOS event tap). `Settings` is shared by all of them and serialises access
internally; objects that own a `QLocalSocket` must be used from their own thread, which is why
`CoreIpcServer::sendStatus` queues onto the IPC server's thread.

`Settings` is read by the app layer and passed down, not read by the libraries below it. The app
builds a plain options struct from `Settings` and hands it to the objects that need it; tests and
fuzzers fill the struct directly instead of writing a settings file. The server library follows
this: `ServerApp` builds a `server::ServerOptions` (known screens and their per-screen settings,
global server options, listen address, protocol) and passes it to `Config`, and `Server` reads the
protocol from its `Config`. New code in `server`, `client`, `platform`, `net` and `domain` takes its
settings the same way; the remaining `Settings::` calls in those layers are to be moved over.

## Processes and IPC

```
             QProcess (desktop mode)
  GUI  ─────────────────────────────────────▶  unictrl-core
   │   ◀──── core IPC (QLocalSocket) ─────────┘
   │
   └──── daemon IPC (named pipe, Windows service mode) ──▶  unictrl-daemon ──▶ unictrl-core
```

- **Transport.** All IPC uses `QLocalServer`/`QLocalSocket` with newline-terminated UTF-8 text
  messages of the form `command` or `command=args`. `deskflow::ipc::splitMessage` splits on the
  first `=` only. Servers derive from `deskflow::core::ipc::IpcServer` (`src/lib/deskflow/ipc`),
  clients from `deskflow::gui::ipc::IpcClient` (`src/lib/gui/ipc`).
- **Handshake.** On connect a client sends `hello=<version>+<git sha>`; the server answers
  `hello=<same id>` (then replays state messages queued while no client was connected) or
  `versionMismatch=<its id>`, which the client treats as fatal. The server enforces it: until a
  matching hello it answers every command but `noop` with `error=hello required`, it broadcasts
  only to clients that completed the handshake (`bye` goes to all), and after a mismatch it
  disconnects the client (the core still accepts `stop`, so a GUI of another version can replace it). `noop` answers
  `ok`; `bye` is broadcast when a server stops. Messages containing a line break are never sent.
- **Core IPC** (`CoreIpcServer`, name from `deskflow::ipc::coreServerName()`). The GUI launches
  the core with `QProcess` in desktop mode, then connects to learn its state. The core pushes
  `connectionState`, `connectedClients`, `connectionRefused`, `peerFingerprint`,
  `unrecognisedClient`, `secureSocket`, `retryIn` and `missingKeyboardLayouts` (reported through
  `deskflow::reportStatus`, see the layering rules); the GUI may send `stop`. On Unix the socket lives in a
  directory only the current user can access. On Windows the pipe is open to all users only when
  the core runs as SYSTEM, a client must be an elevated administrator or in the core's session, and
  the GUI refuses a pipe server in another session or owned by anyone but its own user or SYSTEM.
- **Daemon IPC** (`DaemonIpcServer`, `kDaemonIpcName`, Windows service mode only). The GUI sends
  `logLevel`, `configFile`, `start`, `stop`, `logPath` and `clearSettings`. The daemon starts the
  core in the active user session; `logPath` returns the daemon's log file, which the GUI tails. Its
  security decisions (who may connect or send commands, which config paths are accepted, owner
  binding, boot re-apply, elevation) are in `deskflow::core::ipc::policy` (`DaemonPolicy.h`) and
  unit tested; the Windows code only gathers the caller's facts. A non-administrator client must
  have a known user SID, and its config file is tied to that user. Only an elevated administrator
  (Administrators enabled in its own token, so not a UAC-filtered admin) can have the core elevated;
  otherwise the core starts unelevated and the GUI is told why (`elevationRefused=<reason>`).
- **Configuration** is not sent over IPC. The GUI writes the settings file and the core reads it:
  in desktop mode the core finds the same default file, in service mode the GUI sends its path
  with `configFile` and the daemon passes it to `unictrl-core <server|client> --settings <file>`.

## Host and join

The main window stacks two views, switched and saved with `Gui::AdvancedView`: Host / Join
(`gui/widgets/HostJoinWidget`, the default) and the server and client controls. `HostJoinWidget` is
a pure view: the `gui/lan` controller sets its state and receives its requests, and asks
`MainWindow` to start, restart or stop the core. Showing the view that was saved writes nothing; settings are only
written for what the user did. Nothing runs in the background until the user chooses Host or Join.

- **Discovery** (`gui/lan`): a host broadcasts a small JSON beacon (id, display name, ports) on UDP
  24802 every 2 s, and the joining computer lists the hosts heard in the last 7 s. Beacons are not
  authenticated; pairing is. The browser runs only while the Nearby hosts page is on screen:
  `HostJoinWidget::browsingChanged(bool)` is true while that page is visible and false on every other page and
  while the widget is hidden (Advanced shown, window hidden), and the controller listens for hosts accordingly.
  No socket is opened when the application starts. Only peers on the same IPv4 subnet as one of this computer's
  interfaces (or loopback) are heard.
- **Pairing** (`gui/lan`, TCP 24803): SPAKE2 (RFC 9382, P-256, `net/Spake2`) on the host's six-digit
  code or password, so the code is never sent and each attempt tests one guess. The transcript binds both
  identities (display name, screen name, TLS certificate fingerprint). Afterwards the host adds the client's
  fingerprint to its trusted clients and the client adds the host's to its trusted servers, so the core's TLS
  connection is pinned both ways without fingerprint prompts. Hosting therefore forces `Security::TlsEnabled` and
  `Security::CheckPeers`. Trust is permanent; there is no revoke UI, so the host stops pairing after
  `kMaxPairingsPerSession` (16) computers until the user asks for a new code, and the docs say how to edit
  `tls/trusted-clients` and `tls/trusted-servers`.
- **Wrong guesses**: per remote address 5 wrong codes in 60 s refuse that address for 60 s. All addresses together
  15 wrong codes in 60 s pause pairing for 60 s and report it (`PairingServer::lockedOut`). The controller then
  replaces a code it made (a fresh random code, counters reset, the host is told) and leaves a custom password,
  which only the pause protects. A new code or password resets the counters. A custom password has at least
  `HostJoinWidget::MinCustomPasswordLength` (8) characters, typed twice in the dialog and checked again by the
  controller, and is stored in plain text (`lan/password`).
- **Controller** (`gui/lan/LanSession`): owns the announcer, browser, pairing server and pairing
  client and holds no widgets. `HostJoinWidget` requests arrive at its slots; it answers with state
  signals and asks `MainWindow` to start or stop the core and to add a joined computer's
  screen to the layout, which `MainWindow` does with the same code paths as the advanced view (the
  screen is saved as the server config dialog saves it, and the core restarts only when the layout
  changed). It is told the core's process state, connection state and connected clients, and the identity, port and
  security settings are re-applied while hosting when they change. The certificate is only created when it is
  missing (`TlsUtility::ensureCertificate(false)`): replacing one would end every pairing that pinned it, so the
  user is told when a new one had to be made.
- **Following the core.** Hosting or joining ends when the core stops or leaves its mode; a stop that
  is followed by a start within a moment is a restart and does not end it.
- **What is adopted.** `Lan::HostingEnabled` is set by Host and cleared by Stop hosting and whenever hosting ends.
  In the simple view a server that runs (auto-start, the tray menu, switching from the advanced view) is shown as
  hosting again only while that flag is set and `Security::TlsEnabled` and `Security::CheckPeers` are already on;
  it is then adopted as it is, without changing a setting or restarting it. Any other running server is *legacy*: it
  is left alone, the flag is cleared, and the Host / Join page says that a manual session runs and offers the
  Advanced view (`LanSession::legacySessionChanged`, `HostJoinWidget::setLegacySessionNotice`). A client that runs
  is shown as joined to the host whose paired address is `Client::RemoteHost`, or else to that address, for display
  only. Switching to the advanced view keeps hosting and joining as they are.
- **Joining.** The client is pointed at the host as `Client::RemoteHost` = `address:port`, which the core parses
  itself, and `Core::Port` is not changed: it is the port of the server this computer may host. Joining again
  without a code uses the address and ports the host announces now, and fails with "Host not found on the network"
  when it is not listed. A join or rejoin the user started gives up after `kJoinWatchdogMs` (20 s) without a
  connection: the client is stopped and the user told, and after a rejoin the paired entry is dropped so that the
  list asks for the code again. A client that only shows as joined has no watchdog.
- **Certificate prompts.** `MainWindow::handlePeerFingerprint` does not ask the user to trust an unknown
  certificate while `LanSession` hosts or joins (`LanSession::rejectUntrustedPeer`): a host logs and ignores the
  peer, and a join the user started fails, drops the paired entry and asks to join again with the code, because a
  prompt is a click-through around the code.
- **Screen layout.** A computer that joined is added to the layout as the advanced view adds a new client. While the
  Arrange screens dialog is open (`MainWindow::execServerConfigDialog`) it is queued in
  `gui/lan/ClientScreenQueue`, since the dialog replaces the whole layout when it is accepted, and added when the
  dialog closes unless the layout has it by then; the core restarts once for everything that changed.
