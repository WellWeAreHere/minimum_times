#include "extract.hpp"

#include <gumbo.h>

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>

#include "config.hpp"
#include "gnews.hpp"
#include "http.hpp"
#include "util.hpp"

namespace mt {

const char* status_name(TextStatus s) {
  switch (s) {
    case TextStatus::None: return "none";
    case TextStatus::FullExtraction: return "full_extraction";
    case TextStatus::ShortButValid: return "short_but_valid";
    case TextStatus::Blocked: return "blocked";
    case TextStatus::Empty: return "empty";
    case TextStatus::Unresolved: return "unresolved";
  }
  return "none";
}

namespace {

struct GumboDeleter {
  GumboOutput* out;
  ~GumboDeleter() { gumbo_destroy_output(&kGumboDefaultOptions, out); }
};

bool is_tag(const GumboNode* n, GumboTag tag) {
  return n->type == GUMBO_NODE_ELEMENT && n->v.element.tag == tag;
}

// Parts of a page that never contain the story itself.
bool is_noise(const GumboNode* n) {
  if (n->type != GUMBO_NODE_ELEMENT) return false;
  switch (n->v.element.tag) {
    case GUMBO_TAG_SCRIPT: case GUMBO_TAG_STYLE: case GUMBO_TAG_NOSCRIPT:
    case GUMBO_TAG_NAV: case GUMBO_TAG_FOOTER: case GUMBO_TAG_ASIDE:
    case GUMBO_TAG_FORM: case GUMBO_TAG_BUTTON: case GUMBO_TAG_SELECT:
      return true;
    default:
      return false;
  }
}

void collect_text(const GumboNode* n, std::string& out) {
  if (n->type == GUMBO_NODE_TEXT || n->type == GUMBO_NODE_WHITESPACE) {
    out += n->v.text.text;
    return;
  }
  if (n->type != GUMBO_NODE_ELEMENT || is_noise(n)) return;
  const GumboVector& kids = n->v.element.children;
  for (unsigned i = 0; i < kids.length; ++i) collect_text(static_cast<GumboNode*>(kids.data[i]), out);
}

// Text of a <p>, or "" if it is too short to be a real sentence (menus, captions, "Read more").
std::string paragraph_text(const GumboNode* p) {
  std::string raw;
  collect_text(p, raw);
  std::string t = collapse_ws(raw);
  return word_count(t) >= 6 ? t : "";
}

// All real paragraphs under `n`, skipping noise sections.
void collect_paragraphs(const GumboNode* n, std::vector<std::string>& out) {
  if (n->type != GUMBO_NODE_ELEMENT || is_noise(n)) return;
  if (n->v.element.tag == GUMBO_TAG_P) {
    std::string t = paragraph_text(n);
    if (!t.empty()) out.push_back(t);
    return;
  }
  const GumboVector& kids = n->v.element.children;
  for (unsigned i = 0; i < kids.length; ++i)
    collect_paragraphs(static_cast<GumboNode*>(kids.data[i]), out);
}

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& p : parts) {
    if (!out.empty()) out += ' ';
    out += p;
  }
  return out;
}

// Candidate 1: <script type="application/ld+json"> blocks that carry "articleBody".
void find_article_body(const nlohmann::json& j, std::vector<std::string>& out) {
  if (j.is_object()) {
    auto it = j.find("articleBody");
    if (it != j.end() && it->is_string()) out.push_back(it->get<std::string>());
    for (const auto& kv : j.items()) find_article_body(kv.value(), out);
  } else if (j.is_array()) {
    for (const auto& v : j) find_article_body(v, out);
  }
}

void collect_json_ld(const GumboNode* n, std::vector<std::string>& bodies) {
  if (n->type != GUMBO_NODE_ELEMENT) return;
  if (is_tag(n, GUMBO_TAG_SCRIPT)) {
    GumboAttribute* type = gumbo_get_attribute(&n->v.element.attributes, "type");
    if (type && std::string(type->value) == "application/ld+json" &&
        n->v.element.children.length > 0) {
      auto* child = static_cast<GumboNode*>(n->v.element.children.data[0]);
      if (child->type == GUMBO_NODE_TEXT || child->type == GUMBO_NODE_CDATA) {
        auto parsed = nlohmann::json::parse(child->v.text.text, nullptr, false);
        if (!parsed.is_discarded()) find_article_body(parsed, bodies);
      }
    }
    return;
  }
  const GumboVector& kids = n->v.element.children;
  for (unsigned i = 0; i < kids.length; ++i)
    collect_json_ld(static_cast<GumboNode*>(kids.data[i]), bodies);
}

// Candidate 2: paragraphs inside each <article>.
void collect_article_tags(const GumboNode* n, std::vector<std::string>& candidates) {
  if (n->type != GUMBO_NODE_ELEMENT) return;
  if (is_tag(n, GUMBO_TAG_ARTICLE)) {
    std::vector<std::string> paras;
    collect_paragraphs(n, paras);
    if (!paras.empty()) candidates.push_back(join(paras));
  }
  const GumboVector& kids = n->v.element.children;
  for (unsigned i = 0; i < kids.length; ++i)
    collect_article_tags(static_cast<GumboNode*>(kids.data[i]), candidates);
}

// Candidate 3: the element whose own <p> children hold the most text (the article body
// container on most sites, even without an <article> tag).
void best_container(const GumboNode* n, std::string& best) {
  if (n->type != GUMBO_NODE_ELEMENT || is_noise(n)) return;
  std::vector<std::string> own;
  const GumboVector& kids = n->v.element.children;
  for (unsigned i = 0; i < kids.length; ++i) {
    auto* k = static_cast<GumboNode*>(kids.data[i]);
    if (is_tag(k, GUMBO_TAG_P)) {
      std::string t = paragraph_text(k);
      if (!t.empty()) own.push_back(t);
    }
  }
  if (!own.empty()) {
    std::string joined = join(own);
    if (joined.size() > best.size()) best = joined;
  }
  for (unsigned i = 0; i < kids.length; ++i) best_container(static_cast<GumboNode*>(kids.data[i]), best);
}

bool looks_blocked(const std::string& text) {
  const std::string t = to_lower(text);
  for (const char* marker : {"enable javascript", "access denied", "automated access",
                             "verify you are human", "subscribe to continue",
                             "sign in to continue", "page not found", "robot check",
                             "are you a robot", "unusual traffic"}) {
    if (contains(t, marker)) return true;
  }
  return false;
}

std::string cap_chars(const std::string& s) {
  if (static_cast<int>(s.size()) <= cfg::MAX_TEXT_CHARS) return s;
  std::string cut = s.substr(0, cfg::MAX_TEXT_CHARS);
  auto sp = cut.find_last_of(' ');
  return sp == std::string::npos ? cut : cut.substr(0, sp);
}

}  // namespace

Extracted extract_article(const std::string& html) {
  Extracted result;
  GumboOutput* output = gumbo_parse(html.c_str());
  GumboDeleter guard{output};

  std::vector<std::string> candidates;
  std::vector<std::string> bodies;
  collect_json_ld(output->root, bodies);
  for (auto& b : bodies) candidates.push_back(collapse_ws(b));
  collect_article_tags(output->root, candidates);
  std::string container;
  best_container(output->root, container);
  if (!container.empty()) candidates.push_back(container);

  std::string best;
  for (auto& c : candidates)
    if (c.size() > best.size()) best = c;
  best = cap_chars(best);

  if (best.empty()) {
    result.status = TextStatus::Empty;
    result.reason = "no_article_text";
    return result;
  }
  if (looks_blocked(best) && word_count(best) < cfg::MIN_FULL_WORDS) {
    result.status = TextStatus::Blocked;
    result.reason = "boilerplate_or_interstitial";
    return result;
  }
  int words = word_count(best);
  if (words < cfg::MIN_SHORT_WORDS) {
    result.status = TextStatus::Empty;
    result.reason = "insufficient_article_text";
    return result;
  }
  result.text = best;
  result.status = words >= cfg::MIN_FULL_WORDS ? TextStatus::FullExtraction : TextStatus::ShortButValid;
  return result;
}

void fetch_article_text(Article& a) {
  std::string error;
  auto resolved = gnews_resolve(a.feed_link, error);
  if (!resolved) {
    a.status = TextStatus::Unresolved;
    a.reason = error;
    return;
  }
  a.url = *resolved;

  HttpOptions opt;
  opt.timeout_seconds = cfg::HTTP_TIMEOUT_SECONDS;
  opt.headers = {"Accept: text/html,application/xhtml+xml;q=0.9,*/*;q=0.5",
                 "Accept-Language: en-US,en;q=0.9"};
  HttpResponse r = http_get(a.url, opt);
  if (!r.ok()) {
    a.status = TextStatus::Blocked;
    a.reason = r.status ? "http_" + std::to_string(r.status) : r.error;
    return;
  }
  if (!r.content_type.empty() && !contains(to_lower(r.content_type), "html")) {
    a.status = TextStatus::Empty;
    a.reason = "unsupported_content_type:" + r.content_type;
    return;
  }
  if (a.publisher.empty()) a.publisher = host_of(r.final_url.empty() ? a.url : r.final_url);

  Extracted e = extract_article(r.body);
  a.status = e.status;
  a.reason = e.reason;
  a.text = e.text;
}

}  // namespace mt
