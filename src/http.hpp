// Downloading things over HTTP(S) with libcurl.
#pragma once
#include <string>
#include <vector>

namespace mt {

struct HttpResponse {
  long status = 0;           // 0 means the request itself failed (see error)
  std::string body;
  std::string final_url;     // after redirects
  std::string content_type;
  std::string error;
  bool ok() const { return status >= 200 && status < 300; }
};

struct HttpOptions {
  int timeout_seconds = 15;
  std::vector<std::string> headers;  // each like "Accept: text/html"
  std::size_t max_bytes = 6 * 1024 * 1024;
};

// Call once at program start and once at the end.
void http_global_init();
void http_global_cleanup();

HttpResponse http_get(const std::string& url, const HttpOptions& opt = {});
HttpResponse http_post(const std::string& url, const std::string& body,
                       const HttpOptions& opt = {});

}  // namespace mt
