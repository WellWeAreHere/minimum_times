// Grouping articles that are about the same real-world event.
//
// How it works, in plain words:
//  1. TF-IDF turns each text into a list of numbers. A word counts for more when it is
//     frequent in this text but rare across all the texts (so "Padikkal" matters, "government"
//     barely does).
//  2. Cosine similarity compares two such lists: 0 = nothing in common, 1 = identical.
//  3. Average-link clustering repeatedly merges the two most similar groups until no pair
//     of groups is at least CLUSTER_THRESHOLD similar.
#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mt {

// A text as (word id, weight) pairs, sorted by id and scaled to length 1.
struct SparseVec {
  std::vector<std::pair<int, float>> v;
};

class TfIdf {
 public:
  // Learns word rarity from a set of tokenized texts.
  void fit(const std::vector<std::vector<std::string>>& docs);
  // Turns one tokenized text into a vector (words unseen during fit are ignored).
  SparseVec transform(const std::vector<std::string>& tokens) const;
  std::size_t vocabulary_size() const { return ids_.size(); }

 private:
  std::unordered_map<std::string, int> ids_;
  std::vector<float> idf_;
};

double cosine(const SparseVec& a, const SparseVec& b);

// Average-link clustering over a precomputed similarity matrix (n x n, symmetric).
// Returns groups of indices. Every index appears in exactly one group.
std::vector<std::vector<int>> cluster_average_link(std::vector<std::vector<double>> sim,
                                                   double threshold);

}  // namespace mt
