#include "platform/http_request.h"

#import <Foundation/Foundation.h>

#include <cstdio>

namespace sw {

// One GET = one NSURLSessionDataTask on the shared session, awaited on a semaphore. Synchronous by
// design (see http_request.h): the app owns the async/worker decision, this seam just moves bytes.
// NSURLSession's default config carries the OS credential/proxy set — the NSURLSession restatement of
// TiXL's HttpClientHandler{UseDefaultCredentials=true} (RequestUrl.cs:28). No custom headers, no body,
// GET only — byte-for-byte the same request TiXL issues (its Accept/apikey/Authorization lines are all
// commented out, RequestUrl.cs:38-42).
HttpResponse httpGet(const std::string& url, double timeoutSeconds) {
  HttpResponse out;
  @autoreleasepool {
    NSString* urlStr = [NSString stringWithUTF8String:url.c_str()];
    NSURL* nsurl = urlStr ? [NSURL URLWithString:urlStr] : nil;
    // Reject a malformed URL, and anything that isn't http(s) — a bare path / file:// is not a GET.
    if (!nsurl || !nsurl.scheme ||
        !([nsurl.scheme caseInsensitiveCompare:@"http"] == NSOrderedSame ||
          [nsurl.scheme caseInsensitiveCompare:@"https"] == NSOrderedSame)) {
      out.error = "invalid url";
      return out;
    }

    const double timeout = timeoutSeconds > 0.0 ? timeoutSeconds : 30.0;
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:nsurl];
    req.HTTPMethod = @"GET";
    req.timeoutInterval = timeout;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    // __block so the completion handler (runs on a session delegate queue) writes back into these.
    __block int status = 0;
    __block NSData* data = nil;
    __block NSString* errStr = nil;

    NSURLSessionDataTask* task = [[NSURLSession sharedSession]
        dataTaskWithRequest:req
          completionHandler:^(NSData* d, NSURLResponse* resp, NSError* err) {
            if (err) {
              errStr = err.localizedDescription ?: @"request failed";
            } else {
              data = d;
              if ([resp isKindOfClass:[NSHTTPURLResponse class]])
                status = (int)((NSHTTPURLResponse*)resp).statusCode;
            }
            dispatch_semaphore_signal(sem);
          }];
    [task resume];

    // Bound the wait so a hung connection cannot freeze the caller past its own timeout. Add a small
    // slack over the request timeout so NSURLSession's own timeout error path wins the race (giving us
    // a real error string) rather than this semaphore deadline firing first.
    dispatch_time_t deadline =
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)((timeout + 1.0) * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(sem, deadline) != 0) {
      [task cancel];
      out.error = "timeout";
      return out;
    }

    if (errStr) {
      out.error = errStr.UTF8String ? errStr.UTF8String : "request failed";
      return out;  // ok=false, status=0 (no HTTP response: DNS/refused/timeout — RequestUrl.cs:47-50)
    }

    // A response arrived (ok=true) EVEN for a non-2xx status — RequestUrl reads the body regardless of
    // status (RequestUrl.cs:44); the 2xx gate is the LoadImageFromUrl caller's job (cs:76), not here.
    out.ok = true;
    out.status = status;
    if (data && data.length > 0) {
      const uint8_t* bytes = (const uint8_t*)data.bytes;
      out.body.assign(bytes, bytes + data.length);
    }
  }
  return out;
}

}  // namespace sw
