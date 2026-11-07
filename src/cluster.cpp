#include "cluster.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace mt {

void TfIdf::fit(const std::vector<std::vector<std::string>>& docs) {
  ids_.clear();
  idf_.clear();
  std::vector<int> df;
  for (const auto& doc : docs) {
    std::vector<int> seen;
    for (const auto& word : doc) {
      auto it = ids_.find(word);
      if (it == ids_.end()) {
        it = ids_.emplace(word, static_cast<int>(ids_.size())).first;
        df.push_back(0);
      }
      seen.push_back(it->second);
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    for (int id : seen) ++df[id];  // in how many texts does this word appear?
  }
  const double n = static_cast<double>(docs.size());
  idf_.resize(df.size());
  for (std::size_t i = 0; i < df.size(); ++i)
    idf_[i] = static_cast<float>(std::log((n + 1.0) / (df[i] + 1.0)) + 1.0);
}

SparseVec TfIdf::transform(const std::vector<std::string>& tokens) const {
  std::map<int, int> counts;  // ordered by id
  for (const auto& word : tokens) {
    auto it = ids_.find(word);
    if (it != ids_.end()) ++counts[it->second];
  }
  SparseVec out;
  double norm = 0;
  for (const auto& [id, count] : counts) {
    float tf = 1.0f + std::log(static_cast<float>(count));  // damp repeated words
    float w = tf * idf_[id];
    out.v.emplace_back(id, w);
    norm += static_cast<double>(w) * w;
  }
  norm = std::sqrt(norm);
  if (norm > 0)
    for (auto& p : out.v) p.second = static_cast<float>(p.second / norm);
  return out;
}

double cosine(const SparseVec& a, const SparseVec& b) {
  double dot = 0;
  std::size_t i = 0, j = 0;
  while (i < a.v.size() && j < b.v.size()) {
    if (a.v[i].first == b.v[j].first) {
      dot += static_cast<double>(a.v[i].second) * b.v[j].second;
      ++i;
      ++j;
    } else if (a.v[i].first < b.v[j].first) {
      ++i;
    } else {
      ++j;
    }
  }
  return dot;  // both vectors have length 1, so the dot product is the cosine
}

std::vector<std::vector<int>> cluster_average_link(std::vector<std::vector<double>> sim,
                                                   double threshold) {
  const int n = static_cast<int>(sim.size());
  std::vector<std::vector<int>> groups(n);
  std::vector<bool> alive(n, true);
  for (int i = 0; i < n; ++i) groups[i] = {i};

  for (;;) {
    int bi = -1, bj = -1;
    double best = threshold;
    for (int i = 0; i < n; ++i) {
      if (!alive[i]) continue;
      for (int j = i + 1; j < n; ++j) {
        if (alive[j] && sim[i][j] >= best) {
          best = sim[i][j];
          bi = i;
          bj = j;
        }
      }
    }
    if (bi < 0) break;

    // Merge group bj into bi; the new similarity to every other group is the size-weighted average.
    const double si = static_cast<double>(groups[bi].size());
    const double sj = static_cast<double>(groups[bj].size());
    for (int k = 0; k < n; ++k) {
      if (!alive[k] || k == bi || k == bj) continue;
      double merged = (si * sim[bi][k] + sj * sim[bj][k]) / (si + sj);
      sim[bi][k] = sim[k][bi] = merged;
    }
    groups[bi].insert(groups[bi].end(), groups[bj].begin(), groups[bj].end());
    groups[bj].clear();
    alive[bj] = false;
  }

  std::vector<std::vector<int>> out;
  for (int i = 0; i < n; ++i) {
    if (!alive[i]) continue;
    std::sort(groups[i].begin(), groups[i].end());
    out.push_back(std::move(groups[i]));
  }
  return out;
}

}  // namespace mt
