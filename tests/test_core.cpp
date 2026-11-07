// Tests for everything that does not need the network.
// Run with:  ctest --test-dir build --output-on-failure
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "cluster.hpp"
#include "extract.hpp"
#include "gnews.hpp"
#include "pipeline.hpp"
#include "render.hpp"
#include "rss.hpp"
#include "score.hpp"
#include "summarize.hpp"
#include "util.hpp"

static int failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++failures;                                                                \
      std::cerr << __FILE__ << ":" << __LINE__ << "  CHECK failed: " #cond "\n"; \
    }                                                                            \
  } while (0)

using namespace mt;

static std::string b64url(const std::string& in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  int val = 0, bits = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    bits += 8;
    while (bits >= 0) {
      out.push_back(t[(val >> bits) & 63]);
      bits -= 6;
    }
  }
  if (bits > -6) out.push_back(t[((val << 8) >> (bits + 8)) & 63]);
  return out;
}

static Article make_article(const std::string& scope, const std::string& category,
                            const std::string& title, const std::string& text,
                            const std::string& publisher, int rank, std::time_t published) {
  Article a;
  a.scope = scope;
  a.category = category;
  a.title = title;
  a.feed_link = "https://news.google.com/rss/articles/" + publisher + title.substr(0, 8);
  a.url = "https://" + publisher + "/" + std::to_string(rank);
  a.publisher = publisher;
  a.published = published;
  a.feed_rank = rank;
  a.text = text;
  a.status = text.empty() ? TextStatus::Unresolved : TextStatus::FullExtraction;
  return a;
}

static void test_util() {
  auto tokens = tokenize("India beat Sri Lanka by 7 wickets, chasing 264; Padikkal scored 103.");
  auto has = [&](const std::string& w) { return std::find(tokens.begin(), tokens.end(), w) != tokens.end(); };
  CHECK(has("india"));
  CHECK(has("wicket"));  // light stemming
  CHECK(has("264"));
  CHECK(!has("by"));
  CHECK(!has("7"));  // single digits are noise

  CHECK(host_of("https://www.Example.com/a/b?x=1") == "example.com");
  CHECK(host_of("http://news.google.com:8080/x") == "news.google.com");
  CHECK(strip_publisher_suffix("India beat Sri Lanka by seven wickets - The Hindu") ==
        "India beat Sri Lanka by seven wickets");
  CHECK(strip_publisher_suffix("Short - X") == "Short - X");
  CHECK(limit_words("a b c d e", 3) == "a b c");
  CHECK(html_escape("<a href=\"x\">&</a>") == "&lt;a href=&quot;x&quot;&gt;&amp;&lt;/a&gt;");

  auto s = split_sentences("Mr. Rao said it. The U.S. agreed. Then 5 died!");
  CHECK(s.size() == 3);

  std::time_t t = parse_rfc822("Wed, 05 Nov 2025 10:00:00 GMT");
  CHECK(format_iso(t) == "2025-11-05T10:00:00Z");
  CHECK(format_iso(0) == "1970-01-01T00:00:00Z");
  CHECK(format_date_long("2026-11-07") == "Saturday, 7 November 2026");
  CHECK(valid_date("2026-11-07") && !valid_date("2026-13-07") && !valid_date("garbage"));
}

static void test_env_file() {
  const char* path = "test_env_file.tmp";
  {
    std::ofstream f(path);
    f << "# comment\n"
         "MT_TEST_PLAIN=abc\n"
         "export MT_TEST_QUOTED=\"two words\"\n"
         "  MT_TEST_SPACED = 'x'  \n"
         "MT_TEST_EMPTY=\n"
         "not a line\n";
  }
  CHECK(load_env_file(path) == 3);
  CHECK(std::string(std::getenv("MT_TEST_PLAIN")) == "abc");
  CHECK(std::string(std::getenv("MT_TEST_QUOTED")) == "two words");
  CHECK(std::string(std::getenv("MT_TEST_SPACED")) == "x");
  CHECK(std::getenv("MT_TEST_EMPTY") == nullptr);
  CHECK(load_env_file(path) == 0);  // already set: not overridden
  CHECK(load_env_file("no_such_file.env") == 0);
  std::remove(path);
}

static void test_rss() {
  const std::string xml =
      "<rss><channel><item><title>Big news - Paper</title><link>https://news.google.com/rss/articles/X?oc=5</link>"
      "<pubDate>Wed, 05 Nov 2025 10:00:00 GMT</pubDate><source url=\"https://www.paper.com\">Paper</source></item>"
      "<item><title></title><link>x</link></item></channel></rss>";
  auto items = parse_rss(xml, 10);
  CHECK(items.size() == 1);
  CHECK(items[0].source_url == "https://www.paper.com");
  CHECK(parse_rss("not xml at all", 10).empty());

  // Google's story cluster inside <description>: same story from other outlets.
  const std::string topic =
      "<rss><channel><item><title>Bomb threats hit flights - Al Jazeera</title>"
      "<link>https://news.google.com/rss/articles/AAA?oc=5</link>"
      "<pubDate>Wed, 05 Nov 2025 10:00:00 GMT</pubDate><source url=\"https://www.aljazeera.com\">Al Jazeera</source>"
      "<description>&lt;ol&gt;&lt;li&gt;&lt;a href=\"https://news.google.com/rss/articles/AAA?oc=5\" target=\"_blank\"&gt;Bomb threats hit flights&lt;/a&gt;"
      "&amp;nbsp;&amp;nbsp;&lt;font color=\"#6f6f6f\"&gt;Al Jazeera&lt;/font&gt;&lt;/li&gt;"
      "&lt;li&gt;&lt;a href=\"https://news.google.com/rss/articles/BBB?oc=5\" target=\"_blank\"&gt;New wave of threats &amp;amp; delays&lt;/a&gt;"
      "&amp;nbsp;&amp;nbsp;&lt;font color=\"#6f6f6f\"&gt;BBC&lt;/font&gt;&lt;/li&gt;"
      "&lt;li&gt;&lt;a href=\"https://news.google.com/rss/articles/CCC?oc=5\" target=\"_blank\"&gt;Flights disrupted&lt;/a&gt;"
      "&amp;nbsp;&amp;nbsp;&lt;font color=\"#6f6f6f\"&gt;The Guardian&lt;/font&gt;&lt;/li&gt;&lt;/ol&gt;</description></item></channel></rss>";
  auto t_items = parse_rss(topic, 10);
  CHECK(t_items.size() == 1);
  if (t_items.size() == 1) {
    CHECK(t_items[0].related.size() == 2);  // the item's own link is not listed again
    if (t_items[0].related.size() == 2) {
      CHECK(t_items[0].related[0].source_name == "BBC");
      CHECK(t_items[0].related[0].title == "New wave of threats & delays");
      CHECK(t_items[0].related[1].source_name == "The Guardian");
    }
  }
  CHECK(html_unescape("a &amp; b &#39;c&#39; &quot;d&quot; &lt;e&gt; &#8211;") == std::string("a & b 'c' \"d\" <e> ") + "\xE2\x80\x93");
}

static void test_gnews() {
  CHECK(gnews_article_id("https://news.google.com/rss/articles/CBMiabc?oc=5") == "CBMiabc");
  CHECK(gnews_article_id("https://example.com/articles/abc").empty());

  const std::string url = "https://www.example.com/world/story-123";
  std::string raw = std::string("\x08\x13\x22", 3) + std::string(1, static_cast<char>(url.size())) + url +
                    std::string("\xd2\x01\x00", 3);
  auto decoded = gnews_decode_offline(b64url(raw));
  CHECK(decoded && *decoded == url);
  CHECK(!gnews_decode_offline("AU_yqLabcdef"));  // new-style ids need Google
}

static std::string long_paragraph(const std::string& seed) {
  std::string p;
  for (int i = 0; i < 30; ++i) p += seed + " word" + std::to_string(i) + " ";
  return p;
}

static void test_extract() {
  std::string html =
      "<html><head><title>x</title><script>var a = 1;</script></head><body>"
      "<nav><p>Home News Sports Business Entertainment Tragedies Science Menu Item</p></nav>"
      "<article><h1>Headline</h1><p>" + long_paragraph("alpha") + "</p><p>" + long_paragraph("beta") +
      "</p><p>Read more</p></article><footer><p>Copyright all rights reserved by the publisher here</p></footer>"
      "</body></html>";
  auto r = extract_article(html);
  CHECK(r.status == TextStatus::FullExtraction);
  CHECK(contains(r.text, "alpha word0"));
  CHECK(!contains(r.text, "Home News"));
  CHECK(!contains(r.text, "Copyright"));
  CHECK(!contains(r.text, "Read more"));

  // No <article> tag: falls back to the container with the most paragraph text.
  std::string plain = "<html><body><div><p>" + long_paragraph("gamma") + "</p><p>" + long_paragraph("delta") +
                      "</p></div></body></html>";
  CHECK(extract_article(plain).status == TextStatus::FullExtraction);

  // JSON-LD articleBody.
  std::string body;
  for (int i = 0; i < 40; ++i) body += "sentence" + std::to_string(i) + " text ";
  std::string ld = "<html><head><script type=\"application/ld+json\">{\"@type\":\"NewsArticle\",\"articleBody\":\"" +
                   body + "\"}</script></head><body></body></html>";
  CHECK(extract_article(ld).status == TextStatus::ShortButValid);

  CHECK(extract_article("<html><body><p>Too short.</p></body></html>").status == TextStatus::Empty);
  std::string blocked = "<html><body><p>Please verify you are human to continue reading this page now ok</p></body></html>";
  CHECK(extract_article(blocked).status == TextStatus::Blocked);
}

static void test_clustering() {
  std::time_t now = 1760000000;
  std::vector<Article> arts;
  const std::string cricket1 =
      "India beat Sri Lanka by seven wickets in Colombo on Sunday, chasing 264 with Devdutt Padikkal scoring "
      "103 not out. The Indian batters finished the chase with 20 balls to spare in the one day international.";
  const std::string cricket2 =
      "Devdutt Padikkal hit an unbeaten 103 as India chased down Sri Lanka's 264 and won the Colombo "
      "one day international by seven wickets, finishing the chase with 20 balls remaining.";
  const std::string cricket3 =
      "A century from Padikkal, 103 not out, helped India beat Sri Lanka by seven wickets in Colombo, "
      "reaching the 264 target in the one day match with 20 balls left.";
  const std::string quake1 =
      "A magnitude 6.1 earthquake struck eastern Turkey on Saturday morning, damaging buildings in "
      "several villages and injuring dozens of residents according to the disaster agency.";
  const std::string quake2 =
      "Dozens were injured when a 6.1 magnitude earthquake hit eastern Turkey, damaging buildings in "
      "villages across the region, the disaster agency said on Saturday.";
  const std::string budget =
      "The finance ministry presented the annual budget with new tax slabs for salaried workers and "
      "higher spending on railways and highways over the coming fiscal year.";
  arts.push_back(make_article("national", "sports", "India beat Sri Lanka by 7 wickets", cricket1, "a.com", 0, now));
  arts.push_back(make_article("national", "sports", "Padikkal 103 not out as India win in Colombo", cricket2, "b.com", 1, now));
  arts.push_back(make_article("national", "sports", "Padikkal century seals India win over Sri Lanka", cricket3, "c.com", 2, now));
  arts.push_back(make_article("national", "tragedies", "Magnitude 6.1 quake hits eastern Turkey", quake1, "a.com", 0, now));
  arts.push_back(make_article("national", "tragedies", "Dozens injured in Turkey earthquake", quake2, "d.com", 1, now));
  arts.push_back(make_article("national", "business", "Budget brings new tax slabs", budget, "e.com", 0, now));
  arts.push_back(make_article("international", "sports", "Other scope article stays out", cricket1, "z.com", 0, now));

  // Google's own grouping wins even when the texts look different.
  std::vector<Article> grouped;
  grouped.push_back(make_article("national", "politics", "Alpha one", "red green blue", "a.com", 0, now));
  grouped.push_back(make_article("national", "politics", "Beta two", "cat dog bird", "b.com", 0, now));
  grouped.push_back(make_article("national", "politics", "Gamma three", "sun moon star", "c.com", 1, now));
  grouped[0].group = grouped[1].group = "g1";
  auto grouped_events = build_events(grouped, "national");
  CHECK(grouped_events.size() == 2);

  auto events = build_events(arts, "national");
  CHECK(events.size() == 3);
  std::vector<std::size_t> sizes;
  for (auto& e : events) sizes.push_back(e.articles.size());
  std::sort(sizes.begin(), sizes.end());
  CHECK((sizes == std::vector<std::size_t>{1, 2, 3}));

  // Grading: the event with three outlets beats the single-outlet event.
  for (auto& e : events) {
    e.category = assign_category(e, arts);
    score_event(e, arts, now + 3600);
  }
  Event* big = nullptr;
  Event* single = nullptr;
  for (auto& e : events) {
    if (e.articles.size() == 3) big = &e;
    if (e.articles.size() == 1) single = &e;
  }
  CHECK(big && single);
  if (big && single) {
    CHECK(big->n_publishers == 3);
    CHECK(big->category == "sports");
    CHECK(big->score > single->score);
  }

  // The same publisher twice counts once.
  Event dup;
  dup.articles = {0, 3};  // both from a.com
  score_event(dup, arts, now);
  CHECK(dup.n_publishers == 1);
}

static void test_summaries() {
  CHECK(numbers_supported("India won by 7 wickets chasing 1,264", "chasing 1264 and won by 7 wickets"));
  CHECK(!numbers_supported("India won by 8 wickets", "won by 7 wickets"));

  Summary s;
  s.summary = "India beat Sri Lanka by 7 wickets.";
  s.micro = "India beat Sri Lanka by 7 wickets.";
  s.extended = "India beat Sri Lanka by 7 wickets in Colombo.";
  CHECK(check_summary(s, "India beat Sri Lanka by 7 wickets in Colombo") == "");
  Summary meta = s;
  meta.summary = "This article says India won by 7 wickets.";
  CHECK(!check_summary(meta, "7 wickets").empty());
  Summary invented = s;
  invented.extended = "India beat Sri Lanka by 7 wickets in front of 50000 fans.";
  CHECK(check_summary(invented, "India beat Sri Lanka by 7 wickets in Colombo") == "number_not_in_sources");
  Summary too_long = s;
  too_long.micro = limit_words(std::string(200, 'x'), 1) + " a b c d e f g h i j k l m n o p q r s t u v w";
  CHECK(check_summary(too_long, "x") == "micro_too_long");

  std::vector<Article> arts;
  arts.push_back(make_article("national", "sports", "India beat Sri Lanka by 7 wickets - Paper",
                              "India beat Sri Lanka by seven wickets on Sunday. Devdutt Padikkal scored 103 not out. " +
                                  long_paragraph("filler"),
                              "a.com", 0, 1760000000));
  Event e;
  e.articles = {0};
  Summary x = summarize_extractive(e, arts);
  CHECK(word_count(x.micro) <= 20);
  CHECK(word_count(x.summary) <= 40);
  CHECK(word_count(x.extended) <= 150);
  CHECK(!x.ai);
}

static void test_render_and_repeats() {
  Edition ed;
  ed.date = "2026-11-07";
  PublishedEvent p;
  p.id = "national-sports-1";
  p.scope = "national";
  p.category = "sports";
  p.n_publishers = 3;
  p.timestamp = "2026-11-07T10:00:00Z";
  p.summary = "<script>alert(1)</script> India won";
  p.micro = "India won";
  p.extended = "Extended text & more";
  p.sources = {{"https://a.com/x", "a.com"}, {"javascript:alert(1)", "evil"}};
  ed.events.push_back(p);

  std::string html = render_edition_html(ed, "../");
  CHECK(!contains(html, "<script>alert"));  // the event text is escaped, not executed
  CHECK(contains(html, "<script src=\"../prefs.js\"></script>"));  // the only script is ours
  std::size_t scripts = 0;
  for (std::size_t at = html.find("<script"); at != std::string::npos; at = html.find("<script", at + 1)) ++scripts;
  CHECK(scripts == 1);
  CHECK(contains(html, "id=\"cat-international-sports\""));  // categories are chosen per tab
  CHECK(contains(html, "menu-national") && contains(html, "menu-international"));
  CHECK(contains(html, "&lt;script&gt;"));
  CHECK(contains(html, "https://a.com/x"));
  CHECK(!contains(html, "javascript:alert"));
  CHECK(contains(html, "Saturday, 7 November 2026"));
  CHECK(contains(html, "No major news."));  // categories with no events

  Edition back = edition_from_json(edition_to_json(ed));
  CHECK(back.events.size() == 1 && back.events[0].summary == p.summary && back.events[0].sources.size() == 2);

  // Repeat detection against an earlier edition.
  Edition old_ed;
  old_ed.date = "2026-11-06";
  PublishedEvent o = p;
  o.summary = "India beat Sri Lanka by seven wickets in Colombo with Padikkal scoring 103 not out";
  o.extended = "Devdutt Padikkal hit 103 not out as India chased 264 against Sri Lanka in Colombo.";
  o.sources = {{"https://old.com/1", "old.com"}};
  old_ed.events.push_back(o);

  PublishedEvent same = p;
  same.id = "n-1";
  same.summary = "Padikkal 103 not out guides India to seven wicket win over Sri Lanka in Colombo";
  same.extended = "India chased down 264 in Colombo, Devdutt Padikkal unbeaten on 103 against Sri Lanka.";
  same.sources = {{"https://new.com/1", "new.com"}};
  PublishedEvent different = p;
  different.id = "n-2";
  different.summary = "Parliament passes the new data protection amendment after a long debate";
  different.extended = "Lawmakers approved the amendment on privacy rules for companies handling personal data.";
  different.sources = {{"https://new.com/2", "new.com"}};
  PublishedEvent sameurl = different;
  sameurl.id = "n-3";
  sameurl.sources = {{"https://old.com/1", "old.com"}};

  std::vector<PublishedEvent> events = {same, different, sameurl};
  auto dropped = drop_repeats(events, {old_ed}, 0.5);
  CHECK(dropped.size() == 2);
  CHECK(events.size() == 1 && events[0].id == "n-2");
}

static void test_category_and_demotion() {
  std::time_t now = 1760000000;
  std::vector<Article> arts;
  // A stock-market story that a "crash" keyword feed filed under tragedies.
  arts.push_back(make_article("international", "tragedies", "Wall Street stock market crash hits Asia", "text", "a.com", 0, now));
  arts.push_back(make_article("international", "business", "Stock market slump: investors sell shares", "text", "b.com", 1, now));
  arts.push_back(make_article("international", "tragedies", "AI stock crash rattles markets", "text", "c.com", 2, now));
  Event market;
  market.articles = {0, 1, 2};
  CHECK(assign_category(market, arts) == "business");

  // A genuine tragedy stays a tragedy.
  std::vector<Article> fire;
  fire.push_back(make_article("national", "tragedies", "Fire kills six in Assam factory", "text", "a.com", 0, now));
  fire.push_back(make_article("national", "tragedies", "Six injured in factory blaze", "text", "b.com", 1, now));
  Event f;
  f.articles = {0, 1};
  CHECK(assign_category(f, fire) == "tragedies");

  // Local school sports score less than the same coverage of a national story.
  std::vector<Article> sports;
  sports.push_back(make_article("international", "sports", "Iowa high school football scores", "t", "a.com", 0, now));
  sports.push_back(make_article("international", "sports", "High school football playoff scores", "t", "b.com", 1, now));
  sports.push_back(make_article("international", "sports", "India win the cricket final", "t", "a.com", 0, now));
  sports.push_back(make_article("international", "sports", "India clinch cricket final", "t", "b.com", 1, now));
  Event local, major;
  local.articles = {0, 1};
  major.articles = {2, 3};
  score_event(local, sports, now);
  score_event(major, sports, now);
  CHECK(local.demoted && !major.demoted);
  CHECK(local.score < major.score);

  // World-feed stories carry no category vote: headline keywords decide, politics is the default.
  std::vector<Article> world;
  world.push_back(make_article("international", "world", "Norris fastest in Grand Prix practice", "t", "a.com", 0, now));
  world.push_back(make_article("international", "world", "Formula One: McLaren lead Grand Prix practice", "t", "b.com", 1, now));
  world.push_back(make_article("international", "world", "Quiet summit ends without statement", "t", "c.com", 2, now));
  world.push_back(make_article("international", "world", "Something entirely unclassifiable happens", "t", "d.com", 3, now));
  Event f1, summit, unknown;
  f1.articles = {0, 1};
  summit.articles = {2};
  unknown.articles = {3};
  CHECK(assign_category(f1, world) == "sports");
  CHECK(assign_category(summit, world) == "politics");
  CHECK(assign_category(unknown, world) == "politics");

  // Regression: a college athlete shot by police is a tragedy, not sports.
  std::vector<Article> shot;
  shot.push_back(make_article("international", "world", "College student fled knife attack, then was shot five times by cops", "t", "a.com", 0, now));
  shot.push_back(make_article("international", "world", "College lacrosse player sues Albany police after being shot during hostage standoff", "t", "b.com", 0, now));
  shot.push_back(make_article("international", "world", "Police Shot College Athlete After Mistaking Him for His Brother in Hostage Situation", "t", "c.com", 0, now));
  Event shot_event;
  shot_event.articles = {0, 1, 2};
  CHECK(assign_category(shot_event, shot) == "tragedies");

  // Rank counts relative to the feed's own size.
  std::vector<Article> ranked;
  ranked.push_back(make_article("international", "world", "Top story", "t", "a.com", 5, now));
  ranked.back().feed_size = 60;
  ranked.push_back(make_article("national", "politics", "Bottom story", "t", "a.com", 5, now));
  ranked.back().feed_size = 6;
  Event top, bottom;
  top.articles = {0};
  bottom.articles = {1};
  score_event(top, ranked, now);
  score_event(bottom, ranked, now);
  CHECK(top.rank_score > bottom.rank_score);
}

int main() {
  test_util();
  test_env_file();
  test_rss();
  test_gnews();
  test_extract();
  test_clustering();
  test_summaries();
  test_render_and_repeats();
  test_category_and_demotion();
  if (failures == 0) std::cout << "All tests passed\n";
  else std::cout << failures << " check(s) failed\n";
  return failures == 0 ? 0 : 1;
}
