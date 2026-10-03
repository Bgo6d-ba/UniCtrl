# Brief B — platform-neutral transfer engine (WP2)

One agent (Sonnet is enough), in an isolated git worktree of `feat/file-drag`. Runs in parallel with brief A; the two touch
different files. Give the agent this whole file as its prompt, plus the line "write the patch to `<scratch>/filedrag-B.patch`".

Context: C++20/Qt6/OpenSSL fork of Deskflow. Read first: `docs/dev/file-drag/SPEC.md` (your part = "Transfer engine" and the
"Testing plan"), `docs/dev/file-drag/EXPLORATION.md`, `src/lib/deskflow/FileDragTypes.h` (THE CONTRACT — do not change existing
signatures, only add), `docs/dev/architecture.md` and `cmake/CheckLayering.cmake` (the engine lives in the `domain` target: the flat
`src/lib/deskflow/` folder, listed in `src/lib/deskflow/CMakeLists.txt`), `src/lib/base/IEventQueue.h` and `EventQueue.*`
(thread-safe `addEvent`), `ClipboardChunk.*` (a start/chunk/end state machine to learn from).
Do NOT touch `ProtocolTypes.h`, `FileDragCodec.*`, the 1.9 proxies or negotiation: brief A owns them.

## Job
1. `FileNameSanitizer.{h,cpp}`: `std::optional<std::string> sanitize(std::string_view utf8Name)`. NFC-normalise (Qt's
   `QString::normalized` if Qt Core is available to `domain`; otherwise a documented minimal approach), reject empty, `.`, `..`,
   a leading dot, `/` and `\`, NUL, control characters, DEL, invalid UTF-8, Unicode format characters (Cf), trailing dots or spaces;
   names over `kMaxNameBytes` are truncated at a code point boundary keeping the extension when possible, else rejected (decide,
   document). Table-driven tests.
2. `FileDragSender` (source role): `std::optional<Offer> makeOffer(const std::vector<std::filesystem::path>&)` snapshots and OPENS
   the files at once with `open(O_RDONLY|O_NOFOLLOW|O_CLOEXEC)` (POSIX under `#ifndef _WIN32`; Windows: a minimal `CreateFileW`
   equivalent that cannot be compiled here, kept mechanical and clearly marked), `fstat` and regular files only (folders,
   symlinks, devices are skipped and returned in a "skipped" list), sanitised names, `Limits` (count, per-file, total), records
   size and (dev, inode, mtime) to detect a changed file, unique id. Serving: `onRequest(id,index,credit)` grants credit; a worker
   (`std::jthread` or `QThread`, Qt/std types only, not the legacy `Thread`/`Mutex`) reads `kMaxChunkBytes` chunks with `pread`
   only while credit remains and posts each chunk to the channel ON THE EVENT LOOP THREAD via `IEventQueue::addEvent` — never from
   the worker. After the last byte send `Ok`; read error, size or mtime change give `ReadError`/`Changed`; unknown id/index or
   misuse gives `Refused` without crashing. `cancel(id)`/`cancelAll()` close fds and stop workers promptly. Inactivity timeout and
   offer lifetime come from `Limits` via event-queue timers. At most one active offer per sender (a new one cancels the old).
   Never buffer a whole file.
3. `FileDragReceiver` (target role): `onOffer` validates (count, names again with the sanitiser, sizes against `Limits`, total),
   keeps at most one pending offer (a new one replaces it) and reports through `std::function` observers (offerReceived,
   progress, finished, cancelled). The platform bridge later calls `fetch(id, index, destinationPath, FetchDone)` when the user
   drops file `index`: check free space (`minFreeBytes` + size) on the destination volume, create `<destination>.part` with
   `O_EXCL`, send DFRQ with `kInitialCredit`, write incoming DFDT chunks strictly in order (never beyond the granted credit or the
   announced size, grant more credit as bytes are written, keep about `kInitialCredit` in flight), on DFEN Ok verify the exact size
   and `rename` to the destination (the promise API gives the FINAL url), then `done(true, "")`. On any error, cancel, disconnect or
   timeout delete the `.part` file and call `done(false, reason)` EXACTLY ONCE. Never block the event loop on a slow disk.
   `cancel(id)`/`cancelAll()` fail every pending fetch.
4. `FileDragEndpoint.{h,cpp}` = the facade Server and Client will own per connection: implements `IPeerMessages`, holds a Sender and a
   Receiver, constructed with `(IPeerChannel&, IEventQueue*, Limits)`. API: `offer(files, skipped*)` (snapshot, open, send DFOF),
   `cancel(id)` (sends DFCN), `setTarget(IFileDragTarget*)`, `beginDrop()`, `endDrop()`, `peerGone()`, observer setters. Route
   messages by role: requests and cancels from the peer to the Sender; offers, data and ends to the Receiver; DFCN(id) to whichever
   role owns that id.
5. Tests (`src/unittests/deskflow/`, `create_test` like neighbours; temp dir; deterministic; no long sleeps): two endpoints joined by
   an in-memory loopback `IPeerChannel`/`IPeerMessages` pair (fake event queue that runs posted work when the test pumps it, or the
   real one) covering: 0-byte, 1-byte, chunk-boundary (64 KiB, 64 KiB plus or minus 1) and multi-MiB files; several files;
   out-of-order or over-credit data rejected; cancel mid-file from either side (partial removed, `done(false)` once); unknown
   id/index; size mismatch; file changed during the transfer; symlink and directory skipped; sanitiser table; limits exceeded;
   ENOSPC-like (inject a free-space function); `.part` collision; `peerGone`; inactivity and offer lifetime with short limits; no
   channel call off the event-loop thread (assert in the fake); credit accounting (bytes in flight never exceed the grant); a
   32 MiB throughput sanity test.

## Rules
Match surrounding style, sparse comments, SPDX headers on new files (copy a neighbour), LayeringCheck passes, `-Wall -Wextra
-Werror` clean on macOS AND Linux, clang-format 20.1.0 on every changed file. No commit. Never launch the macOS GUI or
`deskflow-core`. Work in small steps and avoid very long single tool outputs.

Build and test as in brief A, and repeat the new tests (`--repeat until-fail:5 -R FileDrag`). The engine has threads and file
descriptors, so also run the new tests under ASan+UBSan and TSan in the Linux container (see how the top-level CMake wires
`ENABLE_SANITIZERS`).

Report: files changed, the public API of every class (exact signatures), the threading model, test results (macOS, Linux, ASan,
TSan), anything unsure.
