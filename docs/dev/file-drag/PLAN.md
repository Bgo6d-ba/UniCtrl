# File drag and drop between computers — plan

**Status: PAUSED on 2026-09-29.** The user will continue in a few days. What exists: the contract header
`src/lib/deskflow/FileDragTypes.h` (committed), the design (`SPEC.md`), the exploration of the removed upstream code
(`EXPLORATION.md`), two ready-to-run agent briefs (`briefs/`), and a feasibility spike (`spike/`) that has been built but
run once (`drop` PASSED on 2026-09-29, see section 9); `cancel` and a real Finder-drag pasteboard check are still to run. Nothing of the feature itself is implemented.

Branch: `feat/file-drag` (local, not pushed), on top of `feat/host-join` (the Host/Join flow, commit `3e6e50ed6`, also not
pushed; `master` is untouched).

## 1. Goal

Drag file(s) in Finder on one computer, keep the button held, cross to the other computer's screen and release. The file must
land **where the user releases it** (the Finder folder, window or desktop under the cursor), not in a fixed download folder.
Server to client first, client to server next. macOS to macOS first; Windows and Linux later.

## 2. Decisions so far

| # | Decision | Notes |
|---|---|---|
| D1 | Drop where released | Chosen over "copy to `Downloads/Deskflow`" and over Cmd+C / Cmd+V. Those two stay possible later as fallbacks (Cmd+C / Cmd+V is the simplest and most robust). |
| D2 | macOS to macOS first, host to client first | Windows cannot be built or tested yet (GitHub Actions billing on the fork). |
| D3 | Protocol 1.9, negotiated | 1.8 peers (official Deskflow) never see the new messages. |
| D4 | Modern `NSFilePromiseProvider` on the receiving side | The legacy `dragPromisedFilesOfTypes:` only if the spike says so. |
| D5 | Regular files only in v1 | No folders, packages, aliases (they are skipped with a notice). Limits: 64 files, 4 GiB per file, 8 GiB per drag. |
| D6 | Process | Sonnet agents in isolated worktrees, each patch reviewed adversarially, then integrated and verified on macOS and in the Linux container. |

## 3. What we learned (verified)

- Upstream Deskflow **removed** this feature on 2025-05-04 (commit `5365e34f0`, "its broken on all platforms", about 2000 lines).
  Left over: the message constants DFTR/DDRG with no-op stubs, and the unused `OSXPasteboardPeeker`.
- Today `Screen::isLockedToScreen` forbids leaving a screen while **any** mouse button is held, and `Screen.cpp` releases pressed
  buttons on leave. Both must change for an active file drag. Details and file:line pointers: `EXPLORATION.md`.
- Why the old design broke (fixed by the new one): whole files pushed unpaced on the input stream and held in RAM, no ids, requests,
  acks or cancel, comma-delimited paths, sender paths sent over the wire, deprecated AppKit APIs, sleeps, and our own synthetic
  mouse-up re-entering our event tap.
- Apple's **Universal Control** already does drag and drop between Macs (System Settings, Displays, Advanced). Deskflow's value is
  the cross-platform case and the Host/Join flow.
- Trackpad multi-finger gestures (Mission Control, swiping between desktops) cannot be forwarded by Deskflow; use keyboard
  shortcuts on the other computer.

## 4. Design in one page (full text: `SPEC.md`)

- **Protocol 1.9** messages: DFOF offer (names and sizes, no paths), DFRQ request with credit, DFDT data chunk (at most 64 KiB),
  DFEN file end and status, DFCN cancel. The receiver pulls each file when the user drops it; the source streams under a credit
  window (about 256 KiB), so mouse and key events are never held up and no file is buffered whole.
- **Engine** (platform neutral, `domain` library): `FileDragSender`, `FileDragReceiver`, `FileDragEndpoint`, `FileNameSanitizer`.
  Files are opened at offer time with `O_NOFOLLOW`, served only by (id, index), written to `<destination>.part` and renamed.
- **Crossing** (server side): when a file drag is active and the feature is on, lift the button lock, snapshot the files, cancel the
  host's own drag (tagged synthetic Esc, then mouse-up, ignored by our own tap), send the offer, then switch screens as usual.
- **macOS bridge**: source side reads the drag pasteboard (`changeCount` plus `public.file-url`); target side shows a small
  non-activating panel under the cursor and starts a drag session with one file promise per file; Finder hands over the
  destination URL, the engine fetches the bytes and completes the promise (the completion handler must always be called).
- **Safety**: the receiver never picks a path, the source serves only its own snapshot, everything is bounded, off unless the
  session is TLS-authenticated.

## 5. Work packages

| WP | Package | Depends on | Status | Estimate |
|---|---|---|---|---|
| 0 | Feasibility spike (`spike/`) | none | `drop` **PASS**; `cancel`, `pasteboard` still to run | 10 s per run |
| 1 | Protocol 1.9 messages, codec, proxies, negotiation (`briefs/A-protocol.md`) | contract | not started | 30-60 min |
| 2 | Transfer engine (`briefs/B-engine.md`) | contract | not started | 40-70 min |
| 3 | Server, Client, Screen integration: lock exception, offer at crossing, endpoints, leave and cancel, setting `Core::FileDragEnabled`, option plumbing | 1, 2 | not started | 1-1.5 h |
| 4 | macOS bridge: source detection and cancel, target session with promises, main-thread hops, multi-display | 0, 2 | not started | 1-2 h |
| 5 | Settings, notices and progress, docs, `Info.plist` privacy strings | 3, 4 | not started | 30 min |
| 6 | Client to server direction (symmetry) | 3, 4 | not started | 1 h |
| 7 | Integration, Linux, sanitizers, DMG, then the user's two-Mac test and fixes | all | not started | 30 min, then iterations |
| 8 | Windows and Linux bridges | 7 | later | n/a |

Agent time to a first testable DMG: about 3-5 hours. macOS drag rarely works on the first try, so plan 1-2 rounds of fixes after
the first two-Mac test.

WP 1 and 2 can run in parallel right away. WP 3 and 4 need their results and the spike outcome. Every package: `-Wall -Wextra
-Werror` clean on macOS and Linux, LayeringCheck green, clang-format 20.1.0, tests, adversarial review.

## 6. Resume checklist

1. `cd ~/Documents/deskflow-plus && git switch feat/file-drag && git log --oneline -5`.
2. **Run the spike** (moves the real cursor for about 10 s; hands off the mouse and keyboard after the countdown; the app you run
   it from needs Accessibility): full instructions in `spike/README.txt`. Shortest path:
   ```bash
   cd docs/dev/file-drag/spike
   clang -fobjc-arc -Wall -Wextra -framework AppKit -framework CoreGraphics dragspike.m -o dragspike
   ./dragspike check
   ./dragspike drop 2>&1 | tee /tmp/dragspike-drop.log
   ./dragspike cancel 2>&1 | tee /tmp/dragspike-cancel.log
   ```
   Optional: `./dragspike pasteboard` and drag a real file in Finder for 20 s.
3. Read the outcome with this table, and write it into section 9 of this file:

   | Result | Meaning | Next step |
   |---|---|---|
   | `drop` PASS | Synthetic mouse events carry a real drag and Finder tells us the destination folder | Go: WP 4 as designed |
   | `drop` PASS only with `--real-event` or `--activate` | Needs that tweak | Go, and put the tweak in WP 4 |
   | `drop` PASS only with `--legacy` | Only the deprecated API works | Go with the legacy technique; note the deprecation risk |
   | PASS but the NOTE says the promise URL differs from the drop folder | Finder staged the file elsewhere | Design change: re-check how the destination is used |
   | `drop` FAIL in every variant | Not possible with synthetic events | Stop; fall back to D1's alternatives (fixed folder, or Cmd+C / Cmd+V) |
   | `cancel` PASS | Esc through the HID tap ends a drag | Host drag cancel as designed |
   | `cancel` only the pid-targeted Esc works | | Post Esc to the source app's pid instead |
   | `cancel` FAIL | | Find another way to end the host's drag (this is the riskiest part of the crossing) |

4. Launch WP 1 and WP 2 in parallel: one agent per brief in `briefs/`, each in an isolated worktree of `feat/file-drag`, on Sonnet
   (cheaper; earlier sessions hit usage limits). Patches go to a scratch folder; integrate with `git apply --3way`.
5. Then WP 3, WP 4, WP 5 (write their briefs from `SPEC.md` and the WP 1 and 2 reports), review, integrate.
6. Build a DMG with `docs/dev/tools/make-dmg.sh` (it does not launch the app; every new build needs Accessibility and Local Network
   approved again on each Mac; the same applies to Files and Folders prompts the feature adds).
7. The user tests on two Macs (checklist in section 8).

## 7. Risks and open questions

Risks, most serious first:
1. **Cancelling the host's own drag** (stale pasteboard false positives, Esc and mouse-up ordering, our tap re-seeing our own
   events). A failure either drops the file on the host or leaves a stuck drag.
2. **The synthetic drag session on the receiving Mac** (activation rules on macOS 14 and later, Spaces and full-screen apps,
   multi-display Y flip, macOS 27 event numbers, ordering between the main thread and the core thread).
3. **Privacy prompts**: reading the dragged files (Desktop, Documents, external and network volumes) and writing the destination.
   Plan for usage strings and documentation; pasteboard prompts on macOS 26 and 27 are unverified.
4. **Promise lifecycle**: `writePromiseToURL:` may be late, duplicated or never called; the completion handler must always run.
   Photos and Mail promises are not file URLs. Folders and extended attributes are not handled in v1.
5. Shared-stream latency, hostile peers, and plain-text sessions when TLS is off.

Open questions for the user: default on or off (proposal: on, only for TLS-authenticated sessions); the size limits; folders in
v1 or later; whether to add a Cmd+C / Cmd+V file transfer as a simpler second entry point.

## 8. Manual test checklist (two Macs)

Drag one file from a Finder window across the edge and release over (a) a Finder folder window, (b) the desktop, (c) an app
window that accepts files; several files at once; a large file (1 GiB) while moving the mouse and typing (no lag); move back before
releasing (nothing must be dropped on either Mac); press Esc while holding; disconnect during a transfer; drop onto a folder name
that already exists (no overwrite); the same in the other direction (after WP 6).

## 9. Spike results

**2026-09-29, macOS 26.6.2, MacBook Pro (Apple Silicon), run from the Claude app's terminal (Accessibility granted).**

- `check`: `AXIsProcessTrusted` YES, `CGPreflightPostEventAccess` YES.
- `drop` (the default variant: modern `NSFilePromiseProvider`, non-activating panel at alpha 0.25): **PASS**.
  - A synthesised left mouse-down (`CGEventPost` at the HID tap, hardware-style source) was seen by the window server as a held
    button (`pressedMouseButtons=0x1`); `beginDraggingSessionWithItems:` with a synthetic `LeftMouseDragged` `NSEvent` started
    a real drag session, which tracked 40 posted drag events (`movedToPoint` x 40) and did not end early.
  - After the posted mouse-up over a Finder window, Finder asked for the promise about 9 ms later, and `writePromiseToURL:`
    received the URL **inside the folder shown by that Finder window** (`/private/tmp/dragspike-dest/dragspike-test.txt`; the
    folder matched). The promise could be fulfilled **asynchronously** (written 300 ms later on a background queue).
  - None of `--real-event`, `--activate`, `--legacy` was needed. Decision D4 stands: NSFilePromiseProvider.
  - Details for WP 4: `beginDraggingSessionWithItems:` returns before `willBegin` fires (about 115 ms later here), so the
    session start is asynchronous; the `ended` callback reported the operation `0xffffffffffffffff` (every operation), so do
    **not** use it to decide success; use the promise callback. The cursor followed every posted event (0 mismatches).
  - While our own session ran, the drag pasteboard held only promise types (`com.apple.NSFilePromiseItemMetaData`,
    `NSPromiseContentsPboardType`, ...). That says nothing yet about what a real **Finder** drag puts there (file URLs):
    run `./dragspike pasteboard` and drag a real file for the host-side detection rule.
- **Still to run**: `cancel` (does a synthetic Esc through the HID tap end a drag: the riskiest step of the crossing),
  `drop --invisible` (the panel at alpha 0.01), and `pasteboard` with a real Finder drag. Add the results here.

## 10. Other open items (not file drag)

- Host/Join (`feat/host-join`) is built and tested but **not merged** into `master` and not pushed; ask the user before a PR.
- Collected from the first two-Mac test: show "Reconnecting to ..." (not "Joining...") when the connection drops after a join;
  when pairing fails with "Host unreachable", tell the user to check macOS Local Network permission for Deskflow.
- Windows installer: blocked by GitHub Actions billing on `batmunkh0612/deskflow-plus` (runs fail with `startup_failure`). The
  Windows code written so far has never been compiled.
- The Windows SYSTEM privilege escalation (fixed in this fork) also exists in upstream Deskflow: report it privately through their
  GitHub Security Advisory (not done).
- Apple Developer ID signing would stop the repeated Accessibility and Local Network re-approval on every new build.
