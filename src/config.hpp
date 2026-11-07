// Every number you might want to tune lives in this one file.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace mt::cfg {

// ---- what to publish ----
inline constexpr int MAX_EVENTS_PER_CATEGORY = 4;
inline constexpr int MIN_PUBLISHERS = 2;           // an event needs this many different outlets
inline constexpr double PUBLISH_THRESHOLD = 0.35;  // minimum importance score (0..1)

// ---- grouping articles into events ----
// Two articles are "the same event" when their blended similarity is at least this (0..1).
// Lower = more merging, higher = more separate events.
inline constexpr double CLUSTER_THRESHOLD = 0.30;
inline constexpr double BODY_WEIGHT = 0.7;   // similarity of title+text
inline constexpr double TITLE_WEIGHT = 0.3;  // similarity of headlines only

// ---- importance score = weighted mix of three things, each 0..1 ----
inline constexpr double W_COVERAGE = 0.6;        // how many different outlets covered it
inline constexpr double W_RANK = 0.2;            // how high Google ranked it
inline constexpr double W_FRESH = 0.2;           // how recent it is
inline constexpr int COVERAGE_SATURATION = 8;    // outlets at which coverage reaches 1.0
inline constexpr int FEED_ITEMS = 20;            // items read from each India keyword feed
inline constexpr double FRESH_DECAY_HOURS = 24;  // freshness halves roughly every ~17 hours

// ---- keeping local / off-topic stories out ----
// Headlines containing any of these phrases get their score multiplied by DEMOTE_FACTOR.
inline const std::vector<std::string> DEMOTE_PHRASES = {"high school", "ihsaa", "ihsa", "athletics"};
inline constexpr double DEMOTE_FACTOR = 0.5;

// An event's category = its feed votes + KEYWORD_WEIGHT for each distinct category word found in
// its headlines. This fixes stories a keyword feed put in the wrong section (e.g. a "stock market
// crash" found by the tragedies feed). Words are matched after light stemming (trailing "s" dropped).
inline constexpr double KEYWORD_WEIGHT = 2.0;
inline const std::map<std::string, std::vector<std::string>> CATEGORY_KEYWORDS = {
    {"politics", {"government", "parliament", "election", "minister", "court", "president", "senate",
                  "congress", "vote", "voter", "poll", "bill", "law", "policy", "party", "prime",
                  "supreme", "verdict", "ruling", "treaty", "sanction", "diplomat", "cabinet",
                  "lawmaker", "governor", "mayor", "campaign", "bjp", "trump", "modi", "tribunal",
                  "judge", "talks", "summit", "war", "ceasefire", "military", "troops", "peace",
                  "protest", "protesters", "arrest", "arrested", "pope", "royal", "king", "queen"}},
    {"sports", {"match", "cricket", "football", "tennis", "league", "cup", "tournament", "wicket",
                "score", "goal", "win", "beat", "champion", "olympics", "medal", "playoff", "final",
                "coach", "team", "player", "ipl", "nba", "nfl", "fifa", "innings", "series", "game",
                "season", "defeat", "squad", "odi", "t20", "retires", "hockey", "badminton", "grand",
                "prix", "formula", "rugby", "mlb", "premier", "uefa", "olympic", "athlete"}},
    {"business", {"stock", "market", "economy", "inflation", "company", "bank", "trade", "jobs",
                  "earnings", "revenue", "profit", "share", "sensex", "nifty", "investor", "budget",
                  "tax", "gdp", "rate", "merger", "ipo", "tariff", "price", "oil", "rupee", "dollar",
                  "startup", "fund", "loan", "wall", "street"}},
    {"science", {"science", "scientist", "research", "study", "technology", "tech", "space", "nasa",
                 "isro", "artificial", "intelligence", "cyber", "hack", "breach", "satellite",
                 "launch", "robot", "quantum", "chip", "semiconductor", "software", "climate",
                 "discovery", "vaccine", "rocket", "mission", "semicon", "astronaut", "telescope",
                 "asteroid", "openai", "chatgpt", "iphone", "google", "apple", "microsoft"}},
    {"entertainment", {"film", "movie", "actor", "actress", "music", "song", "album", "show",
                       "bollywood", "hollywood", "award", "emmy", "oscar", "festival", "director",
                       "trailer", "singer", "concert", "celebrity", "netflix", "release",
                       "premiere", "cast"}},
    {"tragedies", {"killed", "dead", "death", "died", "injured", "dies", "fire", "crash", "earthquake",
                   "flood", "explosion", "collapse", "accident", "tragedy", "victim", "disaster",
                   "blast", "storm", "hurricane", "cyclone", "wildfire", "shooting", "attack",
                   "missing", "rescue", "toll", "casualty", "drowned", "landslide", "hurt", "arson",
                   "bomb", "terror", "massacre", "plane", "derail", "wreck", "shot", "stabbed",
                   "stabbing", "knife", "hostage", "gunman", "gun", "murder", "kidnap"}},
};

// ---- repeats from earlier editions ----
inline constexpr double PREVIOUS_SIMILARITY_THRESHOLD = 0.5;
inline constexpr int PREVIOUS_EDITIONS_TO_CHECK = 2;

// ---- article text extraction ----
inline constexpr int MIN_SHORT_WORDS = 40;
inline constexpr int MIN_FULL_WORDS = 120;
inline constexpr int MAX_TEXT_CHARS = 6000;

// ---- summary limits ----
inline constexpr int SUMMARY_MAX_WORDS = 40;
inline constexpr int MICRO_MAX_WORDS = 20;
inline constexpr int EXTENDED_MAX_WORDS = 150;

// ---- networking ----
inline constexpr int HTTP_TIMEOUT_SECONDS = 15;
inline constexpr int FETCH_THREADS = 6;
inline constexpr int AI_THREADS = 8;  // summaries are slow (reasoning model), so run many at once

inline const char* AI_ENDPOINT = "https://integrate.api.nvidia.com/v1/chat/completions";
inline const char* AI_DEFAULT_MODEL = "mistralai/mistral-large-4-1t-a52b";

inline const std::vector<std::string> SCOPES = {"national", "international"};
inline const std::vector<std::string> CATEGORIES = {
    "politics", "sports", "business", "science", "entertainment", "tragedies"};

struct Feed {
  std::string scope;
  std::string category;  // "world" = not split by Google; our rules sort it into categories
  std::string url;
  int max_items;
};

// India: 6 keyword searches, one per category.
// International: Google's World topic feed (non-US edition). Google does not split it into
// categories, so pipeline/score.cpp sorts each event into one using the keyword lists above.
inline constexpr int WORLD_FEED_ITEMS = 60;
inline const char* WORLD_FEED_URL =
    "https://news.google.com/rss/topics/CAAqJggKIiBDQkFTRWdvSUwyMHZNRGx1YlY4U0FtVnVHZ0pWVXlnQVAB"
    "?hl=en-GB&gl=GB&ceid=GB:en";

// International sports: Google's Sports topic feed from several countries' editions, so the
// section is not tied to one country's sport. Only English editions work with this pipeline.
// Germany, China and Hong Kong have no English edition on Google News (the request is redirected).
inline constexpr int SPORTS_FEED_ITEMS = 20;
inline const char* SPORTS_TOPIC_ID = "CAAqJggKIiBDQkFTRWdvSUwyMHZNRFp1ZEdvU0FtVnVHZ0pWVXlnQVAB";
struct Edition {
  const char* hl;
  const char* gl;
  const char* ceid;
};
inline const std::vector<Edition> SPORTS_EDITIONS = {
    {"en-US", "US", "US:en"},  // United States
    {"en-GB", "GB", "GB:en"},  // United Kingdom
    {"en-AU", "AU", "AU:en"},  // Australia
    {"en-ZA", "ZA", "ZA:en"},  // South Africa
};

inline std::vector<Feed> feeds() {
  const std::string in = "&hl=en-IN&gl=IN&ceid=IN:en";
  const std::string base = "https://news.google.com/rss/search?q=";
  const int n = FEED_ITEMS;
  std::vector<Feed> list;
  for (const auto& ed : SPORTS_EDITIONS) {
    list.push_back({"international", "sports",
                    std::string("https://news.google.com/rss/topics/") + SPORTS_TOPIC_ID +
                        "?hl=" + ed.hl + "&gl=" + ed.gl + "&ceid=" + ed.ceid,
                    SPORTS_FEED_ITEMS});
  }
  const std::vector<Feed> core = {
      {"national", "politics", base + "India+(government+OR+parliament+OR+election+OR+minister+OR+court)+when:1d" + in, n},
      {"national", "sports", base + "India+(sports+OR+cricket+OR+football)+when:1d" + in, n},
      {"national", "business", base + "India+(business+OR+economy+OR+market+OR+company+OR+inflation+OR+budget)+when:1d" + in, n},
      {"national", "science", base + "India+(science+OR+technology+OR+AI+OR+space+OR+cybersecurity)+when:1d" + in, n},
      {"national", "entertainment", base + "India+(actor+OR+film+OR+music+OR+entertainment)+when:1d" + in, n},
      {"national", "tragedies", base + "India+(earthquake+OR+accident+OR+fire+OR+explosion+OR+flood+OR+crash)+when:1d" + in, n},
      {"international", "world", WORLD_FEED_URL, WORLD_FEED_ITEMS},
  };
  list.insert(list.end(), core.begin(), core.end());
  return list;
}

}  // namespace mt::cfg
