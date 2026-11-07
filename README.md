# Minimum Times

"The minimum news you need." A C++ program that reads Google News, groups articles into events,
grades them, summarizes the best ones and writes a static website (`docs/`).

## How it works

1. **Fetch** – India: 6 keyword feeds. International: Google's World feed plus the Sports feed of
   the US, UK, Australia and South Africa editions. Each Google item also lists the same story from
   other outlets; those become extra articles of one event.
2. **Extract** – resolve the real publisher URL and pull the article text. Blocked, empty or too
   short pages are skipped and logged.
3. **Group** – TF-IDF similarity merges articles about the same event.
4. **Grade** (no AI) – outlets covering it (60%), Google rank (20%), freshness (20%). Local stories
   are demoted and categories are fixed with keyword lists. An event needs 2+ outlets and a score
   of 0.35; each category keeps its best 4.
5. **Summarize** (the only AI step) – micro (20 words), normal (40) and extended (150) summaries
   from NVIDIA's API. Rule checks reject over-long text, "this article…" phrasing and any number
   not in the sources. On failure, or without a key, real sentences are copied from the articles.
6. **Drop repeats** of the last two editions, then write the site and a log of every candidate.

## Build and run

Needs a C++20 compiler, CMake, Ninja, libcurl, nlohmann-json, pugixml, gumbo-parser.

```bash
# Windows (MSYS2 UCRT64)
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,curl,nlohmann-json,pugixml,gumbo-parser,pkgconf}
# Debian/Ubuntu
sudo apt install cmake ninja-build pkg-config libcurl4-openssl-dev nlohmann-json3-dev libpugixml-dev libgumbo-dev

cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
./build/minimum_times            # full edition, writes docs/ (about 20 minutes with AI)
```

Options: `--no-ai`, `--out DIR`, `--log-dir DIR`, `--date YYYY-MM-DD`, `--max-per-feed N`,
`--rerender` (rebuild pages from saved data, no network).

**Key and model:** put `NVIDIA_API_KEY=...` (and optionally `NVIDIA_MODEL=...`) in `.env.local`
in the folder you run from (git-ignored; see `.env.example`). Default model:
`mistralai/mistral-large-4-1t-a52b`. It is a reasoning model, so requests are slow; set
`NVIDIA_REASONING_EFFORT=none` for models that allow it.

## Deploy

GitHub Actions (`.github/workflows/publish-edition.yml`) runs twice a day, builds, tests,
generates an edition and commits `docs/` and `data/logs/`. Vercel only hosts `docs/` (see
`vercel.json`) and redeploys on every push.

1. Push to GitHub, add the repository secret `NVIDIA_API_KEY`.
2. Import the repo on Vercel and deploy with the default settings.
3. Run the workflow once from the Actions tab (Run workflow).

## Site

Static HTML/CSS. The only script, `docs/prefs.js`, saves the reader's theme, extreme mode, tab
and categories per tab in the browser's `localStorage`; nothing is sent anywhere.

## Tuning

All numbers, feeds, category keywords and demote phrases live in `src/config.hpp`. Each run's
`data/logs/DATE.jsonl` records every candidate event, its score and why it was dropped.

## Limits

- Importance is coverage-based, and the keyword rules are blunt.
- Fallback summaries read like headlines.
- Google link decoding and publisher sites can break or block us; failures are logged.
- International sports / science are thin when the feeds have little, and Germany and China have
  no English Google News edition.
