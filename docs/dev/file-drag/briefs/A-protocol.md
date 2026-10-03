# Brief A — protocol 1.9 message layer (WP1)

One agent (Sonnet is enough), in an isolated git worktree of `feat/file-drag`. Runs in parallel with brief B; the two
touch different files. Give the agent this whole file as its prompt, plus the line "write the patch to
`<scratch>/filedrag-A.patch`".

Context: C++20/Qt6/OpenSSL fork of Deskflow. Read first, in this order: `docs/dev/file-drag/SPEC.md` (your part = "Protocol 1.9"),
`docs/dev/file-drag/EXPLORATION.md` (file:line pointers for adding a message and negotiating a minor version),
`src/lib/deskflow/FileDragTypes.h` (THE CONTRACT — do not change existing signatures, only add),
`docs/dev/architecture.md` and `cmake/CheckLayering.cmake` (new files must sit in the right CMake target).
Do NOT create or edit the engine files (`FileDrag*.{h,cpp}` other than the contract, `FileNameSanitizer.*`): brief B owns them.

## Job
1. `ProtocolTypes.h`: bump `kProtocolMinorVersion` 8 to 9. Add DFOF (offer), DFRQ (request and credit), DFDT (data), DFEN (file
   end and status), DFCN (cancel) with the formats of SPEC.md (check that the 4-char codes collide with nothing). ids, index,
   credit and status are u32 (`%4i`); the offer entries are a binary-safe blob (`%s`) of repeated
   `[u16 nameLen BE][name UTF-8][u64 size BE]`; a data chunk is `%s` of at most `kMaxChunkBytes`. Formats are validated at compile
   time (ProtocolUtil.h): add them to the format tests and to `src/fuzz/ProtocolUtilFuzzer.cpp` if such lists exist. Remove the
   obsolete DFTR/DDRG constants and stubs (`ProtocolTypes.h` ~1030-1070, `BaseClientProxy`/`ClientProxy`/`ClientProxy1_0`/`1_5`
   `sendDragInfo`/`fileChunkSending`, `PrimaryClient` stubs) when nothing uses them; keep the build and tests green.
2. `FileDragCodec.{h,cpp}` in `src/lib/deskflow/` (domain target): `encodeEntries(const std::vector<Entry>&) -> std::string` and
   `decodeEntries(std::string_view, uint32_t count) -> std::optional<std::vector<Entry>>` with strict validation: count at most
   `kMaxFilesPerOffer`, each name 1..`kMaxNameBytes`, valid UTF-8, no NUL, control characters or path separators, blob at most
   `kMaxEntriesBlobBytes`, no trailing bytes. Tests with truncated, over-long, bad-UTF-8, wrong-count and trailing-garbage input.
3. Proxies: `ClientProxy1_9` (server side, one per client) derives `ClientProxy1_8`; `ServerProxy1_9` (client side) derives
   `ServerProxy1_8`. Negotiate 1.9 only when both sides speak it; 1.8 peers (official Deskflow) must keep working unchanged
   (`ClientProxyUnknown.cpp` ~146-195, `Client.cpp` ~474-492; check the Hello/HelloBack logic for the advertised version). Each
   1.9 proxy implements `IPeerChannel` (writef of the messages, same conventions as its neighbours) and parses incoming
   DFOF/DFRQ/DFDT/DFEN/DFCN, validating shape (blob via `decodeEntries`, data at most `kMaxChunkBytes`), then forwards to an
   `IPeerMessages*` installed with `setFileDragMessages(IPeerMessages*)` (null = ignore, log at debug). Expose
   `IPeerChannel *fileDragChannel()` and a way for Server and Client to ask "does my peer speak 1.9" without including each
   other or platform code (least invasive virtual on the common base; document it). Do NOT wire the engine into Server or Client
   and do not touch crossing or lock logic.
4. Tests (`src/unittests`, like their neighbours): byte-exact wire tests of every message in both directions, round trips,
   negotiation (1.9-1.9 gives 1.9; server 1.9 with client 1.8 gives 1.8; client 1.9 with server 1.8 gives 1.8), malformed
   messages rejected without a crash and without forwarding, `IPeerMessages` gets exactly what was sent (recording fake), and the
   existing proxy tests stay unchanged and green.
5. Docs: `docs/dev/protocol_reference.md` — the 1.9 section (message rows, formats, flow, limits, version table); remove the
   obsolete DFTR/DDRG rows.

## Rules
Match surrounding style, sparse comments, SPDX headers on new files (copy a neighbour), LayeringCheck passes, `-Wall -Wextra
-Werror` clean on macOS AND Linux, clang-format 20.1.0 on every changed file. No commit. Never launch the macOS GUI or
`deskflow-core`. Work in small steps and avoid very long single tool outputs (an agent stalled on one before).

Build and test: `cmake -S . -B build-wt -DCMAKE_BUILD_TYPE=Debug -DSKIP_BUILD_TESTS=ON -DCMAKE_COMPILE_WARNING_AS_ERROR=ON`,
`cmake --build build-wt -j6`, `ctest --test-dir build-wt -j6 --output-on-failure`; revert regenerated translations before
finishing (`git checkout -- translations`). Linux: the `deskflow-linux-build` Docker image (Debian + Qt6 + Xvfb;
`docs/dev/tools` has no Dockerfile, it is: debian:stable-slim + cmake ninja build-essential qt6-base-dev qt6-tools-dev
libssl-dev libxkbfile-dev libxkbregistry-dev libxkbcommon-dev libei-dev libportal-dev xvfb xauth).

Report: files changed, new APIs (exact signatures), wire formats as implemented, negotiation behaviour, test results.
