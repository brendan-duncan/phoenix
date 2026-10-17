# Site analytics (self-hosted collector)

Cookieless page views, guide reading stats and download counts for
phoenixanimate.com. A small Node service on the web host stores them in SQLite
and serves a password-protected dashboard. It has no dependencies, uses Node's
built-in `node:sqlite`, and runs in about 60–100 MB of RAM. No cookie banner is
needed and nothing goes to a third party. The public statement of what is
collected is [docs/privacy.html](../docs/privacy.html); keep it in step with
this file.

| Path | Role |
| --- | --- |
| `phoenix_analytics.js` | Client script. Batches events and sends them with `sendBeacon` to `/a/collect`. |
| `../docs/assets/site.js` | Loads the client (`assets/analytics.js`) after it builds the page, on phoenixanimate.com only. |
| `../tools/deploy_site.py` | Publishes the client as `assets/analytics.js` with the site, stamped with the commit. |
| `collector/server.mjs` | The collector: ingest, safeguards, dashboard API, maintenance, CLI. |
| `collector/lib/` | Storage (`store.mjs`), dashboard queries, validation and rate limits (`ingest.mjs`), UA parsing, geo lookup, auth. |
| `collector/dashboard/` | The dashboard page (plain JS + inline SVG, jsPDF vendored for PDF export). |
| `collector/phoenix-analytics.service` | systemd unit with memory and CPU limits. |
| `collector/apache.conf` | Apache lines that proxy `/a/` to the collector. |
| `../tools/deploy_analytics.py` | Uploads the collector to the server and restarts it. |
| `tests/` | Unit tests, a client test in a fake page, and an end-to-end test that runs the real server. |

The client only sends from `phoenixanimate.com` / `www.phoenixanimate.com`.
`site.js` doesn't even load it anywhere else, so the guide opened from disk
(`file://`), from a local server or from a mirror sends nothing. Visitors with
Do Not Track or Global Privacy Control are skipped, and so are automated
browsers (`navigator.webdriver`). `?notrack` opts a browser out (remembered in
`localStorage` as `phoenix.analytics.off`) and `?track` opts it back in.

## Privacy model

- There's no cookie and no persistent identifier. A visit ("session") is a
  random id in `sessionStorage` (`phoenix.sid`), so it ends when the tab closes.
- A **visitor** is `sha256(daily salt | IP | user agent | host)`, cut to 16 hex
  characters. The salt is random, rotates at UTC midnight and the old one is
  discarded, so visitors can't be linked across days. As a result, "visitors"
  over a multi-day range is the sum of each day's unique visitors.
- IPs are never stored. They're used in memory for the location lookup, the
  hash and rate limiting, then dropped.
- Location (country, region/state, city) comes from DB-IP's free "IP to City
  Lite" database (CC BY 4.0), looked up in memory at ingest. City-level lite
  data is approximate (often the ISP's nearest hub). The dashboard footer and
  the privacy page carry the attribution.
- The search box's query text is never sent, only its length, word count,
  result count and whether a result was chosen. Mail links record only that a
  mail link was clicked.

Apache's own access logs (`/var/log/apache2/phoenixanimate-access.log`) still
hold IPs, as for any site; the privacy page says so.

## One-time server setup

The collector runs on the same Lightsail host as the site, next to (and
separate from) the TaosEngine collector: its own folder, port **8791**,
database and service. Requires **Node 22.13 or newer** on the server (for
`node:sqlite`); check with `node -v`. Below, `KEY` is the `.pem` path and `IP`
the static IP, both as in `tools/deploy_site.ini`.

1. **Upload the collector (from your PC):**
   ```powershell
   python tools/deploy_analytics.py
   ```
   This creates `/home/admin/phoenix-analytics/` with `data/` inside. It
   reports that the service isn't installed yet; that's expected the first time.
2. **Get the location database** (on the server, `ssh -i KEY admin@IP`; city
   level: about 85 MB download, 240 MB on disk, ~30 s to build, ~115 MB RAM
   while building):
   ```sh
   cd ~/phoenix-analytics && node --disable-warning=ExperimentalWarning --max-old-space-size=64 server.mjs update-geo
   ```
   `update-geo country` builds the small country-only database (~24 MB)
   instead. To save the download, copying the TaosEngine collector's file works
   too: `cp ~/taos-analytics/data/geo.db ~/phoenix-analytics/data/`.
3. **Set the dashboard password** (user `admin`; prompts twice, at least 10 characters):
   ```sh
   cd ~/phoenix-analytics && node --disable-warning=ExperimentalWarning server.mjs set-password
   ```
4. **Install the service:**
   ```sh
   sudo cp ~/phoenix-analytics/phoenix-analytics.service /etc/systemd/system/
   sudo systemctl daemon-reload && sudo systemctl enable --now phoenix-analytics
   curl -s http://127.0.0.1:8791/health        # → {"ok":true}
   ```
5. **Update Apache.** Back up the :443 vhost, then paste the contents of
   `collector/apache.conf` inside `<VirtualHost *:443>`, after `DocumentRoot`:
   ```sh
   sudo cp /etc/apache2/sites-available/phoenixanimate-le-ssl.conf ~/phoenixanimate-le-ssl.conf.bak
   sudo a2enmod proxy proxy_http           # already on for TaosEngine; harmless
   sudo nano /etc/apache2/sites-available/phoenixanimate-le-ssl.conf
   sudo apache2ctl configtest && sudo systemctl reload apache2
   curl -s -o /dev/null -w "%{http_code}\n" -X POST https://phoenixanimate.com/a/collect   # → 204
   curl -s -o /dev/null -w "%{http_code}\n" https://phoenixanimate.com/a/dashboard/        # → 401
   curl -sI https://phoenixanimate.com/download/Phoenix-win-x64-Setup.exe | grep -i location   # still → GitHub
   ```
   The lines to paste:
   ```apache
   <Location "/a/">
       ProxyPass        "http://127.0.0.1:8791/" timeout=10
       ProxyPassReverse "http://127.0.0.1:8791/"
       LimitRequestBody 65536
   </Location>
   ```
   They don't conflict with the `/updates/` and `/download/` RewriteRules: those
   rules are anchored to their own prefixes, so a request for `/a/...` never
   matches them and goes on to the proxy, and `/updates/` and `/download/` never
   reach the proxy. Leave the rules as they are. Only a future catch-all
   RewriteRule would need `RewriteRule ^/a/ - [L]` in front of it. Don't put a
   folder named `a` in `docs/`; the proxy would hide it.
6. **Deploy the site** so pages load the client:
   ```powershell
   python tools/deploy_site.py
   ```
7. **Open the dashboard** at <https://phoenixanimate.com/a/dashboard/>, open a
   guide page in another browser tab, and watch it under **Realtime**.

A monthly cron job (`crontab -e` as `admin`) keeps the location database current:
`0 4 2 * * cd /home/admin/phoenix-analytics && /usr/bin/node --disable-warning=ExperimentalWarning --max-old-space-size=64 server.mjs update-geo`.
(If the TaosEngine collector's cron runs at 04:00 on the 2nd too, put this one at
04:30.) The running collector notices the new file within an hour.

**Updating the collector:** `python tools/deploy_analytics.py` (uploads and
restarts; `--no-restart` uploads only, `--dry-run` lists the files).
**Updating the client:** `python tools/deploy_site.py`.
**Logs:** `journalctl -u phoenix-analytics -n 100`.
**Backups:** the whole state is `data/analytics.db`. Copy it with
`sqlite3 data/analytics.db ".backup backup.db"`, or stop the service and copy the file.
The database schema migrates itself when the service starts.

## Safeguards

Everything is bounded, so a flood costs a little CPU but can't exhaust memory or
disk, or take down Apache or the other services on the host:

| Layer | Limit | Adjustable in Admin |
| --- | --- | --- |
| Apache | `LimitRequestBody 64 KB` on `/a/` | no |
| Global request rate | 200 requests/s (burst 400); excess gets `429` without the body being read | yes |
| Request size | 16 KB, at most 30 events per request, 16 properties per event | no |
| Bots | UA patterns (crawlers, headless, curl, …) are dropped and counted | no |
| Origin | the payload host and `Origin` header must be phoenixanimate.com or www.phoenixanimate.com | no |
| Per-IP rate | 120 events/min; an IP that keeps hitting the limit is ignored for 15 min | yes |
| Global event rate | 3000 events/min across all IPs, which bounds a rotating-IP flood | yes |
| Daily cap | 200,000 stored events per UTC day | yes |
| Database size | stop storing past 1024 MB | yes |
| Write queue | at most 5000 pending events, written once a second in one transaction | no |
| Connections | 256 max; 5 s header and 10 s request timeouts | no |
| Dashboard login | 10 failures per IP per 15 min, then locked out | no |
| systemd | `MemoryHigh=128M`, `MemoryMax=160M`, `CPUQuota=50%`, `Nice=10`, auto-restart | edit the unit |

Every dropped event is counted by reason under **Admin → Ingestion health**,
and **Pause ingestion** drops everything at once.

## Retention and cleanup

Raw events are kept for **396 days (13 months)** by default. Change this under
**Admin → Settings** (and update the privacy page if you do). Once a UTC day,
the collector rolls each finished day up into daily totals (views, visitors,
visits, bounces, duration, plus pageview breakdowns by page, referrer, country,
browser, OS, device, screen, language and section, and per-event counts), then
deletes raw events older than the retention window. Daily totals are kept
forever, so the Overview's long-range charts, tables and download count survive
retention. Filters, the Reading and Downloads tabs and custom-event details only
cover the raw window.

**Admin → Cleanup** can delete raw events older than a date, by event name, by
path prefix, or within a date range (optionally with that range's daily totals).
It can also run maintenance immediately, compact the file (`VACUUM`) and export
the current range as CSV. Deletes run in chunks so collection keeps going, and
freed space is reused automatically.

## Dashboard

- **Overview:** views, visitors, visits, bounce rate, visit duration and app
  downloads against the previous period; views and visitors over time; pages
  (plus entry, exit and with query), referrers, locations (countries / regions /
  cities), browsers and their versions, OS and OS versions, devices, screens /
  languages and guide sections. Click a row to filter everything by it.
- **Realtime:** the last 30 minutes, refreshing every 10 s: views per minute,
  active pages, countries and a live event feed.
- **Events:** every custom event with counts. Pick one to see it over time and
  how each property's values are distributed; numeric properties also show
  median / p95 / min / max.
- **Reading:** per guide page, scroll depth (50% / end), completion, average
  depth, time on page and code copies; the headings reached on a page, as a
  funnel (pick the page, or filter by it); the search box (uses, how often a
  result is chosen or nothing is found, average position chosen); tutorials
  opened and where from; theme toggles.
- **Downloads:** app downloads over time next to lesson files; by file, by the
  platform the file is for, by kind (installer / portable / AppImage); the
  visitor's OS against the file's platform; the pages the links were clicked on;
  lesson files; outbound links by target and URL.
- **Admin:** storage, settings, ingestion health, cleanup and changing the password.

State lives in the URL hash, so any view can be bookmarked. The ⤓ button
downloads the current view (every tab except Admin) as a PDF, a standalone HTML
page or Markdown. The PDF is real text drawn with jsPDF 4.2.1 and
jsPDF-AutoTable 5.0.8 (MIT), vendored as `dashboard/jspdf.umd.min.js` and
`dashboard/jspdf.plugin.autotable.min.js` and loaded only on the first PDF
download. Their built-in fonts cover Windows-1252 only, so flags and arrows are
dropped or spelled out in the PDF.

## What gets recorded

Every beacon carries the visit id, screen size, language, touch-point count and,
on Chromium browsers, User-Agent Client Hints (brand, full version and platform
version). The collector adds the page path (with `index.html` / `.html`
stripped), query string, guide section, external referrer (landing page only),
country / region / city, browser and major version, OS and version, and device
class.

Sections (`kind`): `home` (`/`), `install` (`/get-started`), `tutorials` (the
Interactive tutorials index), `tutorial` (`/first-animation` and
`/tutorial-*`), `about` (`/privacy`) and `guide` (everything else). The client's
`pageKind()` and the collector's `classifyPath()` must agree.

| Event | Where | Data |
| --- | --- | --- |
| `pageview` | all pages | — |
| `vitals` | all pages | `lcp`, `fcp`, `cls`, `inp`, `ttfb`; sent once when the page is first hidden |
| `time-on-page` | all pages | visible `seconds` and `bucket`; `depth` (furthest % of the article seen, in steps of 5; 100 for a page that fits on one screen), `sections` reached of `of` headings |
| `read-depth` | all pages | `depth` 25 / 50 / 75 / 100 of the article text (from the top of `.content` to the pager), sent while scrolling; pages that hardly scroll send none |
| `read-complete` | all pages | reached 75% after at least 30 s of visible time; `seconds` |
| `section` | all pages | a heading (`h2`/`h3` with an id, outside cards) came into the top two thirds of the window: `id`, `n` (its position), `of`, `level`; once per heading, at most 40 per page |
| `search` | all pages | one per use of the search box: `len` (characters), `words`, `results` (shown; -1 if none rendered yet), `chosen`, `rank` (position of the chosen result). Ends on a chosen result, Enter, Escape, clearing the box, leaving it or leaving the page. Never the query text |
| `download` | all pages | an app download link: `file`, `os` (`windows` / `macos` / `linux`), `package` (`installer` / `portable` / `appimage`), `via` (`site` for `/download/…`, `github` for a release asset link), `where` (`page` / `header` / `footer` / `sidebar`) |
| `lesson-download` | tutorials | a tutorial's prepared FLA: `file`, `where` |
| `outbound` | all pages | a link off the site: `target` (`github-releases`, `github-issues`, `changelog`, `github`, `linkedin`, `mastodon`, `mail`, `other`), `host`, `url` (no query; none for mail), `where` |
| `tutorial-open` | tutorial pages | `tutorial` (page name), `from` (the previous guide page, `external` or `direct`) |
| `theme` | all pages | the theme button: `to` (`dark` / `light`) |
| `code-copy` | all pages | a copy made from inside `pre` / `code` (the text isn't sent) |
| `js-error` / `js-rejection` | all pages | `message`, `source`, `stack` (own frames only), `seq`, `build`, `thirdParty`; de-duplicated, at most 5 per page load |

Events are sent in batches of up to 20, every 5 s and whenever the page is
hidden or closed. Page code can send its own events with
`window.phoenixAnalytics?.track(name, data)`.

## Local development and tests

```sh
cd analytics/collector
ANALYTICS_DATA_DIR=./data node server.mjs set-password
ANALYTICS_DATA_DIR=./data ANALYTICS_PORT=8791 node --disable-warning=ExperimentalWarning server.mjs
# dashboard: http://127.0.0.1:8791/dashboard/
```

```sh
node --test "analytics/tests/*.test.mjs"     # from the repo root; Node 22.13+
```

The tests use only Node's built-in test runner. `client.test.mjs` runs the real
client in a small fake page (`fake_page.mjs`); `server.test.mjs` starts the
collector on a random port with a temporary data folder.

Collector environment variables: `ANALYTICS_BIND` (default `127.0.0.1`),
`ANALYTICS_PORT` (default `8791`), `ANALYTICS_DATA_DIR` (default
`collector/data`, ignored by git), `ANALYTICS_HOSTS` (default
`phoenixanimate.com,www.phoenixanimate.com`) and `ANALYTICS_TRUST_PROXY` (`0`
to ignore X-Forwarded-For).
