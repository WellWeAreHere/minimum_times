#include "summarize.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <map>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

#include "config.hpp"
#include "http.hpp"
#include "util.hpp"

namespace mt {

namespace {

// Digit runs in a text with thousands commas removed ("1,200" -> "1200").
std::set<std::string> numbers_in(const std::string& text) {
  std::set<std::string> out;
  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) out.insert(cur);
    cur.clear();
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (std::isdigit(c)) {
      cur.push_back(static_cast<char>(c));
    } else if (c == ',' && !cur.empty() && i + 1 < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
      continue;  // thousands separator
    } else {
      flush();
    }
  }
  flush();
  return out;
}

// The articles of an event that have real text, best (most widely worded) first.
std::vector<const Article*> usable_articles(const Event& e, const std::vector<Article>& all) {
  std::vector<const Article*> v;
  for (int i : e.articles)
    if (all[i].has_text()) v.push_back(&all[i]);
  std::sort(v.begin(), v.end(), [](const Article* a, const Article* b) {
    if (a->text.size() != b->text.size()) return a->text.size() > b->text.size();
    return a->feed_rank < b->feed_rank;
  });
  return v;
}

std::string clean_headline(const Article& a) { return strip_publisher_suffix(a.title); }

// Ends on a full stop if the text does not already end with punctuation.
std::string finish_sentence(std::string s) {
  s = trim(s);
  if (s.empty()) return s;
  char last = s.back();
  if (last != '.' && last != '!' && last != '?' && last != '"') s.push_back('.');
  return s;
}

std::string strip_json_fence(std::string s) {
  s = trim(s);
  if (starts_with(s, "```")) {
    auto nl = s.find('\n');
    if (nl != std::string::npos) s = s.substr(nl + 1);
    if (ends_with(s, "```")) s = s.substr(0, s.size() - 3);
  }
  return trim(s);
}

}  // namespace

bool numbers_supported(const std::string& summary, const std::string& source_text) {
  auto source = numbers_in(source_text);
  for (const auto& n : numbers_in(summary)) {
    if (!source.count(n)) return false;
  }
  return true;
}

std::string check_summary(const Summary& s, const std::string& source_text) {
  if (trim(s.summary).empty() || trim(s.micro).empty() || trim(s.extended).empty())
    return "empty_field";
  if (word_count(s.summary) > cfg::SUMMARY_MAX_WORDS) return "summary_too_long";
  if (word_count(s.micro) > cfg::MICRO_MAX_WORDS) return "micro_too_long";
  if (word_count(s.extended) > cfg::EXTENDED_MAX_WORDS + 25) return "extended_too_long";
  for (const std::string* field : {&s.summary, &s.micro, &s.extended}) {
    std::string low = to_lower(*field);
    for (const char* banned : {"this article", "the article", "according to", "the report states",
                               "the reports", "as an ai", "source:"}) {
      if (contains(low, banned)) return std::string("meta_language:") + banned;
    }
  }
  if (!numbers_supported(s.summary + " " + s.micro + " " + s.extended, source_text))
    return "number_not_in_sources";
  return "";
}

Summary summarize_extractive(const Event& e, const std::vector<Article>& all) {
  Summary out;
  auto usable = usable_articles(e, all);

  // The lead headline: the one Google ranked highest.
  const Article* lead = &all[e.articles.front()];
  for (int i : e.articles)
    if (all[i].feed_rank < lead->feed_rank) lead = &all[i];
  std::string headline = clean_headline(*lead);

  // What the event is "about": words that appear in several of its headlines count more.
  std::map<std::string, int> topic;
  for (int i : e.articles) {
    std::set<std::string> seen;
    for (const auto& t : tokenize(all[i].title)) seen.insert(t);
    for (const auto& t : seen) ++topic[t];
  }
  auto relevance = [&](const std::string& sentence) {
    std::set<std::string> seen;
    for (const auto& t : tokenize(sentence)) seen.insert(t);
    if (seen.empty()) return 0.0;
    double score = 0;
    for (const auto& t : seen) {
      auto it = topic.find(t);
      if (it != topic.end()) score += it->second;
    }
    return score / std::sqrt(static_cast<double>(seen.size()));
  };

  // Look at the opening sentences of every article, keep the one that best matches the topic and
  // is not just the headline repeated. Also remember which article is the most on-topic overall.
  std::string best_sentence;
  double best_sentence_score = 0;
  const Article* best_article = usable.empty() ? nullptr : usable.front();
  double best_article_score = -1;
  std::set<std::string> headline_tokens;
  for (const auto& t : tokenize(headline)) headline_tokens.insert(t);
  for (const Article* a : usable) {
    auto sentences = split_sentences(a->text);
    double article_score = 0;
    for (std::size_t k = 0; k < sentences.size() && k < 6; ++k) {
      const std::string& sentence = sentences[k];
      int w = word_count(sentence);
      double r = relevance(sentence);
      article_score += r;
      if (w < 8 || w > 35) continue;
      std::set<std::string> st;
      for (const auto& t : tokenize(sentence)) st.insert(t);
      int shared = 0;
      for (const auto& t : st) shared += static_cast<int>(headline_tokens.count(t));
      bool repeats_headline = !st.empty() && shared >= 0.8 * static_cast<double>(st.size());
      if (!repeats_headline && r > best_sentence_score) {
        best_sentence_score = r;
        best_sentence = sentence;
      }
    }
    if (article_score > best_article_score) {
      best_article_score = article_score;
      best_article = a;
    }
  }

  // micro: the cleaned headline when it fits, otherwise a trimmed one.
  out.micro = finish_sentence(limit_words(headline, cfg::MICRO_MAX_WORDS));

  // summary: headline, plus the best supporting sentence when it still fits the limit.
  std::string summary = finish_sentence(headline);
  if (!best_sentence.empty()) {
    std::string candidate = summary + " " + best_sentence;
    if (word_count(candidate) <= cfg::SUMMARY_MAX_WORDS) summary = candidate;
  }
  out.summary = limit_words(summary, cfg::SUMMARY_MAX_WORDS);

  // extended: the opening of the most on-topic article.
  out.extended = best_article ? limit_words(best_article->text, cfg::EXTENDED_MAX_WORDS) : out.summary;
  out.ai = false;
  return out;
}

std::optional<Summary> summarize_ai(const Event& e, const std::vector<Article>& all,
                                    const std::string& api_key, const std::string& model,
                                    std::string& error) {
  using nlohmann::json;
  auto usable = usable_articles(e, all);
  if (usable.empty()) {
    error = "no_extracted_text";
    return std::nullopt;
  }

  std::string reports, source_text;
  int used = 0;
  for (const Article* a : usable) {
    if (used++ == 2) break;  // two reports are plenty, and a short prompt means less thinking time
    std::string t = a->text.substr(0, 2500);
    reports += t + "\n\n";
    source_text += t + " ";
  }
  for (const Article* a : usable) source_text += clean_headline(*a) + " ";

  const std::string prompt =
      "You are a concise, neutral news editor. Combine the supplied reports about ONE real-world "
      "event. Use only the supplied text; do not invent facts or numbers and do not use outside "
      "knowledge. NEVER mention any publisher, source, website, report, article or journalist. "
      "Never begin with meta language such as \"This article covers\" or \"According to\". Begin "
      "directly with the event and its facts. Write finished news summaries, not commentary.\n\n"
      "micro_summary: one complete, understandable sentence of at most " +
      std::to_string(cfg::MICRO_MAX_WORDS) +
      " words naming the main subject and the key action or result; include the most important "
      "number or score if it fits.\n"
      "summary: a standalone news summary of at most " +
      std::to_string(cfg::SUMMARY_MAX_WORDS) +
      " words with the essential event, action, result and concrete facts.\n"
      "extended_summary: 100-" +
      std::to_string(cfg::EXTENDED_MAX_WORDS) +
      " factual words.\n"
      "For sports, always include the exact score when it appears in the text.\n\n"
      "Return ONLY valid JSON: {\"summary\":\"...\",\"micro_summary\":\"...\",\"extended_summary\":\"...\"}\n\n"
      "CATEGORY: " + e.category + "\n\nREPORTS:\n\n" + reports;

  json body = {
      {"model", model},
      {"temperature", 0.3},
      {"top_p", 0.95},
      {"max_tokens", 8000},  // room for the model's thinking plus the ~400-token answer
      {"response_format", {{"type", "json_object"}}},
      {"messages", json::array({{{"role", "user"}, {"content", prompt}}})},
  };

  HttpOptions opt;
  opt.timeout_seconds = 420;
  opt.headers = {"Content-Type: application/json", "Authorization: Bearer " + api_key};

  // Nemotron models "think" before answering by default. Those hidden tokens count against
  // max_tokens, and when they use the whole budget the visible reply comes back null.
  // Reasoning models "think" before answering, which is slow and counts against max_tokens.
  // Mistral Large 4 cannot switch it off ("none" is rejected), so the lowest it accepts is "low".
  // Models that accept "none" (Nemotron) can use it: set NVIDIA_REASONING_EFFORT=none.
  // If a model rejects the setting entirely, the request is retried once without it (below).
  {
    const char* effort = std::getenv("NVIDIA_REASONING_EFFORT");
    body["reasoning_effort"] = effort && *effort ? effort : "low";
  }

  std::string feedback;  // why the previous attempt was rejected, sent back on the retry
  for (int attempt = 1; attempt <= 2; ++attempt) {
    if (!feedback.empty()) {
      body["messages"] = json::array({{{"role", "user"},
                                       {"content", prompt + "\n\nYour previous answer was rejected: " +
                                                       feedback +
                                                       ". Fix exactly that and return the JSON again."}}});
    }
    HttpResponse r = http_post(cfg::AI_ENDPOINT, body.dump(), opt);
    if ((r.status == 400 || r.status == 422) && body.contains("reasoning_effort")) {
      body.erase("reasoning_effort");  // this model rejects the setting: try again without it
      r = http_post(cfg::AI_ENDPOINT, body.dump(), opt);
    }
    if (r.status == 0 && r.error.find("Timeout") != std::string::npos) {
      error = "ai_timeout";  // the model is just slow right now: retrying would only double the wait
      break;
    }
    if (!r.ok()) {
      error = r.status ? "ai_http_" + std::to_string(r.status) + ":" + r.body.substr(0, 200)
                       : "ai_" + r.error;
      continue;
    }
    try {
      json reply = json::parse(r.body);
      const json& choice = reply.at("choices").at(0);
      const json& message = choice.at("message");
      if (!message.contains("content") || !message["content"].is_string()) {
        std::string finish = choice.value("finish_reason", "unknown");
        error = "ai_empty_content:finish_reason=" + finish;
        continue;
      }
      std::string content = message["content"].get<std::string>();
      json parsed = json::parse(strip_json_fence(content));
      Summary s;
      s.summary = collapse_ws(parsed.at("summary").get<std::string>());
      s.micro = collapse_ws(parsed.at("micro_summary").get<std::string>());
      s.extended = collapse_ws(parsed.at("extended_summary").get<std::string>());
      s.ai = true;
      std::string problem = check_summary(s, source_text);
      if (problem.empty()) return s;
      error = "ai_rejected:" + problem;
      feedback = problem == "summary_too_long"
                     ? "summary had " + std::to_string(word_count(s.summary)) + " words but the maximum is " +
                           std::to_string(cfg::SUMMARY_MAX_WORDS)
                 : problem == "micro_too_long"
                     ? "micro_summary had " + std::to_string(word_count(s.micro)) + " words but the maximum is " +
                           std::to_string(cfg::MICRO_MAX_WORDS)
                 : problem == "extended_too_long"
                     ? "extended_summary was too long; the maximum is " + std::to_string(cfg::EXTENDED_MAX_WORDS) +
                           " words"
                 : problem == "number_not_in_sources"
                     ? "it contained a number that is not in the reports; use only numbers that appear in the reports"
                     : problem;
    } catch (const std::exception& ex) {
      error = std::string("ai_bad_reply:") + ex.what();
    }
  }
  return std::nullopt;
}

Summary summarize(const Event& e, const std::vector<Article>& all, const std::string& api_key,
                  const std::string& model) {
  if (!api_key.empty()) {
    std::string error;
    if (auto s = summarize_ai(e, all, api_key, model, error)) return *s;
    std::cerr << "summary fallback for " << e.id << ": " << error << "\n";
  }
  return summarize_extractive(e, all);
}

}  // namespace mt
