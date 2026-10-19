// Site analytics for phoenixanimate.com, sent to the self-hosted collector in
// analytics/collector/ (see analytics/README.md).
//
// tools/deploy_site.py publishes this file as assets/analytics.js, and
// website/assets/site.js loads it once the page is built, only when the page is
// served from phoenixanimate.com. Opened from disk or a local server, the guide
// never loads it, and this script checks the host again before sending
// anything.
//
// Events are queued and sent in small batches with navigator.sendBeacon to
// CONFIG.endpoint (same origin; Apache proxies /a/ to the collector). Visitors
// opt out with `?notrack` (remembered in localStorage) and back in with
// `?track`. Do Not Track, Global Privacy Control and automated browsers are
// skipped. There are no cookies; a visit is a random id in sessionStorage.
//
// Page code can report its own events through `window.phoenixAnalytics.track()`.
(function () {
  'use strict';

  var CONFIG = {
    endpoint: '/a/collect',
    hosts: ['phoenixanimate.com', 'www.phoenixanimate.com'],
    respectDoNotTrack: true,
    // Per page load cap on error reports.
    maxErrorEvents: 5,
    // Short commit hash of the deployed site, stamped in by
    // tools/deploy_site.py when it publishes this file. Sent with errors so a
    // fix can be checked against the builds still reporting.
    build: '',
    // Batch window and size. The collector accepts at most 30 events and
    // 16 KB per request.
    flushMs: 5000,
    maxBatch: 20,
    // At most this many headings per page are reported as reached.
    maxSections: 40,
  };
  var OPT_OUT_KEY = 'phoenix.analytics.off';
  var SID_KEY = 'phoenix.sid';

  var queue = [];
  var enabled = false;
  var sid = '';
  var flushTimer = 0;
  var hints = null;

  function clip(value, max) {
    var s = String(value);
    return s.length > max ? s.slice(0, max) : s;
  }

  function randomId() {
    var bytes = new Uint8Array(12);
    crypto.getRandomValues(bytes);
    var out = '';
    for (var i = 0; i < bytes.length; i++) {
      out += (bytes[i] % 36).toString(36);
    }
    return out;
  }

  // ── Page and link classification (pure; exposed for tests) ───────────────

  /** Section of the guide a path belongs to (mirrors collector/lib/ingest.mjs). */
  function pageKind(path) {
    var p = path.replace(/\/index\.html$/, '/').replace(/\.html$/, '');
    if (p === '/' || p === '') {
      return 'home';
    }
    if (p === '/get-started') {
      return 'install';
    }
    if (p === '/tutorials') {
      return 'tutorials';
    }
    if (p === '/first-animation' || /^\/tutorial-/.test(p)) {
      return 'tutorial';
    }
    if (p === '/privacy') {
      return 'about';
    }
    return 'guide';
  }

  /** What a downloadable file is: { os, package } from its name. */
  function downloadInfo(file) {
    var f = file.toLowerCase();
    var os = /win/.test(f) ? 'windows' : /osx|mac/.test(f) ? 'macos' : /linux|appimage/.test(f) ? 'linux' : '';
    var pkg = /\.appimage$/.test(f) ? 'appimage'
        : /portable|\.zip$/.test(f) ? 'portable'
          : /setup|\.exe$|\.pkg$|\.dmg$|\.msi$/.test(f) ? 'installer' : 'other';
    return { os: os, package: pkg };
  }

  /** Where an outbound link goes, as a short stable name for the dashboard. */
  function outboundTarget(url) {
    var host = url.hostname.toLowerCase().replace(/^www\./, '');
    var p = url.pathname;
    if (url.protocol === 'mailto:') {
      return 'mail';
    }
    if (host === 'github.com') {
      if (/\/releases(\/|$)/.test(p)) {
        return 'github-releases';
      }
      if (/\/issues(\/|$)/.test(p)) {
        return 'github-issues';
      }
      if (/CHANGELOG\.md$/i.test(p)) {
        return 'changelog';
      }
      return 'github';
    }
    if (host === 'linkedin.com' || /\.linkedin\.com$/.test(host)) {
      return 'linkedin';
    }
    if (/mastodon|gamedev\.place/.test(host)) {
      return 'mastodon';
    }
    return 'other';
  }

  /**
   * The event a click on a link to `href` should record, or null:
   * downloads (installers on /download/, release assets on GitHub, lesson
   * FLA files) and outbound links (other hosts, mail).
   * @param {string} href resolved link URL
   * @param {string} pageHost location.hostname of the page
   * @returns {{ name: string, data: object } | null}
   */
  function classifyLink(href, pageHost) {
    var url;
    try {
      url = new URL(href);
    } catch (err) {
      return null;
    }
    var ours = CONFIG.hosts.indexOf(url.hostname) >= 0 || url.hostname === pageHost;
    if (url.protocol === 'mailto:') {
      return { name: 'outbound', data: { target: 'mail', host: 'mail' } };
    }
    if (url.protocol !== 'http:' && url.protocol !== 'https:') {
      return null;
    }
    var file = url.pathname.split('/').pop() || '';
    var info;
    if (ours && /^\/download\/[^/]+$/.test(url.pathname)) {
      info = downloadInfo(file);
      return { name: 'download', data: { file: clip(file, 100), os: info.os, package: info.package, via: 'site' } };
    }
    if (ours && /\.fla$/i.test(url.pathname)) {
      // A tutorial's prepared artwork.
      return { name: 'lesson-download', data: { file: clip(file, 100) } };
    }
    if (ours) {
      return null;
    }
    if (url.hostname === 'github.com' && /\/releases\/(latest\/)?download\//.test(url.pathname)) {
      info = downloadInfo(file);
      return { name: 'download', data: { file: clip(file, 100), os: info.os, package: info.package, via: 'github' } };
    }
    return {
      name: 'outbound',
      data: { target: outboundTarget(url), host: clip(url.hostname.toLowerCase(), 120), url: clip(url.origin + url.pathname, 200) },
    };
  }

  // Exposed before the gate so page code can always call track(), which is
  // a no-op when analytics is off.
  window.phoenixAnalytics = {
    track: function (name, data) {
      push(name, data);
    },
    pageKind: pageKind,
    classifyLink: classifyLink,
    downloadInfo: downloadInfo,
  };

  // ── Queue and transport ──────────────────────────────────────────────────

  function flush() {
    clearTimeout(flushTimer);
    flushTimer = 0;
    while (queue.length > 0) {
      var now = Date.now();
      var batch = queue.splice(0, CONFIG.maxBatch);
      var events = [];
      for (var i = 0; i < batch.length; i++) {
        var e = batch[i];
        events.push({ n: e.n, u: e.u, a: now - e.t, d: e.d, r: e.r });
      }
      var body = JSON.stringify({
        v: 1,
        h: location.hostname,
        sid: sid,
        sc: screen.width + 'x' + screen.height,
        lg: navigator.language || '',
        tp: navigator.maxTouchPoints || 0,
        ch: hints || undefined,
        e: events,
      });
      var sent = false;
      try {
        sent = !!(navigator.sendBeacon && navigator.sendBeacon(CONFIG.endpoint, new Blob([body], { type: 'text/plain' })));
      } catch (err) {
        sent = false;
      }
      if (!sent) {
        try {
          fetch(CONFIG.endpoint, {
            method: 'POST',
            body: body,
            keepalive: true,
            credentials: 'omit',
            headers: { 'Content-Type': 'text/plain' },
          }).catch(function () {});
        } catch (err) {
          // Nothing else to try.
        }
      }
    }
  }

  function push(name, data, referrer) {
    if (!enabled) {
      return;
    }
    var clean;
    if (data) {
      clean = {};
      for (var k in data) {
        var v = data[k];
        // The collector derives page + section from the event URL.
        if (v === undefined || v === null || k === 'kind' || k === 'page') {
          continue;
        }
        clean[k] = typeof v === 'string' ? clip(v, 200) : v;
      }
    }
    queue.push({ n: clip(name, 50), u: clip(location.pathname + location.search, 400), t: Date.now(), d: clean, r: referrer });
    if (queue.length >= CONFIG.maxBatch) {
      flush();
    } else if (!flushTimer) {
      flushTimer = setTimeout(flush, CONFIG.flushMs);
    }
  }

  function track(name, data) {
    push(name, data);
  }

  // ── Opt-out / enable gate ────────────────────────────────────────────────
  if (CONFIG.hosts.indexOf(location.hostname) < 0 || navigator.webdriver) {
    return;
  }
  var params = new URLSearchParams(location.search);
  try {
    if (params.has('notrack')) {
      localStorage.setItem(OPT_OUT_KEY, '1');
    } else if (params.has('track')) {
      localStorage.removeItem(OPT_OUT_KEY);
    }
    if (localStorage.getItem(OPT_OUT_KEY)) {
      return;
    }
  } catch (e) {
    // Storage blocked: fall through and track normally.
  }
  if (CONFIG.respectDoNotTrack && (navigator.doNotTrack === '1' || window.doNotTrack === '1' || navigator.globalPrivacyControl === true)) {
    return;
  }
  try {
    sid = sessionStorage.getItem(SID_KEY) || '';
    if (!sid) {
      sid = randomId();
      sessionStorage.setItem(SID_KEY, sid);
    }
  } catch (e) {
    sid = sid || randomId();
  }
  enabled = true;

  // User-Agent Client Hints (Chromium only): the real OS version (UA strings
  // report Windows 10 on Windows 11 and macOS 10.15 on every Mac) and brand.
  try {
    if (navigator.userAgentData && navigator.userAgentData.getHighEntropyValues) {
      navigator.userAgentData.getHighEntropyValues(['platformVersion', 'fullVersionList']).then(function (h) {
        var brand = null;
        var list = h.fullVersionList || [];
        for (var i = 0; i < list.length; i++) {
          if (!/Not.?A.?Brand|Chromium/i.test(list[i].brand)) {
            brand = list[i];
            break;
          }
        }
        hints = { b: brand ? clip(brand.brand, 60) : '', bv: brand ? clip(brand.version, 30) : '', pv: clip(h.platformVersion || '', 30) };
      }, function () {});
    }
  } catch (e) {
    hints = null;
  }

  var path = location.pathname;
  var kind = pageKind(path);
  var page = path.replace(/\/index\.html$/, '/').replace(/\.html$/, '');

  push('pageview', null, document.referrer);

  // A tutorial page opened, and where from: another page of the guide, or
  // outside it.
  if (kind === 'tutorial') {
    var from = 'direct';
    try {
      if (document.referrer) {
        var ref = new URL(document.referrer);
        from = ref.host === location.host
          ? ref.pathname.replace(/\/index\.html$/, '/').replace(/\.html$/, '') || '/'
          : 'external';
      }
    } catch (e) {
      from = 'direct';
    }
    track('tutorial-open', { tutorial: page.replace(/^\//, ''), from: from });
  }

  // ── Web vitals (reported once, when the page is first hidden) ────────────
  var vitals = {};
  function observe(type, cb, extra) {
    try {
      var opts = { type: type, buffered: true };
      for (var k in extra) {
        opts[k] = extra[k];
      }
      new PerformanceObserver(function (list) {
        cb(list.getEntries());
      }).observe(opts);
    } catch (e) {
      // Entry type not supported in this browser.
    }
  }
  observe('largest-contentful-paint', function (entries) {
    var last = entries[entries.length - 1];
    if (last) {
      vitals.lcp = Math.round(last.startTime);
    }
  });
  observe('paint', function (entries) {
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].name === 'first-contentful-paint') {
        vitals.fcp = Math.round(entries[i].startTime);
      }
    }
  });
  var clsTotal = 0;
  observe('layout-shift', function (entries) {
    for (var i = 0; i < entries.length; i++) {
      if (!entries[i].hadRecentInput) {
        clsTotal += entries[i].value;
      }
    }
    vitals.cls = Math.round(clsTotal * 1000) / 1000;
  });
  var inpMax = 0;
  observe('event', function (entries) {
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].interactionId && entries[i].duration > inpMax) {
        inpMax = entries[i].duration;
      }
    }
    vitals.inp = Math.round(inpMax);
  }, { durationThreshold: 40 });
  var vitalsReported = false;
  function reportVitals() {
    if (vitalsReported) {
      return;
    }
    vitalsReported = true;
    var nav = performance.getEntriesByType && performance.getEntriesByType('navigation')[0];
    if (nav && nav.responseStart > 0) {
      vitals.ttfb = Math.round(nav.responseStart);
    }
    for (var k in vitals) {
      if (Object.prototype.hasOwnProperty.call(vitals, k)) {
        push('vitals', vitals);
        return;
      }
    }
  }
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden') {
      reportVitals();
    }
  });
  window.addEventListener('pagehide', reportVitals);

  // ── Visible time ─────────────────────────────────────────────────────────
  var visibleMs = 0;
  var visibleSince = document.visibilityState === 'visible' ? performance.now() : -1;
  function visibleSeconds() {
    var ms = visibleMs + (visibleSince >= 0 ? performance.now() - visibleSince : 0);
    return Math.round(ms / 1000);
  }
  function timeBucket(s) {
    if (s < 10) {
      return '<10s';
    }
    if (s < 30) {
      return '10-30s';
    }
    if (s < 60) {
      return '30-60s';
    }
    if (s < 180) {
      return '1-3m';
    }
    if (s < 600) {
      return '3-10m';
    }
    if (s < 1800) {
      return '10-30m';
    }
    return '30m+';
  }

  // Reading state, filled in by the scroll and heading trackers below.
  var maxDepth = 0;
  var sectionsReached = 0;
  var sectionsTotal = 0;

  var timeReported = false;
  function reportTime() {
    if (timeReported) {
      return;
    }
    timeReported = true;
    var s = visibleSeconds();
    var data = { seconds: s, bucket: timeBucket(s) };
    if (content) {
      // How far down the article the reader got (percent), and how many of
      // its headings they reached.
      data.depth = maxDepth;
      data.sections = sectionsReached;
      data.of = sectionsTotal;
    }
    track('time-on-page', data);
  }
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'visible') {
      visibleSince = performance.now();
    } else {
      if (visibleSince >= 0) {
        visibleMs += performance.now() - visibleSince;
      }
      visibleSince = -1;
      reportTime();
    }
  });
  window.addEventListener('pagehide', reportTime);

  // ── JS errors ────────────────────────────────────────────────────────────
  var errorCount = 0;
  var seenErrors = {};
  // Globals that browser extensions and in-app browsers inject into the page.
  // Errors that mention them are not ours even when the browser attributes
  // them to the page.
  var THIRD_PARTY_RE = /__firefox__|__gCrWeb|ethereum|DarkReader|webkit\.messageHandlers|_AutofillCallbackHandler/;
  var FRAME_RE = /((?:https?|blob|[a-z-]+-extension):\/\/[^\s()@]+?):(\d+):(\d+)/g;

  // Up to 4 stack frames as "file:line:col", newest first, joined by " < ".
  // Frames from other origins are kept as "~" so the shape of the stack
  // survives without leaking third-party URLs.
  function stackFrames(stack) {
    var frames = [];
    var own = false;
    var any = false;
    if (typeof stack === 'string') {
      FRAME_RE.lastIndex = 0;
      var m;
      while (frames.length < 4 && (m = FRAME_RE.exec(stack))) {
        any = true;
        var url = m[1];
        if (url.indexOf(location.origin + '/') === 0) {
          own = true;
          frames.push(url.split('?')[0].split('/').pop() + ':' + m[2] + ':' + m[3]);
        } else {
          frames.push('~');
        }
      }
    }
    return { text: frames.join(' < '), own: own, any: any };
  }

  function reportError(name, message, err, foreignSrc, extra) {
    var key = name + '|' + message;
    if (seenErrors[key] || errorCount >= CONFIG.maxErrorEvents) {
      return;
    }
    seenErrors[key] = true;
    errorCount++;
    // seq 1 is the first error of this page load, usually the root cause of
    // any that follow.
    var data = { message: clip(message, 180), seq: errorCount };
    if (CONFIG.build) {
      data.build = CONFIG.build;
    }
    for (var k in extra) {
      data[k] = extra[k];
    }
    var frames = stackFrames(err && err.stack);
    if (frames.text) {
      data.stack = clip(frames.text, 200);
    }
    // Third-party: a known injected global, or a stack (or failing script)
    // that never touches our origin. No location at all stays ours.
    if (THIRD_PARTY_RE.test(message) || (frames.any && !frames.own) || (!frames.any && foreignSrc)) {
      data.thirdParty = true;
    }
    track(name, data);
  }

  window.addEventListener('error', function (e) {
    var src = e.filename || '';
    if (!src || /^(chrome|moz|safari|safari-web)-extension:/.test(src)) {
      return;
    }
    var source = src.split('?')[0].split('/').pop() + ':' + (e.lineno || 0) + ':' + (e.colno || 0);
    reportError('js-error', e.message || 'error', e.error, src.indexOf(location.origin + '/') !== 0, { source: source });
  });
  window.addEventListener('unhandledrejection', function (e) {
    var r = e.reason;
    reportError('js-rejection', (r && r.message) || String(r), r, false, {});
  });

  // ── Search box (query text is never recorded) ────────────────────────────
  // One `search` event per use of the box: how long the query was, how many
  // results it showed, and whether one was chosen (and which position).
  var search = null;
  function searchResults() {
    var box = document.querySelector('.search-results');
    if (!box || box.hidden) {
      return -1;
    }
    return box.querySelectorAll('a').length;
  }
  function finishSearch(chosen, rank) {
    if (!search) {
      return;
    }
    var s = search;
    search = null;
    var data = { len: s.len, words: s.words, results: s.results, chosen: chosen };
    if (chosen && rank > 0) {
      data.rank = rank;
    }
    track('search', data);
  }
  function noteSearchInput(input) {
    var q = String(input.value || '').trim();
    if (!q) {
      // Cleared without choosing: that use of the box is over.
      if (search) {
        finishSearch(false, 0);
      }
      return;
    }
    if (!search) {
      search = { len: 0, words: 0, results: -1 };
    }
    search.len = Math.min(q.length, 200);
    search.words = q.split(/\s+/).length;
    // Results render asynchronously (the index loads on first use).
    setTimeout(function () {
      if (search) {
        search.results = searchResults();
      }
    }, 400);
  }
  function rankOf(link) {
    var box = link.closest('.search-results');
    var links = box ? box.querySelectorAll('a') : [];
    for (var i = 0; i < links.length; i++) {
      if (links[i] === link) {
        return i + 1;
      }
    }
    return 0;
  }
  document.addEventListener('input', function (e) {
    var t = e.target;
    if (t && t.closest && t.closest('.search') && t.tagName === 'INPUT') {
      noteSearchInput(t);
    }
  }, true);
  document.addEventListener('keydown', function (e) {
    var t = e.target;
    if (!search || !t || !t.closest || !t.closest('.search')) {
      return;
    }
    if (e.key === 'Enter') {
      var active = document.querySelector('.search-results a.active');
      if (active) {
        search.results = searchResults();
        finishSearch(true, rankOf(active));
      }
    } else if (e.key === 'Escape') {
      finishSearch(false, 0);
    }
  }, true);
  document.addEventListener('focusout', function (e) {
    var t = e.target;
    if (search && t && t.closest && t.closest('.search')) {
      // A click on a result blurs the box first; give the click time to land.
      setTimeout(function () {
        var a = document.activeElement;
        if (search && !(a && a.closest && a.closest('.search'))) {
          finishSearch(false, 0);
        }
      }, 400);
    }
  }, true);
  window.addEventListener('pagehide', function () {
    finishSearch(false, 0);
  });

  // ── Clicks: downloads, outbound links, search results, theme ─────────────
  document.addEventListener('click', function (e) {
    var t = e.target;
    if (!t || !t.closest) {
      return;
    }
    if (t.closest('.theme-button')) {
      // site.js flips the theme in its own handler, after this one.
      setTimeout(function () {
        var theme = document.documentElement.getAttribute('data-theme') || '';
        track('theme', { to: theme || 'system' });
      }, 0);
      return;
    }
    var a = t.closest('a[href]');
    if (!a) {
      return;
    }
    if (search && a.closest('.search-results')) {
      search.results = searchResults();
      finishSearch(true, rankOf(a));
    }
    var ev = classifyLink(a.href, location.hostname);
    if (ev) {
      var where = a.closest('.site-header') ? 'header'
        : a.closest('.site-footer') ? 'footer'
          : a.closest('.sidebar') ? 'sidebar' : 'page';
      ev.data.where = where;
      track(ev.name, ev.data);
    }
  }, true);

  // Copies made from code blocks (commands, scripts).
  document.addEventListener('copy', function () {
    var sel = document.getSelection && document.getSelection();
    var node = sel && sel.anchorNode;
    var el = node && (node.nodeType === 1 ? node : node.parentElement);
    if (el && el.closest && el.closest('pre, code')) {
      track('code-copy', {});
    }
  });

  // ── Reading: scroll depth through the article and headings reached ──────
  var content = document.querySelector('main .content') || document.querySelector('.content') || document.querySelector('main');

  /** Bottom of the article text, above the pager and footer site.js adds. */
  function articleBottom() {
    var end = content.querySelector('.pager') || content.querySelector('.site-footer');
    var rect = end ? end.getBoundingClientRect() : content.getBoundingClientRect();
    return (end ? rect.top : rect.bottom) + window.scrollY;
  }

  if (content && content.getBoundingClientRect) {
    var depthsHit = {};
    var readReported = false;
    /** Percent of the article above the bottom of the window, or -1 when the page hardly scrolls. */
    var measure = function () {
      var top = content.getBoundingClientRect().top + window.scrollY;
      var height = articleBottom() - top;
      if (height <= 0 || document.documentElement.scrollHeight - window.innerHeight < 200) {
        return -1;
      }
      var seen = window.scrollY + window.innerHeight - top;
      var pct = Math.max(0, Math.min(100, (seen / height) * 100));
      maxDepth = Math.max(maxDepth, Math.round(pct / 5) * 5);
      return pct;
    };
    // What the first screen shows. A page that fits on it has been seen whole.
    if (measure() < 0) {
      maxDepth = 100;
    }
    var onScroll = function () {
      var pct = measure();
      if (pct < 0) {
        return;
      }
      var marks = [25, 50, 75, 100];
      for (var i = 0; i < marks.length; i++) {
        var m = marks[i];
        if (pct >= m - 2 && !depthsHit[m]) {
          depthsHit[m] = true;
          track('read-depth', { depth: m });
        }
      }
      if (!readReported && depthsHit[75] && visibleSeconds() >= 30) {
        readReported = true;
        track('read-complete', { seconds: visibleSeconds() });
      }
    };
    var scrollTimer = 0;
    window.addEventListener('scroll', function () {
      if (!scrollTimer) {
        scrollTimer = setTimeout(function () {
          scrollTimer = 0;
          onScroll();
        }, 250);
      }
    }, { passive: true });

    // Headings (h2/h3 with ids, as site.js builds the page contents) count as
    // reached once they come into the top two thirds of the window.
    var headings = [];
    var all = content.querySelectorAll('h2[id], h3[id]');
    for (var i = 0; i < all.length; i++) {
      if (!all[i].closest('.card, .pager, .site-footer')) {
        headings.push(all[i]);
      }
    }
    sectionsTotal = headings.length;
    if (headings.length > 0 && typeof IntersectionObserver === 'function') {
      var reached = {};
      var io = new IntersectionObserver(function (entries) {
        for (var j = 0; j < entries.length; j++) {
          var el = entries[j].target;
          if (!entries[j].isIntersecting || reached[el.id]) {
            continue;
          }
          reached[el.id] = true;
          io.unobserve(el);
          sectionsReached++;
          if (sectionsReached <= CONFIG.maxSections) {
            track('section', { id: clip(el.id, 80), n: headings.indexOf(el) + 1, of: headings.length, level: el.tagName === 'H3' ? 3 : 2 });
          }
        }
      }, { rootMargin: '0px 0px -33% 0px' });
      for (var k = 0; k < headings.length; k++) {
        io.observe(headings[k]);
      }
    }
  }

  // Send whatever is queued when the page is hidden or unloaded. Registered
  // after the time-on-page, vitals and search listeners so their events ride
  // along.
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden') {
      flush();
    }
  });
  window.addEventListener('pagehide', flush);
})();
