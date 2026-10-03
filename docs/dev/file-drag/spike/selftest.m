// selftest.m - SAFE headless checks of dragspike's non-input logic.
// Posts NO events, moves NO cursor, opens NO windows. It fulfils the file promise by calling the same
// delegate method Finder would call, into a private temp dir, and renders the drag icon offscreen.
// Build: clang -fobjc-arc -Wall -Wextra -framework AppKit -framework CoreGraphics selftest.m -o selftest && ./selftest
#pragma clang diagnostic ignored "-Wnullability-completeness"  // only because a .m is #included like a header
#define main dragspike_main
#include "dragspike.m"
#undef main

static int fails;
#define CHECK(c) do { if (c) printf("ok    %s\n", #c); else { printf("FAIL  %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

int main(void) {
    @autoreleasepool {
        gT0 = nowSec();

        CHECK([opName(NSDragOperationNone) hasPrefix:@"None"]);
        CHECK([opName(NSDragOperationCopy) hasPrefix:@"Copy"]);

        CGPoint p = CGPointMake(123, 456), q = akToCG(cgToAK(p));
        CHECK(q.x == p.x && q.y == p.y);

        CGImageRef icon = [makeIcon() CGImageForProposedRect:NULL context:nil hints:nil];
        CHECK(icon != NULL && CGImageGetWidth(icon) >= kSize && CGImageGetHeight(icon) >= kSize);

        SrcView *v = [[SrcView alloc] initWithFrame:NSMakeRect(0, 0, kSize, kSize)];
        NSFilePromiseProvider *prov = [[NSFilePromiseProvider alloc] initWithFileType:@"public.plain-text" delegate:v];
        CHECK([[v filePromiseProvider:prov fileNameForType:@"public.plain-text"] isEqualToString:kDestName]);
        CHECK([v operationQueueForFilePromiseProvider:prov] != nil);

        NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"dragspike-selftest-%d", getpid()]];
        [NSFileManager.defaultManager createDirectoryAtPath:tmp withIntermediateDirectories:YES attributes:nil error:NULL];
        NSURL *url = [NSURL fileURLWithPath:[tmp stringByAppendingPathComponent:kDestName]];
        __block BOOL fulfilled = NO;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [v filePromiseProvider:prov writePromiseToURL:url completionHandler:^(NSError *e) {
            fulfilled = (e == nil);
            dispatch_semaphore_signal(sem);
        }];
        CHECK(!atomic_load(&gWritten));
        CHECK(dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) == 0);
        CHECK(fulfilled);
        CHECK([[NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:NULL] isEqualToString:kPayload]);
        CHECK(atomic_load(&gPromised) && atomic_load(&gWritten));
        CHECK([gPromiseDir isEqualToString:tmp.stringByResolvingSymlinksInPath] || [gPromiseDir isEqualToString:tmp]);
        [NSFileManager.defaultManager removeItemAtPath:tmp error:NULL];

        CHECK(finderWindows() != nil);
        CHECK(!CGPointEqualToPoint(cursorNow(), CGPointMake(-1, -1)));

        printf(fails ? "SELFTEST FAILED (%d)\n" : "SELFTEST PASSED\n", fails);
        return fails ? 1 : 0;
    }
}
