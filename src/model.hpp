// The data types shared by every stage of the pipeline.
#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace mt {

// What happened when we tried to get the real article text.
enum class TextStatus {
  None,            // not tried yet
  FullExtraction,  // plenty of real article text
  ShortButValid,   // short, but real article text
  Blocked,         // paywall, bot check, HTTP error...
  Empty,           // page loaded but no usable article text
  Unresolved       // could not turn the Google News link into the publisher link
};

const char* status_name(TextStatus s);

// One story from one publisher.
struct Article {
  std::string scope;      // "national" or "international"
  std::string category;   // the feed it came from (politics, sports, ...)
  std::string title;      // headline with the " - Publisher" suffix removed
  std::string feed_link;  // the Google News wrapper link
  std::string url;        // real publisher URL (empty if we could not resolve it)
  std::string publisher;  // domain, e.g. "hindustantimes.com"
  std::time_t published = 0;
  int feed_rank = 0;      // position in its feed (0 = Google's top result)
  int feed_size = 20;     // how many items that feed had
  std::string group;      // articles with the same non-empty group are the same story (Google's own cluster)
  bool fetch = true;      // download this article's text? (related articles beyond a few are only counted)
  std::string text;       // extracted article text (empty unless has_text())
  TextStatus status = TextStatus::None;
  std::string reason;     // why extraction failed, for the log

  bool has_text() const {
    return status == TextStatus::FullExtraction || status == TextStatus::ShortButValid;
  }
};

// A real-world happening, made of one or more articles about it.
struct Event {
  std::string id, scope, category;
  std::vector<int> articles;  // indices into the global article list

  // Features used for grading (see score.cpp).
  int n_articles = 0;
  int n_publishers = 0;
  double avg_rank = 0;
  double age_hours = 0;
  std::time_t newest = 0;
  double coverage = 0, rank_score = 0, fresh_score = 0, score = 0;
  bool demoted = false;  // headline matched a DEMOTE_PHRASES entry

  // Filled in only for events that make it into the edition.
  std::string summary, micro, extended;
  bool ai = false;

  bool published = false;
  std::string drop_reason;  // why it was not published (for the log)
};

struct SourceLink {
  std::string url, publisher;
};

// An event as stored in an edition file and shown on the site.
struct PublishedEvent {
  std::string id, scope, category;
  double score = 0;
  int n_publishers = 0, n_articles = 0;
  std::string timestamp;  // ISO 8601
  std::string summary, micro, extended;
  std::vector<SourceLink> sources;
  bool ai = false;
};

struct Edition {
  std::string date;  // YYYY-MM-DD
  std::vector<PublishedEvent> events;
};

}  // namespace mt
