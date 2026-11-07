#include "render.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>

#include "config.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace mt {

namespace {

const char* kScopeLabel(const std::string& scope) {
  return scope == "national" ? "NATIONAL \xE2\x80\x94 INDIA" : "INTERNATIONAL";
}

std::string category_label(const std::string& c) {
  if (c == "science") return "SCIENCE &amp; TECHNOLOGY";
  std::string up = c;
  for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return html_escape(up);
}

std::string short_time(const std::string& iso) {
  if (iso.size() < 16) return iso;
  return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
}

const char* kStyle = R"CSS(
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;font:16px/1.5 Georgia,'Times New Roman',serif;background:#000}
.ctl{position:absolute;opacity:0;pointer-events:none}
.page{--bg:#000;--fg:#fff;--muted:#9a9a9a;--line:#333;--panel:#111;
  background:var(--bg);color:var(--fg);min-height:100vh}
@media (prefers-color-scheme:light){
  .page{--bg:#fff;--fg:#111;--muted:#666;--line:#ddd;--panel:#f6f6f6}}
#flip:checked ~ .page{--bg:#fff;--fg:#111;--muted:#666;--line:#ddd;--panel:#f6f6f6}
@media (prefers-color-scheme:light){
  #flip:checked ~ .page{--bg:#000;--fg:#fff;--muted:#9a9a9a;--line:#333;--panel:#111}}
.wrap{max-width:56rem;margin:0 auto;padding:2rem 1rem 3rem}
h1{font-size:2.4rem;letter-spacing:-.02em;margin:0 0 .25rem}
.tag{color:var(--muted);margin:0}
.date{color:var(--muted);font:12px/1.4 system-ui,sans-serif;text-transform:uppercase;letter-spacing:.08em;margin:.5rem 0 0}
.top{display:flex;flex-wrap:wrap;gap:1rem;justify-content:space-between;align-items:flex-end;margin-bottom:2.5rem}
.btns{display:flex;flex-wrap:wrap;gap:.5rem}
.btn,.tab,.catmenu summary{font:600 13px/1 system-ui,sans-serif;border:1px solid var(--line);
  color:var(--muted);padding:.8rem 1rem;min-height:44px;display:inline-flex;align-items:center;
  cursor:pointer;background:transparent;text-decoration:none;user-select:none;list-style:none}
.btn:hover,.tab:hover,.catmenu summary:hover{border-color:var(--muted)}
.catmenu summary::-webkit-details-marker{display:none}
.bar{display:flex;flex-wrap:wrap;gap:.5rem;align-items:flex-start;margin-bottom:2rem}
.catmenu{position:relative}
.menu{position:absolute;z-index:5;left:0;top:calc(100% + .4rem);width:18rem;background:var(--bg);
  border:1px solid var(--line);padding:1rem;display:grid;gap:.4rem;box-shadow:0 6px 20px rgba(0,0,0,.35)}
.menu label{font:14px/1.2 system-ui,sans-serif;padding:.6rem .2rem;cursor:pointer;color:var(--fg)}
h2{font-size:1.5rem;border-bottom:1px solid var(--line);padding-bottom:.6rem;margin:0 0 1.5rem}
.cat{margin-bottom:2rem}
.cat>details>summary{font-weight:700;font-size:1.2rem;border-bottom:1px solid var(--line);
  padding:.4rem 0 .7rem;cursor:pointer;list-style:none;display:flex;justify-content:space-between}
.cat>details>summary::-webkit-details-marker{display:none}
.cat>details>summary::after{content:"\2212"}
.cat>details:not([open])>summary::after{content:"+"}
.none{color:var(--muted);padding:1rem 0;margin:0}
.event{border-bottom:1px solid var(--line)}
.event>summary{padding:1rem 0;cursor:pointer;list-style:none}
.event>summary::-webkit-details-marker{display:none}
.event>summary:hover{background:var(--panel)}
.line{font-size:1.15rem;line-height:1.35;display:block}
.meta{display:block;font:12px/1.4 system-ui,sans-serif;color:var(--muted);margin-top:.4rem}
.more{background:var(--panel);padding:1.25rem 1rem;margin-bottom:.25rem}
.label{font:600 11px/1.2 system-ui,sans-serif;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin:0 0 .6rem}
.more p.body{margin:0;line-height:1.75}
.src{margin:1rem 0 0;padding:1rem 0 0 1.1rem;border-top:1px solid var(--line);font:14px/1.9 system-ui,sans-serif}
.src a{color:var(--fg)}
footer{border-top:1px solid var(--line);padding-top:1.25rem;font:14px/1.5 system-ui,sans-serif;color:var(--muted);margin-top:3rem}
footer a{color:var(--fg)}
.archive a{color:var(--fg)}
.archive li{padding:.4rem 0}
.scope{display:block}
#sc-national:checked ~ .page #scope-international{display:none}
#sc-international:checked ~ .page #scope-national{display:none}
#extreme:checked ~ .page .normal{display:none}
#extreme:not(:checked) ~ .page .micro{display:none}
#sc-national:checked ~ .page label[for=sc-national],
#sc-international:checked ~ .page label[for=sc-international],
#extreme:checked ~ .page label[for=extreme]{background:var(--fg);color:var(--bg);border-color:var(--fg)}
.menu label::before{content:"\2610\00a0\00a0"}
#sc-national:checked ~ .page .menu-international{display:none}
#sc-international:checked ~ .page .menu-national{display:none}
)CSS";

std::string control_css() {
  std::ostringstream css;
  // Each tab (National / International) has its own set of category checkboxes.
  for (const auto& scope : cfg::SCOPES) {
    for (const auto& c : cfg::CATEGORIES) {
      const std::string id = "cat-" + scope + "-" + c;
      css << "#" << id << ":not(:checked) ~ .page #scope-" << scope << " .sec-" << c
          << "{display:none}\n";
      css << "#" << id << ":checked ~ .page label[for=" << id
          << "]::before{content:\"\\2611\\00a0\\00a0\"}\n";
      css << "#" << id << ":focus-visible ~ .page label[for=" << id
          << "]{outline:2px solid var(--fg)}\n";
    }
  }
  for (const char* id : {"sc-national", "sc-international", "extreme", "flip"}) {
    css << "#" << id << ":focus-visible ~ .page label[for=" << id
        << "]{outline:2px solid var(--fg);outline-offset:2px}\n";
  }
  return css.str();
}

std::string page_head(const std::string& title) {
  std::ostringstream h;
  h << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
    << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    << "<title>" << html_escape(title) << "</title><style>" << kStyle << control_css()
    << "</style></head>\n";
  return h.str();
}

// Remembers the reader's choices (theme, extreme mode, tab, categories per tab) in the browser's
// localStorage. Nothing leaves the device. If storage is blocked the page simply works without it.
const char* kPrefsJs = R"JS((function () {
  var KEY = 'minimum-times-prefs-v1';
  var controls = document.querySelectorAll('input.ctl');
  try {
    var saved = JSON.parse(localStorage.getItem(KEY) || 'null');
    if (saved && typeof saved === 'object') {
      for (var i = 0; i < controls.length; i++) {
        var id = controls[i].id;
        if (typeof saved[id] === 'boolean') controls[i].checked = saved[id];
      }
    }
  } catch (e) {}
  document.addEventListener('change', function (event) {
    if (!event.target || event.target.className !== 'ctl') return;
    try {
      var state = {};
      for (var i = 0; i < controls.length; i++) state[controls[i].id] = controls[i].checked;
      localStorage.setItem(KEY, JSON.stringify(state));
    } catch (e) {}
  });
})();
)JS";

void write_file(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << content;
}

}  // namespace

json edition_to_json(const Edition& e) {
  json events = json::array();
  for (const auto& ev : e.events) {
    json sources = json::array();
    for (const auto& s : ev.sources) sources.push_back({{"url", s.url}, {"publisher", s.publisher}});
    events.push_back({{"id", ev.id},
                      {"scope", ev.scope},
                      {"category", ev.category},
                      {"score", ev.score},
                      {"publishers", ev.n_publishers},
                      {"articles", ev.n_articles},
                      {"timestamp", ev.timestamp},
                      {"summary", ev.summary},
                      {"micro_summary", ev.micro},
                      {"extended_summary", ev.extended},
                      {"ai", ev.ai},
                      {"sources", sources}});
  }
  return {{"date", e.date}, {"events", events}};
}

Edition edition_from_json(const json& j) {
  Edition e;
  e.date = j.value("date", "");
  for (const auto& item : j.value("events", json::array())) {
    PublishedEvent ev;
    ev.id = item.value("id", "");
    ev.scope = item.value("scope", "");
    ev.category = item.value("category", "");
    ev.score = item.value("score", 0.0);
    ev.n_publishers = item.value("publishers", 0);
    ev.n_articles = item.value("articles", 0);
    ev.timestamp = item.value("timestamp", "");
    ev.summary = item.value("summary", "");
    ev.micro = item.value("micro_summary", ev.summary);
    ev.extended = item.value("extended_summary", "");
    ev.ai = item.value("ai", false);
    for (const auto& s : item.value("sources", json::array()))
      ev.sources.push_back({s.value("url", ""), s.value("publisher", "")});
    e.events.push_back(std::move(ev));
  }
  return e;
}

std::string render_edition_html(const Edition& e, const std::string& root) {
  std::ostringstream h;
  h << page_head("Minimum Times \xE2\x80\x94 " + e.date);
  h << "<body>\n"
    << "<input type=\"radio\" name=\"scope\" id=\"sc-national\" class=\"ctl\" checked>\n"
    << "<input type=\"radio\" name=\"scope\" id=\"sc-international\" class=\"ctl\">\n"
    << "<input type=\"checkbox\" id=\"extreme\" class=\"ctl\" checked>\n"
    << "<input type=\"checkbox\" id=\"flip\" class=\"ctl\">\n";
  for (const auto& scope : cfg::SCOPES)
    for (const auto& c : cfg::CATEGORIES)
      h << "<input type=\"checkbox\" id=\"cat-" << scope << "-" << c << "\" class=\"ctl\" checked>\n";
  // Restores the reader's saved choices before the page is first painted (see kPrefsJs).
  h << "<script src=\"" << root << "prefs.js\"></script>\n";

  h << "<div class=\"page\"><div class=\"wrap\">\n<header class=\"top\"><div>"
    << "<h1>MINIMUM TIMES</h1><p class=\"tag\">The minimum news you need.</p>"
    << "<p class=\"date\">" << html_escape(format_date_long(e.date)) << "</p></div>\n"
    << "<div class=\"btns\">"
    << "<label class=\"btn\" for=\"flip\" title=\"Switch light/dark theme\" "
       "aria-label=\"Switch light or dark theme\">\xE2\x97\x90</label>"
    << "<label class=\"btn\" for=\"extreme\" title=\"Extreme mode: shortest summaries\" "
       "aria-label=\"Toggle extreme mode, the shortest summaries\">\xE2\x9A\xA1</label>"
    << "<a class=\"btn\" href=\"" << root << "archive.html\">ARCHIVE</a>"
    << "</div></header>\n";

  h << "<nav class=\"bar\" aria-label=\"Edition scope\">"
    << "<label class=\"tab\" for=\"sc-national\">" << kScopeLabel("national") << "</label>"
    << "<label class=\"tab\" for=\"sc-international\">" << kScopeLabel("international") << "</label>"
    << "<details class=\"catmenu\"><summary>CATEGORIES \xE2\x96\xBE</summary>";
  for (const auto& scope : cfg::SCOPES) {
    h << "<div class=\"menu menu-" << scope << "\">";
    for (const auto& c : cfg::CATEGORIES)
      h << "<label for=\"cat-" << scope << "-" << c << "\">" << category_label(c) << "</label>";
    h << "</div>";
  }
  h << "</details></nav>\n<main>\n";

  for (const auto& scope : cfg::SCOPES) {
    h << "<section class=\"scope\" id=\"scope-" << scope << "\"><h2>" << kScopeLabel(scope)
      << "</h2>\n";
    for (const auto& cat : cfg::CATEGORIES) {
      h << "<div class=\"cat sec-" << cat << "\"><details open><summary>" << category_label(cat)
        << "</summary>\n";
      int shown = 0;
      for (const auto& ev : e.events) {
        if (ev.scope != scope || ev.category != cat) continue;
        ++shown;
        h << "<details class=\"event\"><summary>"
          << "<span class=\"line micro\">" << html_escape(ev.micro) << "</span>"
          << "<span class=\"line normal\">" << html_escape(ev.summary) << "</span>"
          << "<span class=\"meta\">" << ev.n_publishers << (ev.n_publishers == 1 ? " outlet" : " outlets")
          << " \xC2\xB7 " << html_escape(short_time(ev.timestamp)) << "</span></summary>"
          << "<div class=\"more\"><p class=\"label\">Extended summary</p><p class=\"body\">"
          << html_escape(ev.extended) << "</p>";
        bool any_link = false;
        for (const auto& s : ev.sources) {
          if (!is_http_url(s.url)) continue;  // never emit javascript: or other odd links
          if (!any_link) h << "<ul class=\"src\">";
          any_link = true;
          h << "<li><a href=\"" << html_escape(s.url)
            << "\" target=\"_blank\" rel=\"noopener noreferrer\">"
            << html_escape(s.publisher.empty() ? host_of(s.url) : s.publisher) << "</a></li>";
        }
        if (any_link) h << "</ul>";
        h << "</div></details>\n";
      }
      if (!shown) h << "<p class=\"none\">No major news.</p>\n";
      h << "</details></div>\n";
    }
    h << "</section>\n";
  }

  h << "</main>\n<footer>Ideas, issues, or suggestions? "
    << "<a href=\"mailto:iamherebcozidontknow@gmail.com?subject=Feedback%20for%20Minimum%20Times\">"
       "\xE2\x86\x92 Email us</a></footer>\n</div></div></body></html>\n";
  return h.str();
}

std::string render_archive_html(const std::vector<std::string>& dates_desc) {
  std::ostringstream h;
  h << page_head("Minimum Times \xE2\x80\x94 Archive");
  h << "<body><div class=\"page\"><div class=\"wrap\"><header class=\"top\"><div>"
    << "<h1>MINIMUM TIMES</h1><p class=\"tag\">Past editions</p></div>"
    << "<div class=\"btns\"><a class=\"btn\" href=\"index.html\">LATEST</a></div></header>\n"
    << "<ul class=\"archive\">";
  for (const auto& d : dates_desc)
    h << "<li><a href=\"editions/" << html_escape(d) << ".html\">" << html_escape(format_date_long(d))
      << "</a></li>";
  h << "</ul></div></div></body></html>\n";
  return h.str();
}

std::vector<std::string> list_edition_dates(const std::string& out_dir) {
  std::vector<std::string> dates;
  fs::path dir = fs::path(out_dir) / "data";
  std::error_code ec;
  if (!fs::exists(dir, ec)) return dates;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (entry.path().extension() != ".json") continue;
    std::string stem = entry.path().stem().string();
    if (valid_date(stem)) dates.push_back(stem);
  }
  std::sort(dates.begin(), dates.end(), std::greater<>());
  return dates;
}

static std::optional<Edition> load_edition(const std::string& out_dir, const std::string& date) {
  std::ifstream in(fs::path(out_dir) / "data" / (date + ".json"), std::ios::binary);
  if (!in) return std::nullopt;
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded()) return std::nullopt;
  return edition_from_json(j);
}

std::vector<Edition> load_recent_editions(const std::string& out_dir, const std::string& date,
                                          int count) {
  std::vector<Edition> out;
  for (const auto& d : list_edition_dates(out_dir)) {
    if (d >= date) continue;
    if (auto e = load_edition(out_dir, d)) out.push_back(std::move(*e));
    if (static_cast<int>(out.size()) >= count) break;
  }
  return out;
}

void write_site(const std::string& out_dir, const Edition& e) {
  fs::path root(out_dir);
  write_file(root / "data" / (e.date + ".json"), edition_to_json(e).dump(2) + "\n");
  write_file(root / "editions" / (e.date + ".html"), render_edition_html(e, "../"));

  auto dates = list_edition_dates(out_dir);
  if (!dates.empty()) {
    if (auto latest = load_edition(out_dir, dates.front()))
      write_file(root / "index.html", render_edition_html(*latest, ""));
  }
  write_file(root / "archive.html", render_archive_html(dates));
  write_file(root / "prefs.js", kPrefsJs);
}

int rerender_site(const std::string& out_dir) {
  fs::path root(out_dir);
  auto dates = list_edition_dates(out_dir);
  for (const auto& d : dates) {
    if (auto e = load_edition(out_dir, d))
      write_file(root / "editions" / (d + ".html"), render_edition_html(*e, "../"));
  }
  if (!dates.empty()) {
    if (auto latest = load_edition(out_dir, dates.front()))
      write_file(root / "index.html", render_edition_html(*latest, ""));
  }
  write_file(root / "archive.html", render_archive_html(dates));
  write_file(root / "prefs.js", kPrefsJs);
  return static_cast<int>(dates.size());
}

}  // namespace mt
