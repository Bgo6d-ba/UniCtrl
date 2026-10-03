# Exploration report: removed drag-and-drop + current tree integration points
(`old:` = `git show 5365e34f0^:src/lib/<path>`; bare paths = HEAD; macOS host 26.6.2; "(verify)" = from memory, not proven)

## 1. Old design
- Server→client: LMB-down motion set `m_draggingStarted` (old:platform/OSXScreen.mm:1015). At the edge `Server::onMouseMovePrimary`
  refused to switch and spawned `sendDragInfoThread` (old:server/Server.cpp:1730-1774): read the drag pasteboard, faked Esc then
  LMB-up (OSXScreen.mm:1902-1922), sent DDRG `%2i%s` = count + "fullpath,size," (old:deskflow/DragInformation.cpp:80-91); the next
  mouse move did the real switch. Client: `ServerProxy::dragInfoReceived` (old:client/ServerProxy.cpp:835) → `fakeDraggingFiles`
  (OSXScreen.mm:1891).
- Data was PUSHED, never requested: on server mouse-up `sendFileToClient` (thread) → `StreamChunker::sendFile`
  (old:deskflow/StreamChunker.cpp:32-95) queued every 512 KiB chunk at once as DFTR `%1i%s` (mark 1 = decimal size, 2 = chunk,
  3 = end; old:deskflow/FileChunk.cpp:23-140). Receiver kept the whole file in a std::string (FileChunk.cpp:56), waited for the fake
  drag to end, wrote `<dropTarget>/<basename>` (old:client/Client.cpp:729; DropHelper.cpp:13-40). One file only.
- Client→server mirrored (OSXScreen::leave on the client sent DDRG + sendFileToServer, OSXScreen.mm:811-833;
  `Server::dragInfoReceived` Server.cpp:2274 started the server-side fake drag).
- Cancel: static `interruptFile()` on jump/enter (Server.cpp:1928, Client.cpp:245). No cancel message, no temp file, four raw Threads.
- Toggle: `--enable-drag-drop` (old:deskflow/ArgParser.cpp:186-206) let LMB through `Screen::isLockedToScreen`
  (old:deskflow/Screen.cpp:321-336).

## 2. macOS technique (old)
- Detect: `getDraggedFileURL()` reads NSDragPboard/NSFilenamesPboardType once at the edge (platform/OSXPasteboardPeeker.m:16-31;
  still built and included at OSXScreen.mm:32 but never called). No changeCount check ⇒ stale drag data = false positive; the `@"\0"`
  joiner is empty so several paths concatenate.
- Start (receiver): hidden 3x3 alpha-0.1 window + `OSXDragView` (old:platform/OSXDragSimulator.m:22-85): main-queue hop, window
  moved under the cursor (Y flip via [NSScreen mainScreen] — wrong on multi-display), activateIgnoringOtherApps, LMB-down posted at
  kCGHIDEventTap, `mouseDown:` calls deprecated `dragPromisedFilesOfTypes:` (old:OSXDragView.m:37-53).
- Destination: `namesOfPromisedFilesDroppedAtDestination:` stored `dropDestination.path` and returned nil (OSXDragView.m:55-61);
  `getCocoaDropTarget()` slept 1 s and returned a non-null empty string; `getDropTargetThread` (OSXScreen.mm:562-587) quit on its
  first pass.
- Source cancel: fake Esc down/up, then LMB-up, before the switch (OSXScreen.mm:1918-1920).

## 3. Why it broke
- Timing hacks (`e145fef76` Esc vs LMB-up ordering, `f6ecf9322` hacked lock, `45c1cde69` events before queue ready, the 1 s usleep);
  our own synthetic LMB-up re-entered our tap and sent files twice (`952be51c8`); synthetic events still untagged
  (OSXKeyState.cpp:666-669).
- Lifetime/API: `39a2ecbd8` own NSApplication, `db9f7974e`/`95ee948f2`/`c92ac1a71` crashes/leaks, unsynchronised statics, deprecated
  APIs, weaker app activation since macOS 14 (verify).
- Protocol: comma-delimited names, sender paths leaked, no id/request/ack/cancel, unpaced messages on the mouse stream, whole file
  in RAM; `FileKeepAlive` addressed to Server (old:Server.cpp:2266) but handled on ClientProxy1_5 (old:ClientProxy1_5.cpp:29).
- Disabled by default in 2022 (`a08149aae`), removed by `5365e34f0`.

## 4. Current tree integration points
- Crossing: Server.cpp:464-535 switchScreen (leave 488, enter 524). Lock check Server.cpp:886 → Screen.cpp:320-328: ANY held button
  locks the screen ⇒ crossing with LMB down is impossible today. Server.cpp:1539-1555 relays button up/down. Screen.cpp:421-429
  releases pressed buttons on leave (would drop at the client edge). Client side: Client.cpp:222-250, ServerProxy.cpp:464-495, 589-650.
- OSXScreen.mm: button poll 292-300; drag events chosen from `m_buttonState` (447-453); `fakeMouseButton` uses macOS 27 event numbers
  (64-68, 457-459, 545-558); enter resets buttons (781); leave freezes the cursor (809); button/motion forwarding 975-1035; the tap
  swallows everything while off-screen (1624-1628); the secondary tap is a no-op (1547). Dead code: OSXCocoaApp.m, updateButtons
  (1281), OSXScreen.h:307-308. The core is a QGuiApplication (deskflow-core.cpp:30-32) with server/client on `coreThread`
  (App.cpp:62-71), so AppKit calls need a main-queue hop.
- Protocol leftovers: ProtocolTypes.h:1036 DFTR and :1067 DDRG; no-op stubs ClientProxy1_5.cpp:26-57 (payload left unread),
  ClientProxy1_0.cpp:308-318, BaseClientProxy.h:79-80, ClientProxy.h:75-76, PrimaryClient.cpp:187-195; tests ProtocolUtilTests.cpp:223-224.
- Adding a message: formats checked at compile time (ProtocolUtil.h:133-141, 196-234); ints max 4 bytes (:115); strings max 1 MiB
  (ProtocolUtil.cpp:157); packets max 4 MiB (ProtocolTypes.h:120); bump minor (:48, now 8); add ClientProxy1_9/ServerProxy1_9 like
  ServerProxy1_7.cpp; add cases in ClientProxyUnknown.cpp:146-195 and Client.cpp:474-492; document in protocol_reference.md:155.
- Pacing: StreamChunker.cpp:14-53 enqueues all 512 KiB clipboard chunks at once; ClipboardChunk.cpp:78-172 is a reusable
  start/chunk/end state machine with caps; TCPSocket::write always appends (TCPSocket.cpp:150-195), 64 MiB cap then a 10 s stall drop
  (TCPSocket.h:35-37), flushed event fires only on full drain (:447-455); no back-pressure API; mouse shares the stream; the client
  alarm resets only on CALV (ServerProxy.cpp:253-256).
- Toggles: Settings.h:123-144 + key/default lists (:310-425) → ServerApp.cpp:60-112 → ServerOptions.h:54-55 → Config.cpp:518-519 →
  DSOP → Client::setOptions (Client.cpp:326-347).

## 5. Layering (architecture.md:17-34, 40-55; CheckLayering.cmake:166-223)
- `domain` (src/lib/deskflow, needs only `io`): message constants, transfer engine, name sanitiser, an `IFileDrag`-style interface
  reached through default no-op virtuals on IPlatformScreen/Screen (pattern IPlatformScreen.cpp:8-16).
- `server`/`client` (`domain` + `net`): may not include each other or `platform`; hold the 1.9 proxies and the Server/Client hooks.
- `platform` (`domain` only): OSXFileDrag.mm. `app`: options wiring. Every new file must be in the matching CMake target or
  LayeringCheck fails. Use Qt/std threads, not legacy Thread.

## 6. Recommended design / top risks
(See filedrag-spec.md — same recommendation: NSFilePromiseProvider drag source on the receiving side, request-driven transfer with
credit windows, host-drag cancellation with tagged Esc + LMB-up at crossing, safe naming and limits.)
Risks: 1) cancelling the host drag (stale pasteboard false positives, Esc/up ordering, self-tap re-entry): a failure drops files
locally or wrecks a real drag. 2) the synthetic session on the receiver (activation, Spaces/fullscreen, multi-display Y, macOS 27
event numbers, ordering across threads). 3) privacy/TCC prompts for reading dragged files and writing promised destinations, pasteboard
prompts on macOS 26/27 (verify). 4) promise lifecycle: writePromiseToURL may be late/duplicated/absent; the handler must always
complete or Finder hangs; Photos/Mail promises are not file URLs; folders and xattrs unhandled. 5) shared-stream latency and hostile
peers.
