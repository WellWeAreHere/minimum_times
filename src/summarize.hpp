// Writing the three summaries for an event.
//
// This is the ONLY place AI is used, and its output is checked by plain rules before use.
// If there is no API key, the call fails, or a check fails, we fall back to extractive
// summaries: real sentences copied from the articles (so nothing can be invented).
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace mt {

struct Summary {
  std::string summary, micro, extended;
  bool ai = false;
};

// Plain-rule checks on AI output. Returns "" if fine, otherwise the reason it was rejected.
std::string check_summary(const Summary& s, const std::string& source_text);

// True when every number in `summary` also appears in `source_text` (commas ignored).
bool numbers_supported(const std::string& summary, const std::string& source_text);

// Copies real sentences out of the event's articles. Never fails.
Summary summarize_extractive(const Event& e, const std::vector<Article>& all);

// Asks the AI model. Empty optional on any failure (reason in `error`).
std::optional<Summary> summarize_ai(const Event& e, const std::vector<Article>& all,
                                    const std::string& api_key, const std::string& model,
                                    std::string& error);

// AI when a key is given, extractive otherwise or on failure.
Summary summarize(const Event& e, const std::vector<Article>& all, const std::string& api_key,
                  const std::string& model);

}  // namespace mt
