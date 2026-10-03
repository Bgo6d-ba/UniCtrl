# File drag & drop between computers — design spec (deskflow-plus)

User goal (macOS↔macOS first): drag file(s) in Finder on one computer, keep the button held, cross to the other
computer's screen and release. The file(s) must land WHERE THE USER RELEASES (the Finder folder/window/desktop
under the cursor), not in a fixed download folder. Server→client first, client→server afterwards. Windows/Linux later
(protocol and engine must be platform-neutral; only the "drag bridge" is per-OS).

Upstream Deskflow removed this feature in commit 5365e34f0 ("its broken on all platforms"). Old code:
`git show 5365e34f0` / `git show 5365e34f0^:<path>`. We rebuild it with a different design (below).

## What was wrong with the old design (avoid all of it)
- Sender PUSHED whole files unpaced on the shared input stream, receiver buffered the whole file in RAM, one file only.
- No ids, no request/ack, no cancel; comma-delimited names/paths (broke on commas); sender paths leaked over the wire.
- macOS: deprecated `dragPromisedFilesOfTypes:`, 1 s usleep hacks, wrong Y-flip on multi-display, own NSApplication,
  synthetic events re-entered our own event tap (files sent twice), stale drag-pasteboard false positives, no cleanup.

## New design

### Protocol 1.9 (bump kProtocolMinorVersion 8→9; ClientProxy1_9 / ServerProxy1_9 derive from the 1_8 classes)
Negotiation is by version, so 1.8 peers (official Deskflow) never see the new messages. New 4-char codes (check for
collisions first): DFOF offer, DFRQ request(+credit), DFDT data, DFEN file end/status, DFCN cancel. Remove the dead
DFTR/DDRG constants/stubs (or leave them documented as obsolete) — decide when implementing, keep tests green.
- DFOF (source→target, also target→source later): `id u32`, `count u32`, `entries` = binary-safe string blob:
  repeated `[u16 nameLen BE][name UTF-8 bytes][u64 size BE]`. NO paths, only sanitised basenames.
  Limits: ≤ 64 files per offer, name ≤ 255 bytes, blob ≤ 32 KiB, total ≤ kMaxDragTotal.
- DFRQ (target→source): `id u32, index u32, credit u32` — the first DFRQ for (id,index) starts streaming that file,
  later ones grant more credit (bytes, ≤ 4 MiB per message). Source never sends more than granted.
- DFDT (source→target): `id u32, index u32, data` (≤ 64 KiB chunk, binary-safe string). Offset = running byte count.
- DFEN (source→target): `id u32, index u32, status u32` — 0 ok (all bytes sent), 1 read error, 2 file changed,
  3 cancelled, 4 refused (limits/unknown id).
- DFCN (either): `id u32` — abort the whole offer; both sides release resources at once.
Everything validated (ids known, indexes in range, sizes consistent, credit accounting); malformed → ignore + log, or
BadClientException per existing patterns. Ints max 4 bytes in ProtocolUtil (use two %4i for 64-bit if needed instead
of the blob), strings ≤ 1 MiB, packets ≤ 4 MiB. Document in docs/dev/protocol_reference.md.

### Transfer engine (platform-neutral; lives in `domain` = src/lib/deskflow/, see docs/dev/architecture.md)
- `FileOffer` (id, entries), `FileSender` (owns an offer: opens files at OFFER time with O_NOFOLLOW|O_CLOEXEC, fstat →
  regular files only (v1 skips folders/aliases/packages with a notice), records size + (dev, ino, mtime) to detect a
  changed file, serves ONLY ids it created; worker thread reads 64 KiB chunks with pread when credit > 0; cancellation;
  inactivity timeout 30 s; offer lifetime 60 s if never requested), `FileReceiver` (requests files, credit window
  ~256 KiB re-granted as data is written, writes `<dest>.part` (O_EXCL) then verifies the exact size and renames to the
  final path, fails cleanly on cancel/disconnect/size mismatch/ENOSPC, deletes partial files), name sanitiser (NFC,
  reject separators/NUL/control chars, `.`/`..`, leading dot; ≤ 255 bytes), limits (settings-backed: max files, per-file
  and total size, free-space check). File IO on std::thread/QThread workers; results posted with IEventQueue::addEvent
  (thread-safe). Uses Qt/std types, not the legacy Thread/Mutex (architecture.md).
- Flow control: input events share the stream. The credit window (~256 KiB) keeps queued file data small so mouse/key
  latency is unaffected; never enqueue a whole file. Optionally also check TCPSocket output backlog.

### Crossing behaviour (server side, macOS host first)
Today Screen::isLockedToScreen (Screen.cpp ~320-328) forbids leaving the screen while ANY mouse button is held, and
Screen.cpp ~421-429 releases pressed buttons on leave. Both must change for an active file drag:
1. Source detection (platform bridge): LMB down → remember NSPasteboardNameDrag changeCount; on first drag event, a
   changed count AND public.file-url items ⇒ "file drag active". Poll outside the event-tap callback.
2. At the edge with an active file drag (feature enabled, TLS session, target client speaks 1.9): the lock is lifted;
   snapshot the file list, build the offer (open fds), CANCEL the host's own drag session (post TAGGED synthetic Esc
   down/up then LMB-up — tag via kCGEventSourceUserData so our own tap ignores them; wait up to ~150 ms for the drag to
   end), send DFOF to the client, then do the normal switch (leave/enter). The host's Finder must never see a drop.
3. Client `enter` with a pending offer: the platform bridge starts a real drag session under the cursor (below); the
   server keeps forwarding motion; the server forwards the physical LMB-up to the client, which ends the session
   (drop). While the offer is pending the client's button state must include the (virtual) LMB so motion is posted as
   drag events (OSXScreen chooses drag vs move events from its button state).
4. Leave/cancel paths: cursor returns to the host before release, connection loss, feature disabled, timeout → the
   client posts Esc+LMB-up (tagged) to abort its session, both sides send/handle DFCN, release fds/partials.

### macOS "drag bridge" (platform lib, Objective-C++; macOS main thread via dispatch_async(main))
- SOURCE side: pasteboard peeker (reuse/replace src/lib/platform/OSXPasteboardPeeker.*, currently unused) + change-count
  logic + `cancelLocalDrag()` (tagged Esc + LMB-up).
- TARGET side: a small borderless, transparent, non-activating NSPanel under the cursor (correct multi-display Y flip:
  use the screen containing the point) whose view starts `beginDraggingSessionWithItems:event:source:` with one
  `NSFilePromiseProvider` per offered file (UTType from the extension, fileName = sanitised name). Promise delegate:
  `writePromiseToURL:completionHandler:` receives the DESTINATION URL from Finder; it asks the FileReceiver to fetch the
  file and write to that URL; the completion handler MUST always be called (success or error) or Finder hangs.
  `operationQueueForFilePromiseProvider:` = background queue. Synthetic events must go through the existing
  Screen::mouseDown/fakeMouseButton path so pressed-button tracking and macOS 27 event numbers stay right.
- A feasibility spike (docs/dev/file-drag/spike) validates: synthetic LMB + `beginDraggingSession` + promise → Finder gives
  the destination folder; and synthetic Esc cancels an active drag. Its result decides the receiver-side technique
  (modern NSFilePromiseProvider vs legacy `dragImage:` + NSFilesPromisePboardType).

### Settings / UI
- Setting (like the clipboard ones) e.g. `Core::FileDragEnabled` (default true only when TLS is on), limits
  (max total size default 8 GiB, per file 4 GiB, max files 64). Optional Advanced-view toggle + a short notice/progress
  ("Copying report.pdf… 40%") in the main window log/status bar (no big UI).
- macOS: privacy prompts (Files & Folders / Documents / Desktop / removable / network volumes) can appear for reading
  the dragged files and writing the promised destination: document; add usage strings to deskflow.plist.in if needed
  (NSDesktopFolderUsageDescription, NSDocumentsFolderUsageDescription, NSDownloadsFolderUsageDescription,
  NSRemovableVolumesUsageDescription, NSNetworkVolumesUsageDescription).

### Safety rules (non-negotiable)
- The receiver never chooses paths; the destination is the URL Finder hands to the promise, plus our `.part` sibling.
- The source serves only files it snapshotted from the user's own drag, only by (id,index), never by path.
- Bounded everything: message sizes, files, bytes, in-flight credit, time. No file data ever buffered whole in RAM.
- Off unless the session is authenticated (TLS + trusted fingerprints); documented.

### Testing plan
- Unit: message codecs; engine (0-byte, 1-byte, chunk-boundary and multi-MiB files, several files, cancel mid-file,
  credit accounting, unknown ids, out-of-range indexes, size mismatch, file changed during transfer, name sanitiser
  table, limits, partial cleanup) over an in-memory loopback transport, plus over real client/server proxies on 127.0.0.1.
- Integration: a scripted protocol-1.9 test server/client pair in one process (no OS drag) that drives the whole offer→
  request→data→end flow; LayeringCheck must pass; -Wall -Wextra -Werror clean on macOS and Linux (platform bridge is
  compiled out on Linux).
- Manual (user, two Macs): drag a file across, drop in Finder folder / Desktop / an app window; drop after moving back;
  cancel with Esc; large file; while typing/moving the mouse (no lag).
