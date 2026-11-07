// Small helpers: strings, URLs, dates, base64, running work on several threads.
#pragma once
#include <atomic>
#include <cstddef>
#include <ctime>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace mt {

// ---- strings ----
std::string to_lower(std::string s);
std::string trim(const std::string& s);
std::string collapse_ws(const std::string& s);          // runs of whitespace -> one space
std::vector<std::string> split_words(const std::string& s);
int word_count(const std::string& s);
std::string limit_words(const std::string& s, int max_words);
std::string html_escape(const std::string& s);
std::string html_unescape(const std::string& s);   // &amp; &lt; &gt; &quot; &#39; &#NNN; &nbsp;
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
bool contains(const std::string& s, const std::string& part);
std::vector<std::string> split_sentences(const std::string& text);

// Lowercase words for similarity: no stopwords, no 1-2 letter words (numbers of 2+ digits are kept).
std::vector<std::string> tokenize(const std::string& text);

// ---- URLs ----
std::string host_of(const std::string& url);            // "https://www.a.com/x" -> "a.com"
bool is_http_url(const std::string& url);
std::string url_encode(const std::string& s);
std::string base64url_decode(const std::string& s);     // tolerant of missing padding
std::string strip_publisher_suffix(const std::string& title);  // "Headline - The Hindu" -> "Headline"

// ---- time (all UTC unless noted) ----
std::time_t parse_rfc822(const std::string& s);          // "Wed, 05 Nov 2026 10:00:00 GMT"; 0 on failure
std::string format_iso(std::time_t t);                   // "2026-11-05T10:00:00Z"
std::string today_ist();                                 // YYYY-MM-DD in India time
std::string format_date_long(const std::string& ymd);    // "Thursday, 5 November 2026"
bool valid_date(const std::string& ymd);

// ---- environment files ----
// Reads KEY=VALUE lines (# comments, optional quotes, optional "export ") and sets each one as an
// environment variable unless it is already set. Returns how many were set; 0 if the file is missing.
int load_env_file(const std::string& path);

// ---- threads ----
// Runs fn(0..n-1) on up to `threads` threads. An exception in one call is swallowed (the
// caller's fn should record its own failures).
template <class F>
void parallel_for(std::size_t n, std::size_t threads, F fn) {
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> pool;
  std::size_t count = threads < n ? threads : n;
  for (std::size_t t = 0; t < count; ++t) {
    pool.emplace_back([&] {
      for (;;) {
        std::size_t i = next.fetch_add(1);
        if (i >= n) break;
        try {
          fn(i);
        } catch (const std::exception&) {
        }
      }
    });
  }
  for (auto& th : pool) th.join();
}

}  // namespace mt
