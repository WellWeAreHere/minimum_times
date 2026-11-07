// Getting the readable article text out of an HTML page.
#pragma once
#include <string>

#include "model.hpp"

namespace mt {

struct Extracted {
  std::string text;
  TextStatus status = TextStatus::Empty;
  std::string reason;
};

// Pure function (no network): given the page HTML, returns the article text and how good it is.
Extracted extract_article(const std::string& html);

// Downloads the page and extracts it. Fills `a.text`, `a.status`, `a.reason`.
void fetch_article_text(Article& a);

}  // namespace mt
