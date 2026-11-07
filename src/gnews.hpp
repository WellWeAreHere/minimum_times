// Google News links are wrappers around the real article. This finds the real link.
#pragma once
#include <optional>
#include <string>

namespace mt {

// The id inside "https://news.google.com/rss/articles/<id>?oc=5", or "" if it is not such a link.
std::string gnews_article_id(const std::string& url);

// Old-style ids contain the publisher URL directly (base64). Works offline.
// Returns nothing for new-style ids that need a request to Google.
std::optional<std::string> gnews_decode_offline(const std::string& id);

// Tries the offline way first, then asks Google. `error` says what went wrong on failure.
std::optional<std::string> gnews_resolve(const std::string& url, std::string& error);

}  // namespace mt
