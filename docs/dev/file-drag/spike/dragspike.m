// dragspike.m - macOS feasibility spike: synthesized drag session carrying a file PROMISE.
// Build: clang -fobjc-arc -Wall -Wextra -framework AppKit -framework CoreGraphics dragspike.m -o dragspike
// See README.txt. drop/cancel move the REAL cursor and post REAL input events.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>

#define kDestDir  @"/tmp/dragspike-dest"
#define kDestName @"dragspike-test.txt"
#define kPayload  @"hello from dragspike\n"
#define CGKEY(k)  ((__bridge NSString *)(k))

enum { kSize = 48, kSteps = 40, kEscKey = 53 };

// ponytail: plain globals instead of a state object - throwaway spike, one run per process.
static double gT0;
static CGEventSourceRef gSrc;
static CGPoint gOrigCursor;
static CGFloat gAlpha = 0.25;
static _Atomic bool gHaveOrig, gButtonDown;
static _Atomic bool gGotDown, gBegan, gEnded, gPromised, gWritten;
static _Atomic unsigned long gEndOp;
static _Atomic int gMoved, gCursorBad;
static double gBeganT, gEndT, gUpT, gEscT, gFbT, gPromiseT;
static unsigned long gPressedAtStart = ~0ul;
static NSPoint gEndPt;
static NSString *gPromisePath, *gPromiseDir, *gWriteErr;
static id<NSObject> gActivity;
static BOOL gRealEvent, gActivate, gLegacy;

#pragma mark - logging and small helpers

static double nowSec(void) { return NSProcessInfo.processInfo.systemUptime; }
static double msSince(double t) { return (nowSec() - t) * 1000.0; }

static void say(NSString *fmt, ...) NS_FORMAT_FUNCTION(1, 2);
static void say(NSString *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    NSString *s = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    printf("[%8.1f ms] %s\n", msSince(gT0), s.UTF8String);
    fflush(stdout);
}

static NSString *opName(NSDragOperation op) {
    if (op == NSDragOperationNone) return @"None (0)";
    NSMutableArray<NSString *> *a = [NSMutableArray array];
    if (op & NSDragOperationCopy) [a addObject:@"Copy"];
    if (op & NSDragOperationLink) [a addObject:@"Link"];
    if (op & NSDragOperationGeneric) [a addObject:@"Generic"];
    if (op & NSDragOperationPrivate) [a addObject:@"Private"];
    if (op & NSDragOperationMove) [a addObject:@"Move"];
    if (op & NSDragOperationDelete) [a addObject:@"Delete"];
    return [NSString stringWithFormat:@"%@ (0x%lx)", [a componentsJoinedByString:@"|"], (unsigned long)op];
}

static void afterMs(double ms, void (^blk)(void)) {
    NSTimer *t = [NSTimer timerWithTimeInterval:ms / 1000.0 repeats:NO block:^(NSTimer *tm) { (void)tm; blk(); }];
    // common modes: a drag session may run the loop in NSEventTrackingRunLoopMode.
    [NSRunLoop.mainRunLoop addTimer:t forMode:NSRunLoopCommonModes];
}

static void every(double ms, BOOL (^blk)(void)) {
    NSTimer *t = [NSTimer timerWithTimeInterval:ms / 1000.0 repeats:YES block:^(NSTimer *tm) { if (!blk()) [tm invalidate]; }];
    [NSRunLoop.mainRunLoop addTimer:t forMode:NSRunLoopCommonModes];
}

#pragma mark - event posting, cursor, cleanup

static CGPoint cursorNow(void) {
    CGEventRef e = CGEventCreate(NULL);
    CGPoint p = CGEventGetLocation(e);
    CFRelease(e);
    return p;
}

static void postMouse(CGEventType type, CGPoint p, CGPoint prev) {
    CGEventRef e = CGEventCreateMouseEvent(gSrc, type, p, kCGMouseButtonLeft);
    if (!e) return;
    CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
    CGEventSetDoubleValueField(e, kCGMouseEventPressure, type == kCGEventLeftMouseUp ? 0.0 : 1.0);
    if (type == kCGEventLeftMouseDragged) {
        CGEventSetIntegerValueField(e, kCGMouseEventDeltaX, (int64_t)llround(p.x - prev.x));
        CGEventSetIntegerValueField(e, kCGMouseEventDeltaY, (int64_t)llround(p.y - prev.y));
    }
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
    if (type == kCGEventLeftMouseDown) atomic_store(&gButtonDown, true);
    if (type == kCGEventLeftMouseUp) atomic_store(&gButtonDown, false);
}

static void postKey(CGKeyCode code, bool down, pid_t pid) {
    CGEventRef e = CGEventCreateKeyboardEvent(gSrc, code, down);
    if (!e) return;
    if (pid) CGEventPostToPid(pid, e);
    else CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// Idempotent: release a held synthetic button, then put the cursor back.
static void cleanup(void) {
    if (atomic_exchange(&gButtonDown, false)) {
        CGPoint here = cursorNow();
        postMouse(kCGEventLeftMouseUp, here, here);
    }
    if (atomic_exchange(&gHaveOrig, false)) {
        CGWarpMouseCursorPosition(gOrigCursor);
        CGAssociateMouseAndMouseCursorPosition(true);
    }
}

__attribute__((noreturn)) static void finish(int code) {
    cleanup();
    exit(code);
}

static void onFatal(int sig) {
    cleanup();
    _exit(128 + sig);
}

static void onException(NSException *ex) {
    cleanup();
    say(@"UNCAUGHT EXCEPTION %@: %@", ex.name, ex.reason);
    _exit(5);
}

static dispatch_source_t gSigSrc[3];

static void installSafety(double watchdogSec) {
    atexit(cleanup);
    signal(SIGPIPE, SIG_IGN);   // a dead `| tee` must not kill us before cleanup() ran
    NSSetUncaughtExceptionHandler(onException);
    const int fatal[] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGTRAP};
    for (size_t i = 0; i < sizeof fatal / sizeof *fatal; i++) signal(fatal[i], onFatal);
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    const int soft[] = {SIGINT, SIGTERM, SIGHUP};
    for (size_t i = 0; i < 3; i++) {
        int sig = soft[i];
        signal(sig, SIG_IGN);
        gSigSrc[i] = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, (uintptr_t)sig, 0, q);
        dispatch_source_set_event_handler(gSigSrc[i], ^{
            cleanup();
            say(@"signal %d: released the mouse button, restored the cursor, exiting", sig);
            _exit(130);
        });
        dispatch_resume(gSigSrc[i]);
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(watchdogSec * NSEC_PER_SEC)), q, ^{
        cleanup();
        say(@"WATCHDOG: %.1f s elapsed, forced exit (button released, cursor restored)", watchdogSec);
        _exit(4);
    });
}

static void requireTrust(void) {
    bool ax = AXIsProcessTrusted(), pe = CGPreflightPostEventAccess();
    say(@"AXIsProcessTrusted=%d CGPreflightPostEventAccess=%d", ax, pe);
    if (ax || pe || getenv("DRAGSPIKE_IGNORE_TRUST")) return;
    say(@"ERROR: this process may not post synthetic input: CGEventPost would silently do NOTHING.");
    say(@"       Grant Accessibility to the app you launched dragspike from (Terminal / iTerm2 / Ghostty /");
    say(@"       VS Code ...): System Settings > Privacy & Security > Accessibility, then QUIT AND REOPEN that");
    say(@"       app and run again. (DRAGSPIKE_IGNORE_TRUST=1 skips this check.)");
    exit(3);
}

static void countdown(const char *mode) {
    printf("dragspike %s: DON'T TOUCH THE MOUSE OR KEYBOARD until it says DONE (Ctrl-C aborts, cursor is restored)\n", mode);
    for (int i = 3; i >= 1; i--) {
        printf("  starting in %d ...\n", i);
        fflush(stdout);
        sleep(1);
    }
}

#pragma mark - geometry (CG global = top-left origin, AppKit = bottom-left origin)

static CGFloat primaryH(void) { return CGDisplayBounds(CGMainDisplayID()).size.height; }
static NSPoint cgToAK(CGPoint p) { return NSMakePoint(p.x, primaryH() - p.y); }
static CGPoint akToCG(NSPoint p) { return CGPointMake(p.x, primaryH() - p.y); }

static CGPoint clampToScreen(CGPoint p, CGFloat margin) {
    NSPoint a = cgToAK(p);
    NSScreen *scr = nil;
    for (NSScreen *s in NSScreen.screens) {
        if (NSPointInRect(a, s.frame)) { scr = s; break; }
    }
    if (!scr) scr = NSScreen.screens.firstObject;
    NSRect v = NSInsetRect(scr.visibleFrame, margin, margin);
    a.x = MIN(MAX(a.x, NSMinX(v)), NSMaxX(v));
    a.y = MIN(MAX(a.y, NSMinY(v)), NSMaxY(v));
    return akToCG(a);
}

static void noteCursor(CGPoint want, double tol, NSString *what) {
    CGPoint c = cursorNow();
    BOOL ok = hypot(c.x - want.x, c.y - want.y) <= tol;
    if (!ok) atomic_fetch_add(&gCursorBad, 1);
    say(@"cursor check (%@): now=(%.0f,%.0f) expected=(%.0f,%.0f) -> %@", what, c.x, c.y, want.x, want.y,
        ok ? @"OK" : @"MISMATCH (posted events not moving the cursor: Accessibility for the parent app?)");
}

#pragma mark - Finder window discovery

static NSArray<NSDictionary *> *finderWindows(void) {
    NSRunningApplication *f = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.finder"].firstObject;
    if (!f) return @[];
    NSArray *all = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    NSMutableArray<NSDictionary *> *out = [NSMutableArray array];
    for (NSDictionary *w in all) {   // front-to-back order
        if ([w[CGKEY(kCGWindowOwnerPID)] intValue] != f.processIdentifier) continue;
        if ([w[CGKEY(kCGWindowLayer)] intValue] != 0) continue;
        CGRect b;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[CGKEY(kCGWindowBounds)], &b)) continue;
        if (b.size.width < 300 || b.size.height < 200) continue;
        [out addObject:w];
    }
    return out;
}

static CGRect boundsOf(NSDictionary *w) {
    CGRect b = CGRectZero;
    CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[CGKEY(kCGWindowBounds)], &b);
    return b;
}

#pragma mark - source window, view, drag session

static NSImage *makeIcon(void) {
    return [NSImage imageWithSize:NSMakeSize(kSize, kSize) flipped:NO drawingHandler:^BOOL(NSRect r) {
        [[NSColor colorWithSRGBRed:1 green:0.45 blue:0.1 alpha:1] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, 2, 2) xRadius:8 yRadius:8] fill];
        NSDictionary *at = @{NSFontAttributeName: [NSFont boldSystemFontOfSize:20],
                             NSForegroundColorAttributeName: NSColor.whiteColor};
        NSSize sz = [@"DS" sizeWithAttributes:at];
        [@"DS" drawAtPoint:NSMakePoint(NSMidX(r) - sz.width / 2, NSMidY(r) - sz.height / 2) withAttributes:at];
        return YES;
    }];
}

static void noteBegan(NSPoint p, const char *api) {
    gBeganT = nowSec();
    atomic_store(&gBegan, true);
    CGPoint cg = akToCG(p);
    NSPasteboard *pb = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    say(@"DELEGATE %s: began at AK(%.0f,%.0f)=CG(%.0f,%.0f); NSEvent.pressedMouseButtons=0x%lx",
        api, p.x, p.y, cg.x, cg.y, (unsigned long)NSEvent.pressedMouseButtons);
    say(@"drag pasteboard now: changeCount=%ld types=[%@]", (long)pb.changeCount, [pb.types componentsJoinedByString:@" "]);
}

static void noteMoved(NSPoint p, const char *api) {
    int n = atomic_fetch_add(&gMoved, 1) + 1;
    if (n == 1 || n % 10 == 0) {
        CGPoint cg = akToCG(p);
        say(@"DELEGATE %s: moved #%d CG(%.0f,%.0f)", api, n, cg.x, cg.y);
    }
}

static void noteEnded(NSPoint p, NSDragOperation op, const char *api) {
    gEndT = nowSec();
    gEndPt = p;
    atomic_store(&gEndOp, (unsigned long)op);
    atomic_store(&gEnded, true);
    CGPoint cg = akToCG(p);
    say(@"DELEGATE %s: ended at CG(%.0f,%.0f) operation=%@", api, cg.x, cg.y, opName(op));
}

@interface SrcPanel : NSPanel
@end
@implementation SrcPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface SrcView : NSView <NSDraggingSource, NSFilePromiseProviderDelegate>
@property (nonatomic, strong) NSFilePromiseProvider *provider;   // provider.delegate is WEAK: keep both alive
@property (nonatomic, strong) NSOperationQueue *promiseQueue;
- (void)beginDragAtWindowPoint:(NSPoint)wp;
- (void)startSessionWithEvent:(NSEvent *)ev;
- (void)logStartFor:(NSEvent *)ev what:(const char *)what;
@end

@implementation SrcView

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _promiseQueue = [[NSOperationQueue alloc] init];
        _promiseQueue.name = @"dragspike.promise";
        _promiseQueue.maxConcurrentOperationCount = 1;
    }
    return self;
}

- (BOOL)isOpaque { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    // Alpha must be > 0 or the window server lets clicks fall through to the window below.
    [[NSColor colorWithSRGBRed:1 green:0.45 blue:0.1 alpha:gAlpha] setFill];
    NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
}

- (void)mouseDown:(NSEvent *)e {
    atomic_store(&gGotDown, true);
    say(@"source view received the REAL mouseDown NSEvent (window loc %.0f,%.0f, pressedMouseButtons=0x%lx)",
        e.locationInWindow.x, e.locationInWindow.y, (unsigned long)NSEvent.pressedMouseButtons);
    if (gRealEvent) [self startSessionWithEvent:e];
}

- (void)mouseUp:(NSEvent *)e {
    (void)e;
    say(@"source view received mouseUp (no drag session was consuming it)");
}

- (void)beginDragAtWindowPoint:(NSPoint)wp {
    NSEvent *ev = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
                                     location:wp
                                modifierFlags:0
                                    timestamp:NSProcessInfo.processInfo.systemUptime
                                 windowNumber:self.window.windowNumber
                                      context:nil
                                  eventNumber:0
                                   clickCount:1
                                     pressure:1.0f];
    [self startSessionWithEvent:ev];
}

- (void)logStartFor:(NSEvent *)ev what:(const char *)what {
    unsigned long pressed = (unsigned long)NSEvent.pressedMouseButtons;
    gPressedAtStart = pressed;
    bool hid = CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft);
    bool comb = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
    say(@"BEGIN SESSION via %s (%s event, type %ld). NSEvent.pressedMouseButtons=0x%lx (left %s) | CG left button HID=%d combined=%d | NSApp.isActive=%d | source view saw mouseDown=%d",
        what, gRealEvent ? "REAL mouseDown" : "synthetic LeftMouseDragged", (long)ev.type, pressed, (pressed & 1) ? "DOWN" : "UP",
        hid, comb, (int)NSApp.isActive, (int)atomic_load(&gGotDown));
    if (!(pressed & 1) && !hid && !comb) {
        say(@"WARNING: no left button is down at session start - the synthetic mouseDown did not register;");
        say(@"         the session will probably end immediately (Accessibility for the parent app?).");
    }
}

- (void)startSessionWithEvent:(NSEvent *)ev {
    [self logStartFor:ev what:"beginDraggingSessionWithItems: (NSFilePromiseProvider)"];
    self.provider = [[NSFilePromiseProvider alloc] initWithFileType:@"public.plain-text" delegate:self];
    NSDraggingItem *item = [[NSDraggingItem alloc] initWithPasteboardWriter:self.provider];
    [item setDraggingFrame:self.bounds contents:makeIcon()];
    @try {
        [self beginDraggingSessionWithItems:@[item] event:ev source:self];
    } @catch (NSException *ex) {
        say(@"beginDraggingSessionWithItems THREW %@: %@", ex.name, ex.reason);
    }
    say(@"beginDraggingSessionWithItems returned (willBegin already fired: %d)", (int)atomic_load(&gBegan));
}

#pragma mark NSDraggingSource

- (NSDragOperation)draggingSession:(NSDraggingSession *)s sourceOperationMaskForDraggingContext:(NSDraggingContext)c {
    (void)s; (void)c;
    return NSDragOperationCopy;
}

- (void)draggingSession:(NSDraggingSession *)s willBeginAtPoint:(NSPoint)p {
    s.animatesToStartingPositionsOnCancelOrFail = NO;
    noteBegan(p, "draggingSession:willBeginAtPoint:");
}

- (void)draggingSession:(NSDraggingSession *)s movedToPoint:(NSPoint)p {
    (void)s;
    noteMoved(p, "draggingSession:movedToPoint:");
}

- (void)draggingSession:(NSDraggingSession *)s endedAtPoint:(NSPoint)p operation:(NSDragOperation)op {
    (void)s;
    noteEnded(p, op, "draggingSession:endedAtPoint:operation:");
}

#pragma mark NSFilePromiseProviderDelegate

- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)p fileNameForType:(NSString *)type {
    (void)p;
    say(@"PROMISE fileNameForType:%@ -> %@", type, kDestName);
    return kDestName;
}

- (NSOperationQueue *)operationQueueForFilePromiseProvider:(NSFilePromiseProvider *)p {
    (void)p;
    return self.promiseQueue;
}

- (void)filePromiseProvider:(NSFilePromiseProvider *)p
          writePromiseToURL:(NSURL *)url
          completionHandler:(void (^)(NSError *_Nullable))done {
    (void)p;
    gPromiseT = nowSec();
    gPromisePath = url.path;
    gPromiseDir = url.URLByDeletingLastPathComponent.path;
    atomic_store(&gPromised, true);
    say(@"PROMISE writePromiseToURL: %@  => destination folder = %@  (queue: %s)",
        gPromisePath, gPromiseDir, NSThread.isMainThread ? "MAIN" : "background");
    // Simulate a late (async) delivery to prove the promise may be fulfilled after this callback returns.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSError *err = nil;
        BOOL ok = [kPayload writeToURL:url atomically:NO encoding:NSUTF8StringEncoding error:&err];
        if (!ok) gWriteErr = err.localizedDescription ?: @"unknown error";
        say(@"PROMISE fulfilled 300 ms later: write %@%@", ok ? @"ok" : @"FAILED: ", ok ? @"" : gWriteErr);
        atomic_store(&gWritten, ok);
        done(ok ? nil : err);
    });
}

@end

// --legacy: the pre-10.13 technique the old Deskflow used (dragPromisedFilesOfTypes: + namesOfPromisedFilesDroppedAtDestination:).
// A separate subclass so the legacy selectors can never influence the default (modern) session.
@interface LegacyView : SrcView
@end

@implementation LegacyView

- (void)startSessionWithEvent:(NSEvent *)ev {
    [self logStartFor:ev what:"LEGACY dragPromisedFilesOfTypes:@[@\"txt\"]"];
    @try {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        BOOL ok = [self dragPromisedFilesOfTypes:@[@"txt"] fromRect:self.bounds source:self slideBack:NO event:ev];
#pragma clang diagnostic pop
        say(@"LEGACY dragPromisedFilesOfTypes returned %d (it blocks until the drag ends)", ok);
    } @catch (NSException *ex) {
        say(@"LEGACY dragPromisedFilesOfTypes THREW %@: %@", ex.name, ex.reason);
    }
}

- (NSArray<NSString *> *)namesOfPromisedFilesDroppedAtDestination:(NSURL *)dest {
    gPromiseT = nowSec();
    NSURL *url = [dest URLByAppendingPathComponent:kDestName];
    gPromiseDir = dest.path;
    gPromisePath = url.path;
    atomic_store(&gPromised, true);
    say(@"LEGACY namesOfPromisedFilesDroppedAtDestination: %@  => destination folder = %@  (thread: %s)",
        dest, dest.path, NSThread.isMainThread ? "MAIN" : "background");
    NSError *err = nil;
    BOOL ok = [kPayload writeToURL:url atomically:NO encoding:NSUTF8StringEncoding error:&err];
    if (!ok) gWriteErr = err.localizedDescription ?: @"unknown error";
    say(@"LEGACY promise fulfilled synchronously: write %@%@", ok ? @"ok" : @"FAILED: ", ok ? @"" : gWriteErr);
    atomic_store(&gWritten, ok);
    return ok ? @[kDestName] : nil;
}

- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)local {
    (void)local;
    return NSDragOperationCopy;
}

- (void)draggedImage:(NSImage *)img beganAt:(NSPoint)p {
    (void)img;
    noteBegan(p, "LEGACY draggedImage:beganAt:");
}

- (void)draggedImage:(NSImage *)img movedTo:(NSPoint)p {
    (void)img;
    noteMoved(p, "LEGACY draggedImage:movedTo:");
}

- (void)draggedImage:(NSImage *)img endedAt:(NSPoint)p operation:(NSDragOperation)op {
    (void)img;
    noteEnded(p, op, "LEGACY draggedImage:endedAt:operation:");
}

@end

#pragma mark - orchestration

@interface Runner : NSObject <NSApplicationDelegate>
@property (nonatomic) BOOL cancelMode;
@end

@implementation Runner {
    SrcPanel *_panel;
    SrcView *_view;
    CGPoint _start, _target;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    (void)n;
    if (self.cancelMode) [self cancelStart];
    else [self dropStart];
}

- (void)buildPanelAt:(CGPoint)cg activating:(BOOL)activating {
    NSPoint a = cgToAK(cg);
    NSRect r = NSMakeRect(a.x - kSize / 2.0, a.y - kSize / 2.0, kSize, kSize);
    NSWindowStyleMask mask = NSWindowStyleMaskBorderless;
    if (!activating) mask |= NSWindowStyleMaskNonactivatingPanel;
    _panel = [[SrcPanel alloc] initWithContentRect:r styleMask:mask backing:NSBackingStoreBuffered defer:NO];
    _panel.releasedWhenClosed = NO;
    _panel.hidesOnDeactivate = NO;   // NSPanel default is YES: it would vanish because our app is never active
    _panel.opaque = NO;
    _panel.backgroundColor = NSColor.clearColor;
    _panel.hasShadow = NO;
    _panel.level = NSStatusWindowLevel;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    _view = [(gLegacy ? [LegacyView alloc] : [SrcView alloc]) initWithFrame:NSMakeRect(0, 0, kSize, kSize)];
    _panel.contentView = _view;
    [_panel orderFrontRegardless];
    say(@"source window %dx%d at CG(%.0f,%.0f) (%s, alpha %.2f) frame AK %@",
        (int)kSize, (int)kSize, cg.x, cg.y, activating ? "activating" : "non-activating", gAlpha, NSStringFromRect(r));
}

- (void)beginSession {
    if (gRealEvent) {
        say(@"t=+300ms  (--real-event) session is started from the real mouseDown: handler; began so far: %d", (int)atomic_load(&gBegan));
        return;
    }
    say(@"t=+300ms  starting the session with a synthetic LeftMouseDragged NSEvent");
    [_view beginDragAtWindowPoint:[_panel convertPointFromScreen:cgToAK(_start)]];
}

#pragma mark drop mode

- (void)dropStart {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSError *err = nil;
    [fm removeItemAtPath:kDestDir error:NULL];   // recreate (not just empty) so Finder must open a NEW window
    if (![fm createDirectoryAtPath:kDestDir withIntermediateDirectories:YES attributes:nil error:&err]) {
        say(@"ERROR: cannot create %@: %@", kDestDir, err.localizedDescription);
        finish(1);
    }
    NSSet<NSNumber *> *before = [NSSet setWithArray:[finderWindows() valueForKey:CGKEY(kCGWindowNumber)]];
    say(@"destination %@ recreated empty; Finder windows before open: %lu", kDestDir, (unsigned long)before.count);
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:kDestDir isDirectory:YES]
                           configuration:[NSWorkspaceOpenConfiguration configuration]
                       completionHandler:^(NSRunningApplication *app, NSError *e) {
        (void)app;
        say(@"NSWorkspace open finished: %@", e ? e.localizedDescription : @"ok");
    }];
    say(@"opened %@ in Finder; waiting 1.5 s for the window ...", kDestDir);
    afterMs(1500, ^{ [self findWindow:before attempt:0]; });
}

- (void)findWindow:(NSSet<NSNumber *> *)before attempt:(int)attempt {
    NSArray<NSDictionary *> *all = finderWindows();
    NSDictionary *pick = nil;
    for (NSDictionary *w in all) {
        if (![before containsObject:w[CGKEY(kCGWindowNumber)]]) { pick = w; break; }
    }
    if (!pick && attempt < 4) {
        afterMs(500, ^{ [self findWindow:before attempt:attempt + 1]; });
        return;
    }
    if (!pick && all.count) {
        pick = all.firstObject;
        say(@"WARNING: no NEW Finder window appeared (Finder reused an existing one?). Using the front-most Finder");
        say(@"         window id=%@. If it is not the %@ window the drop lands elsewhere (verdict will say so).",
            pick[CGKEY(kCGWindowNumber)], kDestDir);
    }
    if (!pick) {
        say(@"ERROR: no Finder window found on screen after opening %@ (other Space / full-screen app / Finder hidden?).", kDestDir);
        say(@"       Close Finder windows on that folder, make sure a Finder window can appear on this Space, and retry.");
        finish(2);
    }
    CGRect b = boundsOf(pick);
    // ~62% across (right of the sidebar), ~55% down (below the toolbar): empty content area of the folder view.
    _target = clampToScreen(CGPointMake(CGRectGetMinX(b) + b.size.width * 0.62, CGRectGetMinY(b) + b.size.height * 0.55), 30);
    _start = clampToScreen(CGPointMake(_target.x - 150, _target.y), 30);
    say(@"Finder window id=%@ bounds CG(%.0f,%.0f %.0fx%.0f)", pick[CGKEY(kCGWindowNumber)], b.origin.x, b.origin.y, b.size.width, b.size.height);
    say(@"TARGET (drop point) CG(%.0f,%.0f)   CURSOR START CG(%.0f,%.0f)   original cursor CG(%.0f,%.0f)",
        _target.x, _target.y, _start.x, _start.y, gOrigCursor.x, gOrigCursor.y);
    [self buildPanelAt:_start activating:gActivate];
    afterMs(300, ^{ [self dropSequence]; });
}

- (void)dropSequence {
    CGPoint s = _start, t = _target;
    say(@"t=0       warp cursor to start + associate mouse/cursor");
    CGWarpMouseCursorPosition(s);
    CGAssociateMouseAndMouseCursorPosition(true);
    afterMs(140, ^{ noteCursor(s, 4.0, @"after warp"); });
    afterMs(150, ^{
        say(@"t=+150ms  post LeftMouseDown at start via kCGHIDEventTap");
        postMouse(kCGEventLeftMouseDown, s, s);
    });
    afterMs(300, ^{ [self beginSession]; });
    afterMs(400, ^{
        say(@"t=+400ms  posting %d LeftMouseDragged events, 16 ms apart, start -> target", (int)kSteps);
        __block int k = 0;
        __block CGPoint prev = s;
        every(16, ^BOOL{
            k++;
            if (k == 2 || k == kSteps / 2 || k == kSteps) noteCursor(prev, 12.0, [NSString stringWithFormat:@"before drag #%d, run loop mode %@", k, NSRunLoop.currentRunLoop.currentMode]);
            CGFloat f = (CGFloat)k / kSteps;
            CGPoint p = CGPointMake(s.x + (t.x - s.x) * f, s.y + (t.y - s.y) * f);
            postMouse(kCGEventLeftMouseDragged, p, prev);
            prev = p;
            if (k < kSteps) return YES;
            say(@"arrived at target CG(%.0f,%.0f); holding 300 ms", t.x, t.y);
            afterMs(300, ^{ [self dropRelease:t]; });
            return NO;
        });
    });
}

- (void)dropRelease:(CGPoint)t {
    gUpT = nowSec();
    say(@"post LeftMouseUp at target -> the drop happens now. Waiting up to 5 s for callbacks ...");
    postMouse(kCGEventLeftMouseUp, t, t);
    double t0 = nowSec();
    __block double doneAt = 0;
    every(100, ^BOOL{
        if (atomic_load(&gWritten) && doneAt == 0) doneAt = nowSec();
        if ((doneAt != 0 && nowSec() - doneAt > 0.3) || nowSec() - t0 >= 5.0) {
            [self dropFinish];
            return NO;
        }
        return YES;
    });
}

- (void)dropFinish {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *expect = [kDestDir stringByAppendingPathComponent:kDestName];
    NSString *content = [NSString stringWithContentsOfFile:expect encoding:NSUTF8StringEncoding error:NULL];
    BOOL exists = [fm fileExistsAtPath:expect];
    BOOL pass = [content isEqualToString:kPayload];
    BOOL began = atomic_load(&gBegan), ended = atomic_load(&gEnded), promised = atomic_load(&gPromised);
    BOOL early = ended && (gUpT == 0 || gEndT < gUpT);
    BOOL dirOk = promised && [gPromiseDir.stringByResolvingSymlinksInPath isEqualToString:kDestDir.stringByResolvingSymlinksInPath];

    printf("\n================ RESULT: drop ================\n");
    printf("(a) synthetic drag session started ........ %s", began ? "YES" : "NO");
    if (began) printf(" (willBegin at %.0f ms on the log clock)", (gBeganT - gT0) * 1000.0);
    printf("\n    NSEvent.pressedMouseButtons at session start: ");
    if (gPressedAtStart == ~0ul) printf("(session start never reached)\n");
    else printf("0x%lx (left button %s)\n", gPressedAtStart, (gPressedAtStart & 1) ? "DOWN" : "UP");
    printf("    session ended ............................. %s", ended ? "YES" : "NO");
    if (ended) printf(", operation=%s", opName(atomic_load(&gEndOp)).UTF8String);
    printf("\n    ended BEFORE the mouse-up (early end) ..... %s%s\n", early ? "YES" : "NO",
           early ? "  <-- drag did not survive; see DELEGATE lines above for when" : "");
    printf("    movedToPoint callbacks .................... %d\n", (int)atomic_load(&gMoved));
    printf("    source view saw real mouseDown ............ %s\n", atomic_load(&gGotDown) ? "yes" : "NO");
    printf("    cursor-follow mismatches .................. %d\n", (int)atomic_load(&gCursorBad));
    printf("(b) Finder asked for the promise .............. %s\n", promised ? "YES" : "NO");
    if (promised) {
        printf("    promise URL ............................... %s\n", gPromisePath.UTF8String);
        printf("    DESTINATION FOLDER ........................ %s  (expected %s: %s)\n", gPromiseDir.UTF8String,
               kDestDir.stringByResolvingSymlinksInPath.UTF8String, dirOk ? "MATCH" : "DIFFERENT");
        printf("    promise written by us ..................... %s%s\n", atomic_load(&gWritten) ? "yes" : "NO",
               gWriteErr ? [NSString stringWithFormat:@" (%@)", gWriteErr].UTF8String : "");
    }
    printf("    %s exists: %s, contents %s\n", expect.UTF8String, exists ? "yes" : "NO", pass ? "OK" : (exists ? "WRONG" : "n/a"));
    if (promised && !dirOk && gPromisePath && [fm removeItemAtPath:gPromisePath error:NULL])
        printf("    removed stray file the drop wrote outside the test folder: %s\n", gPromisePath.UTF8String);
    if (promised && !dirOk && pass)
        printf("    NOTE: PASS although the promise URL folder differs - Finder staged the promise elsewhere and moved it in,\n"
               "          so the promise URL is NOT the drop folder (design finding: the destination cannot be read from it).\n");
    printf("VERDICT: %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        if (!began) printf("  hint: session never began - check the WARNING lines about the mouse button / Accessibility.\n");
        else if (early) printf("  hint: session ended early - the synthetic button state did not keep AppKit's drag alive.\n");
        else if (!promised) printf("  hint: session survived but Finder never requested the promise - drop target/point or Finder refused it.\n");
    }
    printf("DONE - you may touch the mouse again.\n");
    fflush(stdout);
    finish(pass ? 0 : 1);
}

#pragma mark cancel mode

- (void)cancelStart {
    CGPoint cur = cursorNow();
    _start = clampToScreen(cur, 60);
    say(@"cancel mode: session at the current cursor CG(%.0f,%.0f)%s", _start.x, _start.y,
        hypot(_start.x - cur.x, _start.y - cur.y) > 1 ? " (moved in from a screen edge)" : "");
    [self buildPanelAt:_start activating:YES];   // activating: the synthetic click makes us frontmost, so Esc reaches us
    afterMs(300, ^{ [self cancelSequence]; });
}

- (void)cancelSequence {
    CGPoint s = _start;
    say(@"t=0       place cursor at start + associate; NSApp activate");
    [NSApp activate];
    CGWarpMouseCursorPosition(s);
    CGAssociateMouseAndMouseCursorPosition(true);
    afterMs(150, ^{
        say(@"t=+150ms  post LeftMouseDown at start via kCGHIDEventTap");
        postMouse(kCGEventLeftMouseDown, s, s);
    });
    afterMs(300, ^{ [self beginSession]; });
    afterMs(900, ^{ [self cancelEsc]; });
}

- (void)cancelEsc {
    NSRunningApplication *fa = NSWorkspace.sharedWorkspace.frontmostApplication;
    BOOL us = fa.processIdentifier == getpid();
    say(@"t=+900ms  frontmost app: %@ (pid %d) %s | NSApp.isActive=%d | session began=%d ended=%d",
        fa.localizedName, fa.processIdentifier, us ? "= dragspike" : "!= dragspike, Esc via HID goes THERE",
        (int)NSApp.isActive, (int)atomic_load(&gBegan), (int)atomic_load(&gEnded));
    if (atomic_load(&gEnded)) say(@"WARNING: the session ended BEFORE Esc - test inconclusive (the drag never stuck).");
    gEscT = nowSec();
    say(@"post Esc (keycode %d) down+up via CGEventPost(kCGHIDEventTap); waiting up to 2 s for endedAtPoint", (int)kEscKey);
    postKey(kEscKey, true, 0);
    afterMs(40, ^{ postKey(kEscKey, false, 0); });
    __block BOOL fellBack = NO;
    every(20, ^BOOL{
        if (atomic_load(&gEnded)) { [self cancelRelease]; return NO; }
        if (!fellBack && msSince(gEscT) >= 2000) {
            fellBack = YES;
            gFbT = nowSec();
            say(@"session NOT ended 2 s after the HID-tap Esc -> extra experiment: Esc via CGEventPostToPid(self)");
            postKey(kEscKey, true, getpid());
            afterMs(40, ^{ postKey(kEscKey, false, getpid()); });
        }
        if (fellBack && msSince(gFbT) >= 1500) { [self cancelRelease]; return NO; }
        return YES;
    });
}

- (void)cancelRelease {
    gUpT = nowSec();
    CGPoint here = cursorNow();
    say(@"post LeftMouseUp to release the synthetic button");
    postMouse(kCGEventLeftMouseUp, here, here);
    afterMs(300, ^{ [self cancelVerdict]; });
}

- (void)cancelVerdict {
    BOOL ended = atomic_load(&gEnded);
    unsigned long op = atomic_load(&gEndOp);
    NSString *why;
    BOOL pass = NO;
    if (!ended) why = @"session never ended: neither the HID-tap Esc nor the pid-targeted Esc cancelled it";
    else if (gEndT < gEscT) why = @"session ended BEFORE Esc was posted (inconclusive: the synthetic drag never stuck)";
    else if (gUpT != 0 && gEndT >= gUpT) why = @"session only ended when the mouse button was released - Esc had NO effect";
    else if (gFbT != 0 && gEndT >= gFbT) why = @"HID-tap Esc had NO effect; only the pid-targeted fallback Esc (CGEventPostToPid) ended it";
    else if (op != NSDragOperationNone) why = [NSString stringWithFormat:@"ended after Esc but operation was %@, expected None", opName(op)];
    else { pass = YES; why = [NSString stringWithFormat:@"HID-tap Esc ended the session after %.0f ms with operation None", (gEndT - gEscT) * 1000.0]; }
    printf("\n================ RESULT: cancel ================\n");
    printf("session began: %s | ended: %s", atomic_load(&gBegan) ? "yes" : "NO", ended ? "yes" : "NO");
    if (ended) printf(" (operation %s)", opName(op).UTF8String);
    printf("\nNSEvent.pressedMouseButtons at session start: 0x%lx\n", gPressedAtStart == ~0ul ? 0ul : gPressedAtStart);
    printf("%s: %s\n", pass ? "PASS" : "FAIL", why.UTF8String);
    printf("DONE - you may touch the mouse again.\n");
    fflush(stdout);
    finish(pass ? 0 : 1);
}

@end

#pragma mark - pasteboard mode (read-only)

static int runPasteboard(int seconds) {
    NSPasteboard *pb = [NSPasteboard pasteboardWithName:NSPasteboardNameDrag];
    printf("watching the DRAG pasteboard for %d s (poll 200 ms). Now drag a file around in Finder.\n", seconds);
    printf("Lines print on any change and once a second. NOTE: the drag pasteboard keeps the LAST drag's\n");
    printf("contents after the drag ends; a NEW drag shows as a changeCount bump, ideally with leftButton=DOWN.\n");
    NSInteger lastCC = -1, ccAtDown = -1;
    int lastDown = -1;
    BOOL armed = NO;
    int ticks = seconds * 5;
    for (int i = 0; i < ticks; i++) {
        @autoreleasepool {
            NSInteger cc = pb.changeCount;
            int down = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft) ? 1 : 0;
            if (down && lastDown != 1) { ccAtDown = lastCC != -1 ? lastCC : cc; armed = YES; }
            if (!down) armed = NO;
            BOOL changed = (lastCC != -1 && cc != lastCC);
            NSArray<NSString *> *types = pb.types ?: @[];
            BOOL hasFileURL = [types containsObject:@"public.file-url"];
            if (changed || down != lastDown || i % 5 == 0) {
                printf("[%5.1f s] changeCount=%ld%s leftButton=%s types=[%s]%s%s\n", i * 0.2, (long)cc,
                       changed ? " CHANGED" : "", down ? "DOWN" : "up", [types componentsJoinedByString:@" "].UTF8String,
                       hasFileURL ? " FILE-URL" : "",
                       [types containsObject:@"com.apple.pasteboard.promised-file-url"] ? " FILE-PROMISE" : "");
                if (changed) {
                    for (NSPasteboardItem *it in pb.pasteboardItems) {
                        if (![it.types containsObject:@"public.file-url"]) continue;
                        NSString *s = [it stringForType:@"public.file-url"];
                        NSURL *u = s ? [NSURL URLWithString:s] : nil;
                        printf("          public.file-url item: %s\n", (u.path ?: s ?: @"(unreadable)").UTF8String);
                    }
                }
                fflush(stdout);
            }
            // the design's source-detection rule: LMB down remembers changeCount; a later change WITH public.file-url = file drag
            if (armed && cc != ccAtDown && hasFileURL) {
                printf("          >>> FILE DRAG DETECTED (left button down, changeCount %ld -> %ld, public.file-url present)\n",
                       (long)ccAtDown, (long)cc);
                fflush(stdout);
                armed = NO;
            }
            lastCC = cc;
            lastDown = down;
        }
        if (i + 1 < ticks) usleep(200 * 1000);
    }
    printf("done.\n");
    return 0;
}

#pragma mark - check mode (read-only preflight)

static int runCheck(void) {
    bool ax = AXIsProcessTrusted(), pe = CGPreflightPostEventAccess();
    printf("macOS %s\n", NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
    printf("AXIsProcessTrusted ............ %s\n", ax ? "YES" : "NO");
    printf("CGPreflightPostEventAccess .... %s\n", pe ? "YES" : "NO");
    printf("=> synthetic input %s\n", (ax || pe) ? "should work"
           : "will be silently DROPPED: grant Accessibility to the app running this shell (Terminal/iTerm2/...), then restart that app");
    CGDirectDisplayID ids[8];
    uint32_t n = 0;
    CGGetActiveDisplayList(8, ids, &n);
    for (uint32_t i = 0; i < n; i++) {
        CGRect b = CGDisplayBounds(ids[i]);
        printf("display %u%s: CG(%.0f,%.0f %.0fx%.0f)\n", ids[i], CGDisplayIsMain(ids[i]) ? " (main)" : "", b.origin.x, b.origin.y, b.size.width, b.size.height);
    }
    CGPoint c = cursorNow();
    NSRunningApplication *f = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.finder"].firstObject;
    printf("cursor now: CG(%.0f,%.0f), left button %s\n", c.x, c.y,
           CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft) ? "DOWN" : "up");
    printf("Finder: %s (pid %d), on-screen Finder windows >=300x200: %lu\n", f ? "running" : "NOT running",
           f ? f.processIdentifier : 0, (unsigned long)finderWindows().count);
    printf("frontmost app: %s\n", NSWorkspace.sharedWorkspace.frontmostApplication.localizedName.UTF8String);
    return (ax || pe) ? 0 : 3;
}

#pragma mark - main

static int usage(FILE *f, int code) {
    fprintf(f,
        "dragspike - macOS drag-and-drop feasibility spike (see README.txt)\n\n"
        "usage:\n"
        "  dragspike drop [opts]            synthesized drag of a file PROMISE into a NEW Finder window,\n"
        "                                   PASS iff Finder hands us the folder and the file lands in %s\n"
        "  dragspike cancel [opts]          start a synthetic drag in place, cancel it with a synthesized Esc\n"
        "  dragspike pasteboard [seconds]   read-only: watch the drag pasteboard (default 20, max 20)\n"
        "  dragspike check                  read-only: Accessibility status, displays, Finder, cursor\n"
        "  dragspike --help\n\n"
        "drop/cancel MOVE THE REAL CURSOR and post REAL input events. They print a 3 s countdown, then run\n"
        "for at most ~15 s (hard watchdog 18 s). Needs Accessibility for the app you launch it from.\n"
        "Ctrl-C / SIGTERM release the synthetic mouse button and restore the cursor (never use kill -9).\n"
        "opts (drop/cancel), all still CGEventPost-only, for isolating a FAIL:\n"
        "  --invisible   source window alpha 0.01 instead of 0.25 (a truly transparent source window)\n"
        "  --real-event  start the session from the real mouseDown: NSEvent instead of a synthetic one\n"
        "  --activate    make the drop-mode source window activating (cancel mode always is)\n"
        "  --legacy      old technique: dragPromisedFilesOfTypes: + namesOfPromisedFilesDroppedAtDestination:\n"
        "env DRAGSPIKE_IGNORE_TRUST=1: skip the Accessibility pre-check\n"
        "exit codes: 0 PASS, 1 FAIL, 2 no Finder window, 3 no permission to post events, 4 watchdog,\n"
        "            5 uncaught exception, 64 usage\n", kDestDir.UTF8String);
    return code;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IONBF, 0);
        gT0 = nowSec();
        if (argc < 2) return usage(stderr, 64);
        NSString *mode = @(argv[1]);
        if ([mode isEqualToString:@"--help"] || [mode isEqualToString:@"-h"] || [mode isEqualToString:@"help"]) return usage(stdout, 0);

        if ([mode isEqualToString:@"check"]) return runCheck();

        if ([mode isEqualToString:@"pasteboard"]) {
            int secs = argc > 2 ? atoi(argv[2]) : 20;
            if (secs < 1 || secs > 20) return usage(stderr, 64);
            installSafety(MIN(secs + 0.5, 20.0));
            return runPasteboard(secs);
        }

        BOOL cancel = [mode isEqualToString:@"cancel"];
        if (!cancel && ![mode isEqualToString:@"drop"]) return usage(stderr, 64);
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--invisible") == 0) gAlpha = 0.01;
            else if (strcmp(argv[i], "--real-event") == 0) gRealEvent = YES;
            else if (strcmp(argv[i], "--activate") == 0) gActivate = YES;
            else if (strcmp(argv[i], "--legacy") == 0) gLegacy = YES;
            else return usage(stderr, 64);
        }

        installSafety(18.0);
        gSrc = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        requireTrust();
        countdown(argv[1]);
        gOrigCursor = cursorNow();
        atomic_store(&gHaveOrig, true);
        say(@"macOS %@ | original cursor CG(%.0f,%.0f) | mode=%@ | invisible=%d real-event=%d activate=%d legacy=%d",
            NSProcessInfo.processInfo.operatingSystemVersionString, gOrigCursor.x, gOrigCursor.y, mode,
            gAlpha < 0.05, (int)gRealEvent, (int)gActivate, (int)gLegacy);

        // no App Nap / timer coalescing: the 16 ms drag cadence must not be stretched
        gActivity = [NSProcessInfo.processInfo beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                                                                 reason:@"dragspike synthesized input"];
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        static Runner *runner;   // NSApp.delegate is unretained
        runner = [[Runner alloc] init];
        runner.cancelMode = cancel;
        NSApp.delegate = runner;
        [NSApp run];
    }
    return 0;
}
