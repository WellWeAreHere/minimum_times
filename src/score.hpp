// Grading events: how important is this event? No AI involved.
#pragma once
#include <ctime>
#include <vector>

#include "model.hpp"

namespace mt {

// Fills in the event's features and `score` (0..1) from its articles.
//
//   coverage   how many DIFFERENT outlets reported it (log scale, saturating)  weight 0.6
//   rank       how high Google News ranked its articles                        weight 0.2
//   freshness  how recently the newest article was published                   weight 0.2
void score_event(Event& e, const std::vector<Article>& all, std::time_t now);

// The category an event belongs to: feed votes plus category words found in its headlines.
std::string assign_category(const Event& e, const std::vector<Article>& all);

}  // namespace mt
