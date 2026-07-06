// platform/http_request — synchronous HTTP GET behind a C++ pimpl (the native seam for the io/json
// RequestUrl node + the image/generate/load LoadImageFromUrl node). One blocking GET: give it a URL,
// get back {status code, body bytes}. No streaming, no custom headers, no auth beyond the OS default.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/json/RequestUrl.cs            — HttpClient GET, Result = Content.ReadAsStringAsync
//                                                    (RequestUrl.cs:28-45). Status NOT checked; body is
//                                                    the raw response string regardless of status.
//   Operators/Lib/image/generate/load/LoadImageFromUrl.cs — client.GetAsync(url), status IS checked
//                                                    (response.IsSuccessStatusCode, cs:76-94), body =
//                                                    ReadAsStreamAsync → WIC decode. We surface the raw
//                                                    bytes; the image decode rides sw's image_decode path.
// TiXL's HttpClient is a .NET BCL type; the transport here is NSURLSession. The MESSAGE SEMANTICS match:
// a plain HTTP/1.1 GET, no request body, the default credential set, and the whole response body handed
// back verbatim (RequestUrl.cs:28 sets UseDefaultCredentials=true; no Accept/apikey/Authorization — those
// lines are commented out at cs:38-42). Status-code POLICY is the CALLER's (RequestUrl ignores it; the
// LoadImageFromUrl node gates on 2xx) — this seam reports the code and lets the caller decide.
//
// platform leaf: ObjC (NSURLSession) behind a C++ pimpl — callers include no ObjC, no runtime/app/ui
// headers. Synchronous: the call blocks the calling thread on a semaphore until the response (or an
// error/timeout) lands, mirroring how the app's per-frame cook wants a settled result to route. TiXL's
// nodes are fire-and-forget async (async void); the app layer expresses that by kicking this GET onto a
// worker and latching the result into the next cook — the SYNC/ASYNC boundary is a named app-side fork,
// not a semantics difference in the bytes (see runtime/net_node_cook http seam).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// The outcome of one GET. `ok` is the transport verdict (a response arrived at all — NOT a 2xx check;
// a 404 with a body is ok=true, status=404). `status` is the HTTP status code (0 when no HTTP response
// was received: DNS failure, refused connection, timeout). `body` is the raw response bytes (UTF-8 text
// for RequestUrl; encoded image bytes for LoadImageFromUrl — this seam does not decode). `error` carries
// a short human string on transport failure (empty on success), for the app to log (mirrors TiXL's
// "Download failed:" log, RequestUrl.cs:49).
struct HttpResponse {
  bool ok = false;
  int status = 0;
  std::vector<uint8_t> body;
  std::string error;
};

// Blocking HTTP/1.1 GET of `url`. Uses the system default credential/proxy set (NSURLSession default
// config = TiXL UseDefaultCredentials=true). `timeoutSeconds` bounds the wait (<=0 → a default of 30s).
// Never throws / never crashes on a bad URL: a malformed or unreachable URL returns {ok=false, status=0,
// error=...}. Safe to call off the main thread (the app kicks it onto a worker to model TiXL's async).
HttpResponse httpGet(const std::string& url, double timeoutSeconds = 30.0);

// Blocking HTTP/1.1 POST of `body` to `url` with Content-Type `contentType`. Same NSURLSession
// transport, error, and status semantics as httpGet (a non-2xx with a body is ok=true,status=NNN; a
// transport failure is ok=false,status=0,error=...). The ONVIF seam (OnvifCamera.cs:696-736
// SendSoapRequestAsync) posts a SOAP envelope with Content-Type "application/soap+xml" — that whole
// request is BODY + method + content-type over the identical NSURLSession the GET already uses; this
// generalises the GET seam to carry a request body (TiXL's HttpClient.PostAsync with a StringContent).
// The real camera endpoint is deferred-hw-verify; the closed-form half (envelope assembly incl. the
// golden WS-Security digest) is verified against a canonical vector, independent of any device.
HttpResponse httpPost(const std::string& url, const std::string& body,
                      const std::string& contentType, double timeoutSeconds = 30.0);

}  // namespace sw
