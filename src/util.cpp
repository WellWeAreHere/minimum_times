#include "util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <set>
#include <sstream>

namespace mt {

// ---------------------------------------------------------------- strings

std::string to_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string collapse_ws(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool space = false;
  for (unsigned char c : s) {
    // Treat the UTF-8 non-breaking space (C2 A0) as whitespace too.
    if (std::isspace(c)) {
      space = true;
    } else {
      if (space && !out.empty()) out.push_back(' ');
      space = false;
      out.push_back(static_cast<char>(c));
    }
  }
  // Replace NBSP sequences left behind.
  std::string::size_type p;
  while ((p = out.find("\xC2\xA0")) != std::string::npos) out.replace(p, 2, " ");
  return out;
}

std::vector<std::string> split_words(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

int word_count(const std::string& s) { return static_cast<int>(split_words(s).size()); }

std::string limit_words(const std::string& s, int max_words) {
  auto words = split_words(s);
  if (static_cast<int>(words.size()) <= max_words) return collapse_ws(s);
  std::string out;
  for (int i = 0; i < max_words; ++i) {
    if (i) out += ' ';
    out += words[i];
  }
  return out;
}

std::string html_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string html_unescape(const std::string& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') { out.push_back(s[i]); continue; }
    auto semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 8) { out.push_back(s[i]); continue; }
    std::string ent = s.substr(i + 1, semi - i - 1);
    std::string rep;
    if (ent == "amp") rep = "&";
    else if (ent == "lt") rep = "<";
    else if (ent == "gt") rep = ">";
    else if (ent == "quot") rep = "\"";
    else if (ent == "apos") rep = "'";
    else if (ent == "nbsp") rep = " ";
    else if (!ent.empty() && ent[0] == '#') {
      long code = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                      ? std::strtol(ent.c_str() + 2, nullptr, 16)
                      : std::strtol(ent.c_str() + 1, nullptr, 10);
      if (code > 0 && code < 0x80) rep = std::string(1, static_cast<char>(code));
      else if (code >= 0x80 && code < 0x800) {
        rep.push_back(static_cast<char>(0xC0 | (code >> 6)));
        rep.push_back(static_cast<char>(0x80 | (code & 0x3F)));
      } else if (code >= 0x800 && code < 0x10000) {
        rep.push_back(static_cast<char>(0xE0 | (code >> 12)));
        rep.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        rep.push_back(static_cast<char>(0x80 | (code & 0x3F)));
      }
    }
    if (rep.empty()) { out.push_back(s[i]); continue; }
    out += rep;
    i = semi;
  }
  return out;
}

bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool ends_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool contains(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

std::vector<std::string> split_sentences(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  const std::string t = collapse_ws(text);
  for (std::size_t i = 0; i < t.size(); ++i) {
    cur.push_back(t[i]);
    char c = t[i];
    if (c != '.' && c != '!' && c != '?') continue;
    bool end_of_text = i + 1 >= t.size();
    bool boundary = end_of_text;
    if (!end_of_text && t[i + 1] == ' ') {
      // Look at the next visible character: a new sentence starts with a capital, digit or quote.
      std::size_t j = i + 2;
      while (j < t.size() && (t[j] == '"' || t[j] == '\'')) ++j;
      boundary = j < t.size() && (std::isupper(static_cast<unsigned char>(t[j])) ||
                                  std::isdigit(static_cast<unsigned char>(t[j])));
      if (boundary && c == '.') {
        // Avoid cutting after short abbreviations like "Mr." or "U.S." or "No.".
        std::size_t k = cur.size() - 1;  // index of '.'
        std::size_t b = k;
        while (b > 0 && cur[b - 1] != ' ') --b;
        std::string word = cur.substr(b, k - b);
        if (word.size() == 1 || contains(word, ".") || word == "Mr" || word == "Mrs" ||
            word == "Dr" || word == "Ms" || word == "St" || word == "No")
          boundary = false;
      }
    }
    if (boundary) {
      std::string s = trim(cur);
      if (!s.empty()) out.push_back(s);
      cur.clear();
    }
  }
  std::string s = trim(cur);
  if (!s.empty()) out.push_back(s);
  return out;
}

static const std::set<std::string>& stopwords() {
  static const std::set<std::string> w = {
      "the", "and", "for", "with", "from", "that", "this", "was", "were", "are", "has", "have",
      "had", "his", "her", "its", "their", "they", "them", "she", "him", "who", "whom", "will",
      "would", "could", "should", "can", "not", "but", "also", "after", "before", "over", "into",
      "than", "then", "about", "against", "between", "during", "while", "when", "where", "which",
      "what", "why", "how", "been", "being", "more", "most", "some", "any", "all", "other",
      "such", "only", "own", "same", "very", "just", "out", "off", "our", "your", "you", "said",
      "says", "say", "told", "per", "new", "one", "two", "may", "might", "did", "does", "doing",
      "got", "get", "make", "made", "year", "years", "day", "days", "news", "report", "reports",
      "reported", "reuters", "ani", "pti", "ians", "read", "also", "its", "there", "these",
      "those", "here", "now", "today", "like", "amid", "via", "next", "last", "first"};
  return w;
}

std::vector<std::string> tokenize(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    if (cur.empty()) return;
    bool numeric = std::all_of(cur.begin(), cur.end(),
                               [](unsigned char c) { return std::isdigit(c); });
    bool keep = numeric ? cur.size() >= 2 : (cur.size() >= 3 && !stopwords().count(cur));
    if (keep) {
      // Very light stemming: "wickets"->"wicket", "elections"->"election".
      if (!numeric && cur.size() > 4 && cur.back() == 's' && cur[cur.size() - 2] != 's')
        cur.pop_back();
      out.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char c : text) {
    if (std::isalnum(c)) {
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

// ---------------------------------------------------------------- URLs

std::string host_of(const std::string& url) {
  auto p = url.find("://");
  if (p == std::string::npos) return "";
  std::size_t b = p + 3;
  std::size_t e = url.find_first_of("/?#:", b);
  std::string host = to_lower(url.substr(b, e == std::string::npos ? std::string::npos : e - b));
  // Drop any user@ prefix, then a leading www.
  auto at = host.find('@');
  if (at != std::string::npos) host = host.substr(at + 1);
  if (starts_with(host, "www.")) host = host.substr(4);
  return host;
}

bool is_http_url(const std::string& url) {
  return starts_with(url, "http://") || starts_with(url, "https://");
}

std::string url_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

std::string base64url_decode(const std::string& s) {
  std::string out;
  int val = 0, bits = -8;
  for (unsigned char c : s) {
    int d;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '-' || c == '+') d = 62;
    else if (c == '_' || c == '/') d = 63;
    else continue;  // padding or junk
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

std::string strip_publisher_suffix(const std::string& title) {
  std::string t = collapse_ws(title);
  for (const char* sep : {" - ", " | ", " \xE2\x80\x93 "}) {
    auto p = t.rfind(sep);
    if (p != std::string::npos && p > 15 && t.size() - p <= 45) {
      return trim(t.substr(0, p));
    }
  }
  return t;
}

// ---------------------------------------------------------------- time

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static long long days_from_civil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

static void civil_from_days(long long z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long yy = static_cast<long long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yy + (m <= 2));
}

std::time_t parse_rfc822(const std::string& s) {
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int day, year, hh, mm, ss = 0;
  char mon[4] = {0};
  const char* p = std::strchr(s.c_str(), ',');
  p = p ? p + 1 : s.c_str();
  if (std::sscanf(p, " %d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) < 5) return 0;
  int mi = -1;
  for (int i = 0; i < 12; ++i)
    if (std::strncmp(mon, months[i], 3) == 0) mi = i;
  if (mi < 0) return 0;
  long long days = days_from_civil(year, static_cast<unsigned>(mi + 1), static_cast<unsigned>(day));
  long long t = days * 86400 + hh * 3600 + mm * 60 + ss;
  // Offsets such as "+0530" are rare in Google feeds (they use GMT); handle the numeric form.
  auto z = s.find_last_of("+-");
  if (z != std::string::npos && z + 5 == s.size() - 0 && z > 10) {
    int off = std::atoi(s.c_str() + z + 1);
    int secs = (off / 100) * 3600 + (off % 100) * 60;
    t += (s[z] == '+') ? -secs : secs;
  }
  return static_cast<std::time_t>(t);
}

std::string format_iso(std::time_t t) {
  long long days = static_cast<long long>(t) / 86400;
  long long rem = static_cast<long long>(t) % 86400;
  if (rem < 0) { rem += 86400; --days; }
  int y; unsigned m, d;
  civil_from_days(days, y, m, d);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02dZ", y, m, d,
                static_cast<int>(rem / 3600), static_cast<int>(rem % 3600 / 60),
                static_cast<int>(rem % 60));
  return buf;
}

std::string today_ist() {
  long long t = static_cast<long long>(std::time(nullptr)) + 19800;  // UTC+5:30
  int y; unsigned m, d;
  civil_from_days(t / 86400, y, m, d);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", y, m, d);
  return buf;
}

bool valid_date(const std::string& ymd) {
  int y, m, d;
  if (ymd.size() != 10 || std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
  if (ymd[4] != '-' || ymd[7] != '-' || m < 1 || m > 12 || d < 1 || d > 31) return false;
  return true;
}

std::string format_date_long(const std::string& ymd) {
  static const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December"};
  static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                               "Thursday", "Friday", "Saturday"};
  int y, m, d;
  if (!valid_date(ymd)) return ymd;
  std::sscanf(ymd.c_str(), "%4d-%2d-%2d", &y, &m, &d);
  long long n = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
  int wd = static_cast<int>(((n % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s, %d %s %d", days[wd], d, months[m - 1], y);
  return buf;
}

int load_env_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) return 0;
  int set_count = 0;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (starts_with(line, "export ")) line = trim(line.substr(7));
    auto eq = line.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    if (key.empty() || value.empty()) continue;
    const char* existing = std::getenv(key.c_str());
    if (existing && *existing) continue;  // a real environment variable wins over the file
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 0);
#endif
    ++set_count;
  }
  return set_count;
}

}  // namespace mt
