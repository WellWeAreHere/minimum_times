#include "rss.hpp"

#include <pugixml.hpp>
#include <regex>

#include "util.hpp"

namespace mt {

namespace {

std::string link_key(const std::string& link) { return link.substr(0, link.find('?')); }

// Google puts its own story cluster in the description as a list:
//   <li><a href="LINK">Title</a>&nbsp;&nbsp;<font color="#6f6f6f">Outlet</font></li>
std::vector<RssRelated> parse_related(const std::string& description, const std::string& own_link) {
  static const std::regex entry(R"re(<a href="([^"]+)"[^>]*>(.*?)</a>(?:&nbsp;|\s)*<font[^>]*>(.*?)</font>)re",
                                std::regex::icase);
  std::vector<RssRelated> out;
  const std::string& html = description;  // the XML parser already turned it into plain HTML
  for (auto it = std::sregex_iterator(html.begin(), html.end(), entry); it != std::sregex_iterator(); ++it) {
    RssRelated r;
    r.link = html_unescape((*it)[1]);
    r.title = collapse_ws(html_unescape((*it)[2]));
    r.source_name = collapse_ws(html_unescape((*it)[3]));
    if (r.link.empty() || link_key(r.link) == link_key(own_link)) continue;
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace

std::vector<RssItem> parse_rss(const std::string& xml, int max_items) {
  std::vector<RssItem> items;
  pugi::xml_document doc;
  if (!doc.load_buffer(xml.data(), xml.size())) return items;

  auto channel = doc.child("rss").child("channel");
  for (auto node = channel.child("item"); node; node = node.next_sibling("item")) {
    if (static_cast<int>(items.size()) >= max_items) break;
    RssItem item;
    item.title = node.child_value("title");
    item.link = node.child_value("link");
    item.pub_date = node.child_value("pubDate");
    auto source = node.child("source");
    item.source_name = source.text().as_string();
    item.source_url = source.attribute("url").as_string();
    item.related = parse_related(node.child_value("description"), item.link);
    if (!item.title.empty() && !item.link.empty()) items.push_back(std::move(item));
  }
  return items;
}

}  // namespace mt
