#include "score.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "config.hpp"
#include "util.hpp"

namespace mt {

namespace {

// Category words as stems, so they match tokenize() output.
const std::map<std::string, std::set<std::string>>& keyword_stems() {
  static const auto stems = [] {
    std::map<std::string, std::set<std::string>> out;
    for (const auto& [category, words] : cfg::CATEGORY_KEYWORDS)
      for (const auto& w : words)
        for (const auto& t : tokenize(w)) out[category].insert(t);
    return out;
  }();
  return stems;
}

}  // namespace

std::string assign_category(const Event& e, const std::vector<Article>& all) {
  std::map<std::string, double> points;
  std::set<std::string> title_tokens;
  for (int i : e.articles) {
    // One vote per article from the feed it came from. The World feed is not a category, so it
    // gives no vote: its stories are sorted by headline keywords alone.
    if (all[i].category != "world") points[all[i].category] += 1.0;
    for (const auto& t : tokenize(all[i].title)) title_tokens.insert(t);
  }
  for (const auto& [category, stems] : keyword_stems()) {
    int hits = 0;
    for (const auto& t : title_tokens) hits += static_cast<int>(stems.count(t));
    points[category] += cfg::KEYWORD_WEIGHT * hits;
  }
  std::string best;
  double best_points = -1;
  // Walk in the fixed category order so ties are decided the same way every run.
  for (const auto& c : cfg::CATEGORIES) {
    auto it = points.find(c);
    if (it != points.end() && it->second > best_points) {
      best = c;
      best_points = it->second;
    }
  }
  return best.empty() ? "politics" : best;  // world news with no keyword hit: most likely politics
}

void score_event(Event& e, const std::vector<Article>& all, std::time_t now) {
  std::set<std::string> publishers;
  double rank_sum = 0, rank_fraction_sum = 0;
  std::time_t newest = 0;
  for (int i : e.articles) {
    const Article& a = all[i];
    publishers.insert(a.publisher.empty() ? a.feed_link : to_lower(a.publisher));
    rank_sum += a.feed_rank;
    rank_fraction_sum += static_cast<double>(a.feed_rank) / std::max(1, a.feed_size);
    newest = std::max(newest, a.published);
  }
  e.n_articles = static_cast<int>(e.articles.size());
  e.n_publishers = static_cast<int>(publishers.size());
  e.avg_rank = e.n_articles ? rank_sum / e.n_articles : 0;
  e.newest = newest;
  e.age_hours = newest ? std::max(0.0, std::difftime(now, newest) / 3600.0) : 48.0;

  e.coverage = std::min(1.0, std::log(1.0 + e.n_publishers) / std::log(1.0 + cfg::COVERAGE_SATURATION));
  e.rank_score = e.n_articles ? std::max(0.0, 1.0 - rank_fraction_sum / e.n_articles) : 0.0;
  e.fresh_score = std::exp(-e.age_hours / cfg::FRESH_DECAY_HOURS);
  e.score = cfg::W_COVERAGE * e.coverage + cfg::W_RANK * e.rank_score + cfg::W_FRESH * e.fresh_score;

  // Local or off-topic kinds of story (e.g. school sports results) count for less.
  e.demoted = false;
  for (int i : e.articles) {
    const std::string title = to_lower(all[i].title);
    for (const auto& phrase : cfg::DEMOTE_PHRASES)
      if (title.find(phrase) != std::string::npos) e.demoted = true;
  }
  if (e.demoted) e.score *= cfg::DEMOTE_FACTOR;
}

}  // namespace mt
