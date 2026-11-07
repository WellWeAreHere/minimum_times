// The whole daily run, from fetching feeds to writing the site.
#pragma once
#include <string>

#include "config.hpp"
#include "model.hpp"

namespace mt {

struct Options {
  std::string out_dir = "docs";
  std::string log_dir = "data/logs";
  std::string date;              // YYYY-MM-DD; empty = today in India
  bool use_ai = true;            // false = extractive summaries only
  int max_per_feed = 1000;       // upper limit per feed (each feed also has its own limit)
};

// Exit codes: 0 = published, 75 = every feed returned HTTP 503 (try again later), 1 = failed.
int run_pipeline(const Options& options);

// ---- pieces exposed so they can be tested without the network ----

// Groups articles of one scope into events. Returns events whose `articles` index into `all`.
std::vector<Event> build_events(const std::vector<Article>& all, const std::string& scope);

// Drops events too similar to ones in earlier editions. Returns the ids dropped.
std::vector<std::string> drop_repeats(std::vector<PublishedEvent>& events,
                                      const std::vector<Edition>& previous, double threshold);

}  // namespace mt
