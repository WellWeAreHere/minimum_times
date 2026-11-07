#include "pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>

#include "cluster.hpp"
#include "extract.hpp"
#include "http.hpp"
#include "render.hpp"
#include "rss.hpp"
#include "score.hpp"
#include "summarize.hpp"
#include "util.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace mt {

namespace {

constexpr int kMaxRelatedPerStory = 5;      // outlets read from Google's cluster, besides the main one
constexpr int kFetchedRelatedPerStory = 2;  // of those, how many get their text downloaded

struct FeedResult {
  cfg::Feed feed;
  std::vector<Article> articles;
  long status = 0;
  std::string error;
};

FeedResult fetch_feed(const cfg::Feed& feed, int feed_index, int cap) {
  const int max_items = std::min(feed.max_items, cap);
  FeedResult result;
  result.feed = feed;
  HttpOptions opt;
  opt.timeout_seconds = 30;
  opt.headers = {"Accept: application/rss+xml, application/xml, text/xml;q=0.9"};

  for (int attempt = 1; attempt <= 2; ++attempt) {
    HttpResponse r = http_get(feed.url, opt);
    result.status = r.status;
    if (!r.ok()) {
      result.error = r.status ? "HTTP " + std::to_string(r.status) : r.error;
    } else {
      auto items = parse_rss(r.body, max_items);
      if (items.empty()) {
        result.error = "empty or unreadable feed";
      } else {
        int rank = 0;
        for (const auto& item : items) {
          Article a;
          a.scope = feed.scope;
          a.category = feed.category;
          a.title = strip_publisher_suffix(item.title);
          a.feed_link = item.link;
          a.publisher = item.source_name.empty() ? host_of(item.source_url) : item.source_name;
          a.published = parse_rfc822(item.pub_date);
          a.feed_rank = rank;
          a.feed_size = static_cast<int>(items.size());
          a.group = std::to_string(feed_index) + ":" + std::to_string(rank);
          const Article primary = a;
          result.articles.push_back(std::move(a));

          // The same story as reported by other outlets, straight from Google's own grouping.
          // They share the story's rank, time and group. Only the first few get their text
          // downloaded; the rest still count as extra outlets covering the story.
          int related_index = 0;
          for (const auto& rel : item.related) {
            if (related_index >= kMaxRelatedPerStory) break;
            Article r = primary;
            r.title = strip_publisher_suffix(rel.title);
            r.feed_link = rel.link;
            r.publisher = rel.source_name;
            r.fetch = related_index < kFetchedRelatedPerStory;
            ++related_index;
            result.articles.push_back(std::move(r));
          }
          ++rank;
        }
        result.error.clear();
        return result;
      }
    }
    if (attempt == 1) std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  return result;
}

std::string env_or(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return v && *v ? v : fallback;
}

std::string event_headline(const Event& e, const std::vector<Article>& all) {
  const Article* best = &all[e.articles.front()];
  for (int i : e.articles)
    if (all[i].feed_rank < best->feed_rank) best = &all[i];
  return best->title;
}

}  // namespace

std::vector<Event> build_events(const std::vector<Article>& all, const std::string& scope) {
  std::vector<int> idx;
  for (int i = 0; i < static_cast<int>(all.size()); ++i)
    if (all[i].scope == scope) idx.push_back(i);
  std::vector<Event> events;
  if (idx.empty()) return events;

  // One "document" per article: headline (counted twice, it is the most telling part) + text.
  std::vector<std::vector<std::string>> body_docs, title_docs;
  for (int i : idx) {
    auto title = tokenize(all[i].title);
    auto body = title;
    body.insert(body.end(), title.begin(), title.end());
    auto text = tokenize(all[i].text);
    body.insert(body.end(), text.begin(), text.end());
    body_docs.push_back(std::move(body));
    title_docs.push_back(std::move(title));
  }
  TfIdf body_model, title_model;
  body_model.fit(body_docs);
  title_model.fit(title_docs);
  std::vector<SparseVec> body_vecs, title_vecs;
  for (std::size_t k = 0; k < idx.size(); ++k) {
    body_vecs.push_back(body_model.transform(body_docs[k]));
    title_vecs.push_back(title_model.transform(title_docs[k]));
  }

  const std::size_t n = idx.size();
  std::vector<std::vector<double>> sim(n, std::vector<double>(n, 0.0));
  for (std::size_t a = 0; a < n; ++a)
    for (std::size_t b = a + 1; b < n; ++b)
      sim[a][b] = sim[b][a] =
          (!all[idx[a]].group.empty() && all[idx[a]].group == all[idx[b]].group)
              ? 1.0  // Google already says these are the same story
              : cfg::BODY_WEIGHT * cosine(body_vecs[a], body_vecs[b]) +
                    cfg::TITLE_WEIGHT * cosine(title_vecs[a], title_vecs[b]);

  for (const auto& group : cluster_average_link(sim, cfg::CLUSTER_THRESHOLD)) {
    Event e;
    e.scope = scope;
    for (int g : group) e.articles.push_back(idx[g]);
    events.push_back(std::move(e));
  }
  return events;
}

std::vector<std::string> drop_repeats(std::vector<PublishedEvent>& events,
                                      const std::vector<Edition>& previous, double threshold) {
  std::vector<std::string> dropped;
  struct Old {
    std::string scope, category;
    std::set<std::string> urls;
    std::vector<std::string> tokens;
  };
  std::vector<Old> olds;
  for (const auto& ed : previous)
    for (const auto& ev : ed.events) {
      Old o;
      o.scope = ev.scope;
      o.category = ev.category;
      for (const auto& s : ev.sources) o.urls.insert(s.url);
      o.tokens = tokenize(ev.summary + " " + ev.extended);
      olds.push_back(std::move(o));
    }
  if (olds.empty() || events.empty()) return dropped;

  std::vector<std::vector<std::string>> docs;
  for (const auto& o : olds) docs.push_back(o.tokens);
  for (const auto& ev : events) docs.push_back(tokenize(ev.summary + " " + ev.extended));
  TfIdf model;
  model.fit(docs);
  std::vector<SparseVec> old_vecs;
  for (std::size_t i = 0; i < olds.size(); ++i) old_vecs.push_back(model.transform(docs[i]));

  std::vector<PublishedEvent> kept;
  for (std::size_t k = 0; k < events.size(); ++k) {
    const auto& ev = events[k];
    SparseVec v = model.transform(docs[olds.size() + k]);
    bool repeat = false;
    for (std::size_t i = 0; i < olds.size() && !repeat; ++i) {
      if (olds[i].scope != ev.scope) continue;
      for (const auto& s : ev.sources)
        if (olds[i].urls.count(s.url)) repeat = true;      // same article published before
      if (cosine(v, old_vecs[i]) >= threshold) repeat = true;  // same story, other words
    }
    if (repeat) {
      dropped.push_back(ev.id);
    } else {
      kept.push_back(ev);
    }
  }
  events = std::move(kept);
  return dropped;
}

int run_pipeline(const Options& opt) {
  const std::string date = opt.date.empty() ? today_ist() : opt.date;
  if (!valid_date(date)) {
    std::cerr << "Invalid date: " << date << " (use YYYY-MM-DD)\n";
    return 1;
  }
  const std::string api_key = opt.use_ai ? env_or("NVIDIA_API_KEY", "") : "";
  const std::string model = env_or("NVIDIA_MODEL", cfg::AI_DEFAULT_MODEL);
  std::cout << "Edition " << date << (api_key.empty() ? " (no AI: extractive summaries)\n"
                                                      : " (AI summaries)\n");

  // ---- 1. feeds ----
  auto feed_defs = cfg::feeds();
  std::vector<FeedResult> feed_results(feed_defs.size());
  parallel_for(feed_defs.size(), 3, [&](std::size_t i) {
    feed_results[i] = fetch_feed(feed_defs[i], static_cast<int>(i), opt.max_per_feed);
  });
  std::vector<Article> articles;
  int feeds_ok = 0, feeds_503 = 0;
  for (auto& fr : feed_results) {
    if (fr.error.empty()) {
      ++feeds_ok;
      for (auto& a : fr.articles) articles.push_back(std::move(a));
    } else {
      std::cerr << "feed failed " << fr.feed.scope << "/" << fr.feed.category << ": " << fr.error << "\n";
      if (fr.status == 503) ++feeds_503;
    }
  }
  if (feeds_ok == 0) {
    if (feeds_503 == static_cast<int>(feed_results.size())) {
      std::cerr << "Every feed returned HTTP 503; ask the caller to retry later.\n";
      return 75;
    }
    std::cerr << "No feed could be read.\n";
    return 1;
  }
  std::cout << feeds_ok << "/" << feed_results.size() << " feeds read, " << articles.size()
            << " articles\n";

  // ---- 2. real article text (each distinct link once) ----
  std::map<std::string, std::size_t> first_with_link;
  std::vector<std::size_t> unique;
  for (std::size_t i = 0; i < articles.size(); ++i) {
    if (!articles[i].fetch) continue;  // counted as an outlet, but its text is not downloaded
    if (first_with_link.emplace(articles[i].feed_link, i).second) unique.push_back(i);
  }
  std::atomic<int> done{0};
  parallel_for(unique.size(), cfg::FETCH_THREADS, [&](std::size_t k) {
    fetch_article_text(articles[unique[k]]);
    int d = ++done;
    if (d % 25 == 0 || d == static_cast<int>(unique.size()))
      std::cout << "  text fetched " << d << "/" << unique.size() << "\n" << std::flush;
  });
  for (auto& a : articles) {
    auto found = first_with_link.find(a.feed_link);
    if (found == first_with_link.end()) continue;  // never fetched
    const Article& src = articles[found->second];
    if (&src == &a) continue;
    a.url = src.url;
    a.text = src.text;
    a.status = src.status;
    a.reason = src.reason;
    if (a.publisher.empty()) a.publisher = src.publisher;
  }
  std::map<std::string, int> status_counts;
  for (std::size_t i : unique) ++status_counts[status_name(articles[i].status)];
  std::cout << "text extraction:";
  for (const auto& [name, count] : status_counts) std::cout << " " << name << "=" << count;
  std::cout << "\n";

  // ---- 3. group, grade, select ----
  std::vector<Event> all_events;
  for (const auto& scope : cfg::SCOPES) {
    auto events = build_events(articles, scope);
    for (auto& e : events) all_events.push_back(std::move(e));
  }
  const std::time_t now = std::time(nullptr);
  for (auto& e : all_events) {
    e.category = assign_category(e, articles);
    score_event(e, articles, now);
  }

  std::map<std::pair<std::string, std::string>, std::vector<Event*>> buckets;
  for (auto& e : all_events) {
    bool has_text = std::any_of(e.articles.begin(), e.articles.end(),
                                [&](int i) { return articles[i].has_text(); });
    if (!has_text) e.drop_reason = "no_extracted_text";
    else if (e.n_publishers < cfg::MIN_PUBLISHERS) e.drop_reason = "single_publisher";
    else if (e.score < cfg::PUBLISH_THRESHOLD) e.drop_reason = "below_threshold";
    else buckets[{e.scope, e.category}].push_back(&e);
  }
  std::vector<Event*> chosen;
  for (auto& [key, list] : buckets) {
    std::sort(list.begin(), list.end(), [](const Event* a, const Event* b) {
      if (a->score != b->score) return a->score > b->score;
      return a->n_publishers > b->n_publishers;
    });
    int number = 0;
    for (Event* e : list) {
      if (number >= cfg::MAX_EVENTS_PER_CATEGORY) {
        e->drop_reason = "category_full";
        continue;
      }
      e->id = key.first + "-" + key.second + "-" + std::to_string(++number);
      chosen.push_back(e);
    }
  }

  // ---- 4. summaries (the only AI step) ----
  std::cout << "summarizing " << chosen.size() << " events\n";
  parallel_for(chosen.size(), cfg::AI_THREADS, [&](std::size_t k) {
    Summary s = summarize(*chosen[k], articles, api_key, model);
    chosen[k]->summary = s.summary;
    chosen[k]->micro = s.micro;
    chosen[k]->extended = s.extended;
    chosen[k]->ai = s.ai;
  });

  Edition edition;
  edition.date = date;
  for (Event* e : chosen) {
    PublishedEvent pe;
    pe.id = e->id;
    pe.scope = e->scope;
    pe.category = e->category;
    pe.score = std::round(e->score * 1000) / 1000;
    pe.n_publishers = e->n_publishers;
    pe.n_articles = e->n_articles;
    pe.timestamp = e->newest ? format_iso(e->newest) : "";
    pe.summary = e->summary;
    pe.micro = e->micro;
    pe.extended = e->extended;
    pe.ai = e->ai;
    std::set<std::string> seen_publishers;
    for (int i : e->articles) {
      const Article& a = articles[i];
      if (a.url.empty() || !seen_publishers.insert(to_lower(a.publisher)).second) continue;
      if (pe.sources.size() < 6) pe.sources.push_back({a.url, a.publisher});
    }
    edition.events.push_back(std::move(pe));
  }

  // ---- 5. drop what the last editions already told the reader ----
  auto previous = load_recent_editions(opt.out_dir, date, cfg::PREVIOUS_EDITIONS_TO_CHECK);
  auto dropped = drop_repeats(edition.events, previous, cfg::PREVIOUS_SIMILARITY_THRESHOLD);
  for (const auto& id : dropped) {
    std::cout << "  dropped repeat of an earlier edition: " << id << "\n";
    for (Event* e : chosen)
      if (e->id == id) e->drop_reason = "repeat_of_previous_edition";
  }
  for (Event* e : chosen)
    if (e->drop_reason.empty()) e->published = true;

  // ---- 6. write the site and the log ----
  write_site(opt.out_dir, edition);

  fs::create_directories(opt.log_dir);
  std::ofstream log(fs::path(opt.log_dir) / (date + ".jsonl"), std::ios::binary);
  for (const auto& e : all_events) {
    json titles = json::array(), publishers = json::array();
    std::set<std::string> pubs;
    for (int i : e.articles) {
      titles.push_back(articles[i].title);
      if (pubs.insert(articles[i].publisher).second) publishers.push_back(articles[i].publisher);
    }
    log << json{{"type", "event"},
                {"date", date},
                {"scope", e.scope},
                {"category", e.category},
                {"headline", event_headline(e, articles)},
                {"n_articles", e.n_articles},
                {"n_publishers", e.n_publishers},
                {"avg_rank", e.avg_rank},
                {"age_hours", e.age_hours},
                {"coverage", e.coverage},
                {"rank_score", e.rank_score},
                {"fresh_score", e.fresh_score},
                {"score", e.score},
                {"demoted", e.demoted},
                {"published", e.published},
                {"drop_reason", e.drop_reason},
                {"publishers", publishers},
                {"titles", titles}}
               .dump()
        << "\n";
  }
  log << json{{"type", "run"},
              {"date", date},
              {"feeds_ok", feeds_ok},
              {"articles", articles.size()},
              {"extraction", status_counts},
              {"events_total", all_events.size()},
              {"events_published", edition.events.size()}}
             .dump()
      << "\n";

  std::cout << "Published " << edition.events.size() << " events to " << opt.out_dir << "/\n";
  std::map<std::string, int> per;
  for (const auto& ev : edition.events) ++per[ev.scope + "/" + ev.category];
  for (const auto& [k, v] : per) std::cout << "  " << k << ": " << v << "\n";
  return 0;
}

}  // namespace mt
