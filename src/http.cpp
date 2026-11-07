#include "http.hpp"

#include <curl/curl.h>

#include <memory>

namespace mt {

namespace {

struct Sink {
  std::string* body;
  std::size_t max_bytes;
};

std::size_t write_cb(char* data, std::size_t size, std::size_t n, void* user) {
  auto* sink = static_cast<Sink*>(user);
  std::size_t bytes = size * n;
  if (sink->body->size() + bytes > sink->max_bytes) return 0;  // too big: abort the transfer
  sink->body->append(data, bytes);
  return bytes;
}

struct CurlDeleter {
  void operator()(CURL* c) const { curl_easy_cleanup(c); }
};
struct SlistDeleter {
  void operator()(curl_slist* l) const { curl_slist_free_all(l); }
};

HttpResponse perform(const std::string& url, const std::string* post_body, const HttpOptions& opt) {
  HttpResponse res;
  std::unique_ptr<CURL, CurlDeleter> h(curl_easy_init());
  if (!h) {
    res.error = "curl_easy_init failed";
    return res;
  }
  Sink sink{&res.body, opt.max_bytes};
  curl_slist* raw = nullptr;
  for (const auto& header : opt.headers) raw = curl_slist_append(raw, header.c_str());
  std::unique_ptr<curl_slist, SlistDeleter> headers(raw);

  curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h.get(), CURLOPT_MAXREDIRS, 8L);
  curl_easy_setopt(h.get(), CURLOPT_TIMEOUT, static_cast<long>(opt.timeout_seconds));
  curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(h.get(), CURLOPT_ACCEPT_ENCODING, "");  // let curl handle gzip/br
  curl_easy_setopt(h.get(), CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; MinimumTimes/1.0)");
  curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &sink);
  if (headers) curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, headers.get());
#ifdef _WIN32
  // Use the Windows certificate store so HTTPS works outside the MSYS2 shell too.
  curl_easy_setopt(h.get(), CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
  if (post_body) {
    curl_easy_setopt(h.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, post_body->c_str());
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
  }

  CURLcode code = curl_easy_perform(h.get());
  if (code != CURLE_OK) {
    res.error = curl_easy_strerror(code);
    return res;
  }
  curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &res.status);
  char* eff = nullptr;
  curl_easy_getinfo(h.get(), CURLINFO_EFFECTIVE_URL, &eff);
  if (eff) res.final_url = eff;
  char* ct = nullptr;
  curl_easy_getinfo(h.get(), CURLINFO_CONTENT_TYPE, &ct);
  if (ct) res.content_type = ct;
  return res;
}

}  // namespace

void http_global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void http_global_cleanup() { curl_global_cleanup(); }

HttpResponse http_get(const std::string& url, const HttpOptions& opt) {
  return perform(url, nullptr, opt);
}

HttpResponse http_post(const std::string& url, const std::string& body, const HttpOptions& opt) {
  return perform(url, &body, opt);
}

}  // namespace mt
