// Writing the finished edition as a static website (plain HTML + CSS, no JavaScript).
//
// Layout of the output folder (default "docs/", which GitHub Pages can serve):
//   index.html               the latest edition
//   archive.html             list of all editions
//   editions/YYYY-MM-DD.html one page per day
//   data/YYYY-MM-DD.json     the same edition as data (also used to detect repeats later)
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "model.hpp"

namespace mt {

nlohmann::json edition_to_json(const Edition& e);
Edition edition_from_json(const nlohmann::json& j);

// `root` is the path from the page to the site root: "" for index.html, "../" for editions/.
std::string render_edition_html(const Edition& e, const std::string& root);
std::string render_archive_html(const std::vector<std::string>& dates_desc);

// Writes the edition's data + page, then refreshes index.html and archive.html.
void write_site(const std::string& out_dir, const Edition& e);

// Rebuilds every page (editions, index, archive, prefs.js) from the saved data files, without
// fetching anything. Use it after changing the page design. Returns how many editions it rebuilt.
int rerender_site(const std::string& out_dir);

// Dates that have a data file, newest first.
std::vector<std::string> list_edition_dates(const std::string& out_dir);

// Up to `count` editions published before `date`, newest first.
std::vector<Edition> load_recent_editions(const std::string& out_dir, const std::string& date,
                                          int count);

}  // namespace mt
