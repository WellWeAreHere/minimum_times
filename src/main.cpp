// Command-line entry point. See README.md for what each stage does.
#include <cstdlib>
#include <iostream>
#include <string>

#include "http.hpp"
#include "pipeline.hpp"
#include "render.hpp"
#include "util.hpp"

static void usage() {
  std::cout << "minimum_times [options]\n"
               "  --out DIR           where to write the site        (default: docs)\n"
               "  --log-dir DIR       where to write the run log     (default: data/logs)\n"
               "  --date YYYY-MM-DD   edition date                   (default: today, India time)\n"
               "  --no-ai             use extractive summaries only (also used if NVIDIA_API_KEY is unset)\n"
               "  --max-per-feed N    read only N items per feed (quick test runs)\n"
               "  --rerender          rebuild the pages in --out from saved data, fetch nothing\n"
               "  --help              show this text\n";
}

int main(int argc, char** argv) {
  mt::Options options;
  bool rerender_only = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << arg << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--out") options.out_dir = value();
    else if (arg == "--log-dir") options.log_dir = value();
    else if (arg == "--date") options.date = value();
    else if (arg == "--no-ai") options.use_ai = false;
    else if (arg == "--rerender") rerender_only = true;
    else if (arg == "--max-per-feed") options.max_per_feed = std::atoi(value().c_str());
    else if (arg == "--help" || arg == "-h") { usage(); return 0; }
    else {
      std::cerr << "Unknown option: " << arg << "\n";
      usage();
      return 2;
    }
  }
  if (options.max_per_feed < 1) options.max_per_feed = 1;

  if (rerender_only) {  // rebuild pages from saved data: no network, no key needed
    int n = mt::rerender_site(options.out_dir);
    std::cout << "Rebuilt " << n << " edition page(s) in " << options.out_dir << "/\n";
    return n > 0 ? 0 : 1;
  }

  // Local convenience: pick up NVIDIA_API_KEY etc. from .env.local or .env in the current folder.
  // (Real environment variables win. These files are git-ignored; never commit them.)
  mt::load_env_file(".env.local");
  mt::load_env_file(".env");

  mt::http_global_init();
  int code = mt::run_pipeline(options);
  mt::http_global_cleanup();
  return code;
}
