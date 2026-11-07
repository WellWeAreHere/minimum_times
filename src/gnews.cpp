#include "gnews.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <regex>
#include <thread>

#include "http.hpp"
#include "util.hpp"

namespace mt {

std::string gnews_article_id(const std::string& url) {
  if (host_of(url) != "news.google.com") return "";
  for (const char* marker : {"/articles/", "/read/"}) {
    auto p = url.find(marker);
    if (p == std::string::npos) continue;
    std::size_t b = p + std::string(marker).size();
    std::size_t e = url.find_first_of("?#/", b);
    return url.substr(b, e == std::string::npos ? std::string::npos : e - b);
  }
  return "";
}

std::optional<std::string> gnews_decode_offline(const std::string& id) {
  std::string bytes = base64url_decode(id);
  const std::string prefix("\x08\x13\x22", 3);
  const std::string suffix("\xd2\x01\x00", 3);
  if (starts_with(bytes, prefix)) bytes = bytes.substr(prefix.size());
  if (ends_with(bytes, suffix)) bytes = bytes.substr(0, bytes.size() - suffix.size());
  if (bytes.empty()) return std::nullopt;

  // The first byte (or two, for long values) says how long the URL is.
  std::size_t length = static_cast<unsigned char>(bytes[0]);
  std::size_t start = 1;
  if (length >= 0x80) start = 2;
  if (start > bytes.size() || length + 1 > bytes.size()) return std::nullopt;
  std::string value = bytes.substr(start, length + 1 - start);

  if (starts_with(value, "AU_yqL")) return std::nullopt;  // new style: needs Google's help
  if (!is_http_url(value)) return std::nullopt;
  return value;
}

namespace {

// Pulls the two values Google's page hands out for decoding.
bool decoding_params(const std::string& id, std::string& signature, std::string& timestamp,
                     std::string& error) {
  static const std::regex sg(R"re(data-n-a-sg="([^"]*)")re");
  static const std::regex ts(R"re(data-n-a-ts="([^"]*)")re");
  HttpOptions opt;
  opt.timeout_seconds = 15;
  opt.headers = {"Accept-Language: en-US,en;q=0.9"};
  for (const char* base : {"https://news.google.com/articles/", "https://news.google.com/rss/articles/"}) {
    HttpResponse r = http_get(std::string(base) + id, opt);
    if (r.status == 429) {  // rate limited: wait once and retry
      std::this_thread::sleep_for(std::chrono::seconds(3));
      r = http_get(std::string(base) + id, opt);
    }
    if (!r.ok()) {
      error = r.status ? "google_http_" + std::to_string(r.status) : "google_" + r.error;
      continue;
    }
    std::smatch a, b;
    if (std::regex_search(r.body, a, sg) && std::regex_search(r.body, b, ts)) {
      signature = a[1];
      timestamp = b[1];
      return true;
    }
    error = "google_page_has_no_signature";
  }
  return false;
}

}  // namespace

std::optional<std::string> gnews_resolve(const std::string& url, std::string& error) {
  std::string id = gnews_article_id(url);
  if (id.empty()) {
    if (is_http_url(url) && host_of(url) != "news.google.com") return url;  // already direct
    error = "not_a_google_news_link";
    return std::nullopt;
  }
  if (auto direct = gnews_decode_offline(id)) return direct;

  std::string signature, timestamp;
  if (!decoding_params(id, signature, timestamp, error)) return std::nullopt;

  // Ask Google's internal "batchexecute" endpoint which publisher URL this id points to.
  using nlohmann::json;
  std::string inner =
      R"(["garturlreq",[["X","X",["X","X"],null,null,1,1,"US:en",null,1,null,null,null,null,null,0,1],"X","X",1,[1,1,1],1,1,null,0,0,null,0],)" +
      json(id).dump() + "," + timestamp + "," + json(signature).dump() + "]";
  json request = json::array({json::array({json::array({"Fbv4je", inner, nullptr, "generic"})})});
  std::string body = "f.req=" + url_encode(request.dump());

  HttpOptions opt;
  opt.timeout_seconds = 15;
  opt.headers = {"Content-Type: application/x-www-form-urlencoded;charset=UTF-8"};
  HttpResponse r = http_post("https://news.google.com/_/DotsSplashUi/data/batchexecute", body, opt);
  if (!r.ok()) {
    error = r.status ? "batchexecute_http_" + std::to_string(r.status) : "batchexecute_" + r.error;
    return std::nullopt;
  }
  try {
    // The reply is some junk, a blank line, then a JSON document.
    auto split = r.body.find("\n\n");
    if (split == std::string::npos) throw std::runtime_error("no_separator");
    json outer = json::parse(r.body.substr(split + 2));
    json payload = json::parse(outer.at(0).at(2).get<std::string>());
    std::string decoded = payload.at(1).get<std::string>();
    if (!is_http_url(decoded)) throw std::runtime_error("not_a_url");
    return decoded;
  } catch (const std::exception& e) {
    error = std::string("batchexecute_parse_failed:") + e.what();
    return std::nullopt;
  }
}

}  // namespace mt
