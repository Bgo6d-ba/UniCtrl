dragspike - macOS drag-and-drop feasibility spike (for Deskflow)
=================================================================

WHAT IT TRIES TO PROVE  (macOS 26, Apple Silicon)
  (a) an app can start a REAL drag session carrying a file PROMISE using only synthesized mouse events
      (CGEventPost) - what a Deskflow client would do when a remote user drags a file over to it;
  (b) Finder accepts that drop and the promise callback hands us the DESTINATION FOLDER;
  (c) a drag in progress can be cancelled by posting a synthesized Escape key event (what the SOURCE
      computer would do when the cursor leaves it).
  The result decides the receiver-side technique: modern NSFilePromiseProvider (default) vs the legacy
  dragPromisedFilesOfTypes: (--legacy).  `pasteboard` checks the host-side "is a file drag active" detection.

FILES  (this directory)
  dragspike.m   the whole spike, Objective-C + ARC
  dragspike     the built binary
  selftest.m    optional headless self-check of the NON-input logic (posts no events, opens no windows)
  README.txt    this file

BUILD  (needed once: the binary is not committed)
  cd docs/dev/file-drag/spike (from the repository root)
  clang -fobjc-arc -Wall -Wextra -framework AppKit -framework CoreGraphics dragspike.m -o dragspike
  (selftest:  clang -fobjc-arc -Wall -Wextra -framework AppKit -framework CoreGraphics selftest.m -o selftest && ./selftest)
  Needs macOS 14+ (uses -[NSApplication activate]).

BEFORE YOU RUN drop OR cancel  -  READ THIS
  * They MOVE YOUR REAL CURSOR and post REAL mouse events / a REAL Esc key press for about 5-10 s.
    After the 3-second countdown keep your hands OFF the mouse and keyboard until it prints "DONE".
  * Accessibility: enable the app you run it FROM (Terminal / iTerm2 / Ghostty / VS Code / the Claude app)
    in System Settings > Privacy & Security > Accessibility, then QUIT AND REOPEN that app.
    `./dragspike check` tells you whether that worked (safe, nothing moves).
  * Use a normal (NOT full-screen) terminal window on the main display, with Finder running.
  * Every mode exits by itself in well under 20 s (hard watchdog at 18 s; pasteboard max 20 s).
  * Ctrl-C, SIGTERM (`pkill dragspike`) and SIGHUP release the synthetic mouse button and put the cursor
    back. NEVER use kill -9 (it cannot clean up; if a click ever seems stuck, just click once yourself).
  * Side effects: it deletes and recreates /tmp/dragspike-dest and opens it in Finder. Nothing else is
    touched (a stray copy of the test file dropped elsewhere would be removed again).

COMMANDS, IN THIS ORDER
  cd docs/dev/file-drag/spike (from the repository root)

  1. ./dragspike --help
  2. ./dragspike check
        Safe. Prints Accessibility status, displays, Finder pid, cursor. Both trust lines should say YES.
  3. ./dragspike pasteboard
        Safe. For 20 s, drag any file around in Finder with your REAL mouse (pick it up, wave it, drop it,
        then do it again; also try a plain click on a file WITHOUT dragging). See "pasteboard" below.
        If macOS pops up a pasteboard-access prompt, note the wording and paste it back.
  4. ./dragspike drop 2>&1 | tee /tmp/dragspike-drop.log
  5. ./dragspike cancel 2>&1 | tee /tmp/dragspike-cancel.log

  If step 4 says FAIL, try these variants ONE AT A TIME (each still uses only CGEventPost input):
        ./dragspike drop --real-event     start the session from the real mouseDown: NSEvent
        ./dragspike drop --activate       make the source window activating
        ./dragspike drop --legacy         the OLD technique: dragPromisedFilesOfTypes: +
                                          namesOfPromisedFilesDroppedAtDestination:
        ./dragspike drop --legacy --real-event --activate     (closest to what old Deskflow did)
  If step 4 says PASS, also run:
        ./dragspike drop --invisible      source window alpha 0.01 (proves it may be ~fully transparent)

  Please paste the complete terminal output of every run back (the timestamps matter).

WHAT `drop` DOES  (times are relative to the sequence start t=0; the leading [ms] on every line is the
                   log clock since process start)
  countdown 3 s -> recreate /tmp/dragspike-dest -> `open` it in Finder -> wait 1.5 s -> find the NEW Finder
  window by diffing CGWindowList before/after (titles are NOT used; if no NEW window appears it falls back to
  the front-most Finder window with a WARNING, and exits 2 only if there is no Finder window at all) ->
  target = 62% across / 55% down that window, start = 150 px left of the target, both clamped onto the same
  screen -> show a 48x48 faint orange NSPanel (non-activating, level NSStatusWindowLevel) centred on the
  start point ->
    t=0       warp cursor to start (+ associate mouse and cursor)
    t=+150ms  LeftMouseDown at start        via CGEventPost(kCGHIDEventTap), HIDSystemState event source
    t=+300ms  beginDraggingSessionWithItems: (NSFilePromiseProvider, public.plain-text) with a synthetic
              LeftMouseDragged NSEvent
    t=+400ms  40 LeftMouseDragged events, 16 ms apart, start -> target; hold 300 ms; LeftMouseUp at target
  then it waits up to 5 s for the promise callback (the file is written 300 ms after the callback on
  purpose), prints RESULT, restores the cursor, exits.  You should see a Finder window, the cursor jump left
  of the middle, a small orange square, an icon "DS" dragged into the window, and dragspike-test.txt appear.
  All timers run on the main run loop in NSRunLoopCommonModes; if the log shows the drag timers arriving
  late after "BEGIN SESSION" the session is stalling the run loop (the "run loop mode" in the cursor-check
  lines shows which mode it runs in).

WHAT `cancel` DOES
  countdown -> orange NSPanel (ACTIVATING, so the synthetic click makes dragspike the frontmost app and a
  keystroke reaches it) at the CURRENT cursor position ->
    t=0 NSApp activate; t=+150 LeftMouseDown; t=+300 begin session (same as drop, no movement);
    t=+900 print the frontmost app, post Esc (keycode 53) down+up via CGEventPost(kCGHIDEventTap),
           wait up to 2 s for the session-ended callback
           If it did NOT end: extra experiment, Esc again via CGEventPostToPid(self), wait 1.5 s more
    then LeftMouseUp to release the synthetic button -> RESULT -> restore cursor -> exit.
  Not covered: the Esc-then-immediate-LMB-up ORDERING race of the real design (the source here is our own
  process, not Finder, and nothing under the cursor accepts a drop, so cancel and drop-on-nothing look alike).

HOW TO READ `drop`
  Key log lines:
    TARGET ... CURSOR START ...        where it is going; check the target is inside Finder's empty content area
    cursor check (...)                 the cursor really followed the posted events; MISMATCH = CGEventPost
                                       not effective (Accessibility!) or another app grabbed the pointer
    source view received the REAL mouseDown   the synthetic click reached our window (if this never
                                       appears the panel was not under the cursor or ate no events)
    BEGIN SESSION ... pressedMouseButtons=0x1   0x1 = the window server sees the left button HELD (good).
                                       0x0 = the synthetic mouse-down did not register as held.
    DELEGATE draggingSession:willBeginAtPoint: ... "drag pasteboard now: types=[...]"   AppKit accepted the
                                       session; the types are what a host would see during a real remote drag.
    DELEGATE ...moved #N               the session tracks our posted drags.
    DELEGATE ...ended ... operation=Copy   normal end after a drop.  operation=None = nobody took it.
    PROMISE writePromiseToURL: <url> => destination folder = <dir>     THE ANSWER TO (b): Finder handed us the
                                       folder of the drop.  "(queue: background)" is expected.
                                       (--legacy prints LEGACY namesOfPromisedFilesDroppedAtDestination: instead)
    PROMISE fulfilled 300 ms later     the promise can be kept asynchronously after the callback returned.
  RESULT block:
    (a) session started YES + pressedMouseButtons at start 0x1 + "ended BEFORE the mouse-up: NO"
            -> synthesized events DO keep a real drag session alive.
    (b) Finder asked for the promise YES + DESTINATION FOLDER ... MATCH
            -> Finder accepted the drop and told us the folder.  DIFFERENT = the drop landed in another
               folder/window (the stray file is deleted again) - UNLESS the verdict is PASS and a NOTE says the
               promise URL folder differs: then Finder STAGED the file elsewhere and moved it in, so the promise
               URL is not the drop folder (an important design finding - paste the whole output back).
    VERDICT PASS = /tmp/dragspike-dest/dragspike-test.txt exists with "hello from dragspike\n".
  Diagnosing a FAIL:
    session NO + WARNING "no left button is down"   CGEventPost is not producing a held button:
            check `./dragspike check`, restart the terminal app after granting Accessibility.
    session YES but "ended BEFORE the mouse-up: YES"  AppKit killed the drag although the button was held
            (0x1) -> try --real-event, then --activate, then --legacy.  If pressedMouseButtons was 0x0 the
            window server never saw a held button, so synthetic events cannot carry a drag as posted.
    session survived, promise NO   Finder did not take the drop: look at the TARGET (sidebar? another window
            on top?), or Finder refused a promise from this source (try --legacy).
    exit code 2   no Finder window appeared (other Space / Finder hidden); 3 no permission; 4 watchdog.

HOW TO READ `cancel`
    frontmost app: dragspike (pid ...)   Esc via the HID tap will reach us.  If it names ANOTHER app the
            Esc goes there, and a FAIL only says "HID Esc needs the source app to be frontmost".
    PASS  "HID-tap Esc ended the session after N ms with operation None"   -> (c) proven; N is the latency
            (the design budgets ~150 ms).
    FAIL  "HID-tap Esc had NO effect; only the pid-targeted fallback Esc ended it"   -> post Esc with
            CGEventPostToPid(<pid of the app the user is dragging from>) instead of the HID tap.
    FAIL  "Esc had NO effect"   -> neither works; another cancel strategy is needed.
    FAIL  "session ended BEFORE Esc"   -> the drag never stuck; fix (a) first (run `drop` diagnostics).

HOW TO READ `pasteboard`
  A line is printed on every change and once a second: changeCount, leftButton=DOWN/up, type list, and
  FILE-URL / FILE-PROMISE flags; on a change the public.file-url paths are listed.  It also applies the
  design's detection rule (left button down remembers changeCount; a later change WITH public.file-url):
        >>> FILE DRAG DETECTED (left button down, changeCount 222 -> 223, public.file-url present)
  Healthy result: that line appears when you pick up a file and drag it, and does NOT appear for a plain
  click or for an old, stale pasteboard.  NOTE: the drag pasteboard KEEPS the previous drag's contents after
  the drag ends, so "types present" alone does not mean "a drag is in progress".  The first line is just
  the baseline (possibly stale).  The poll is 200 ms, so a very fast drag can be missed by this helper (the
  real host would poll from its event tap and see it).

EXIT CODES   0 PASS, 1 FAIL, 2 no Finder window, 3 no permission to post events, 4 watchdog fired,
             5 uncaught exception, 64 usage error
ENV          DRAGSPIKE_IGNORE_TRUST=1 skips the Accessibility pre-check (if it wrongly refuses to start)
