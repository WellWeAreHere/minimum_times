// Reading an RSS feed.
#pragma once
#include <string>
#include <vector>

namespace mt {

// One of the other outlets' articles about the same story (Google lists up to ~5 per item).
struct RssRelated {
  std::string title, link, source_name;
};

struct RssItem {
  std::string title;
  std::string link;
  std::string pub_date;
  std::string source_name;  // Google News <source>Publisher</source>
  std::string source_url;   // Google News <source url="https://publisher.com">
  std::vector<RssRelated> related;  // same story from other outlets (from the description)
};

// Returns an empty list if the XML cannot be parsed.
std::vector<RssItem> parse_rss(const std::string& xml, int max_items);

}  // namespace mt
