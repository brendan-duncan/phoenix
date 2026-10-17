// Phoenix analytics dashboard. Plain DOM + inline SVG, no dependencies; the page
// CSP forbids inline scripts and style attributes, so every element is built
// with h()/s() and sized through the CSSOM (el.style.*), which CSP allows.
(() => {
  'use strict';

  // ── DOM helpers ────────────────────────────────────────────────────────
  const $ = (sel) => document.querySelector(sel);
  const SVG_NS = 'http://www.w3.org/2000/svg';

  function h(tag, attrs, ...children) {
    const el = document.createElement(tag);
    applyAttrs(el, attrs);
    appendAll(el, children);
    return el;
  }

  function s(tag, attrs, ...children) {
    const el = document.createElementNS(SVG_NS, tag);
    applyAttrs(el, attrs);
    appendAll(el, children);
    return el;
  }

  function applyAttrs(el, attrs) {
    if (!attrs) {
      return;
    }
    for (const [k, v] of Object.entries(attrs)) {
      if (v === null || v === undefined || v === false) {
        continue;
      }
      if (k.startsWith('on') && typeof v === 'function') {
        el.addEventListener(k.slice(2), v);
      } else {
        el.setAttribute(k, v === true ? '' : String(v));
      }
    }
  }

  function appendAll(el, children) {
    for (const c of children.flat()) {
      if (c === null || c === undefined || c === false) {
        continue;
      }
      el.append(c instanceof Node ? c : document.createTextNode(String(c)));
    }
  }

  // ── Formatting ─────────────────────────────────────────────────────────
  const nf = new Intl.NumberFormat('en-US');
  function fmtNum(n) {
    if (n === null || n === undefined || Number.isNaN(n)) {
      return '–';
    }
    const a = Math.abs(n);
    if (a >= 1e6) {
      return `${(n / 1e6).toFixed(a >= 1e7 ? 0 : 1)}M`;
    }
    if (a >= 1e4) {
      return `${(n / 1e3).toFixed(a >= 1e5 ? 0 : 1)}K`;
    }
    return nf.format(Math.round(n));
  }
  function fmtFull(n) {
    return n === null || n === undefined ? '–' : nf.format(Math.round(n));
  }
  function fmtPct(x, digits = 0) {
    return x === null || x === undefined || !Number.isFinite(x) ? '–' : `${(x * 100).toFixed(digits)}%`;
  }
  function fmtDur(sec) {
    if (sec === null || sec === undefined || !Number.isFinite(sec)) {
      return '–';
    }
    const t = Math.round(sec);
    if (t < 60) {
      return `${t}s`;
    }
    if (t < 3600) {
      return `${Math.floor(t / 60)}m ${t % 60}s`;
    }
    return `${Math.floor(t / 3600)}h ${Math.floor((t % 3600) / 60)}m`;
  }
  function fmtBytes(b) {
    if (b < 1024 * 1024) {
      return `${(b / 1024).toFixed(0)} KB`;
    }
    if (b < 1024 * 1024 * 1024) {
      return `${(b / 1048576).toFixed(1)} MB`;
    }
    return `${(b / 1073741824).toFixed(2)} GB`;
  }
  function fmtWhen(ts) {
    const d = new Date(ts);
    return d.toLocaleString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
  }
  function timeAgo(ts) {
    const sec = Math.max(0, Math.round((Date.now() - ts) / 1000));
    if (sec < 60) {
      return `${sec}s ago`;
    }
    if (sec < 3600) {
      return `${Math.floor(sec / 60)}m ago`;
    }
    return `${Math.floor(sec / 3600)}h ago`;
  }
  const regionNames = (() => {
    try {
      return new Intl.DisplayNames(['en'], { type: 'region' });
    } catch {
      return null;
    }
  })();
  function countryLabel(cc) {
    if (!cc) {
      return 'Unknown';
    }
    const flag = String.fromCodePoint(...[...cc.toUpperCase()].map((c) => 0x1f1a5 + c.charCodeAt(0)));
    let name = cc;
    try {
      name = regionNames?.of(cc) ?? cc;
    } catch {
      name = cc;
    }
    return `${flag}  ${name}`;
  }
  function countryName(cc) {
    try {
      return regionNames?.of(cc) ?? cc;
    } catch {
      return cc;
    }
  }
  /** Region keys are "US|California"; city keys "US|California|San Jose". */
  function regionLabel(key) {
    const [cc, region] = key.split('|');
    if (!cc) {
      return 'Unknown';
    }
    return `${region || 'Unknown region'}, ${countryName(cc)}`;
  }
  function cityLabel(key) {
    const [cc, region, city] = key.split('|');
    if (!cc) {
      return 'Unknown';
    }
    return [city || 'Unknown city', region, cc].filter(Boolean).join(', ');
  }
  const SECTION_LABEL = {
    home: 'Home', install: 'Install', tutorials: 'Tutorials index', tutorial: 'Tutorials', guide: 'Guide', about: 'About',
  };

  const DIM_LABEL = {
    referrer: (k) => k || '(direct / none)',
    country: countryLabel,
    region: regionLabel,
    city: cityLabel,
    // Empty for events recorded before versions were tracked.
    browserVersion: (k) => k || 'Not recorded',
    osVersion: (k) => k || 'Not recorded',
    screen: (k) => k || 'Unknown',
    lang: (k) => k || 'Unknown',
    kind: (k) => SECTION_LABEL[k] ?? (k || 'Unknown'),
  };
  function dimLabel(dim, key) {
    return (DIM_LABEL[dim] ?? ((k) => k || '(none)'))(key);
  }

  // ── State (mirrored to the URL hash so views are bookmarkable) ─────────
  const TABS = ['overview', 'realtime', 'events', 'reading', 'downloads', 'admin'];
  const FILTER_KEYS = ['path', 'kind', 'country', 'region', 'city', 'browser', 'browserVersion', 'os', 'osVersion',
    'device', 'screen', 'lang', 'referrer'];
  const FILTER_NAMES = {
    path: 'Page', kind: 'Section', country: 'Country', region: 'Region', city: 'City', browser: 'Browser',
    browserVersion: 'Browser version', os: 'OS', osVersion: 'OS version', device: 'Device',
    screen: 'Screen', lang: 'Language', referrer: 'Referrer',
  };
  const state = { tab: 'overview', range: '7d', from: '', to: '', filters: {}, event: '', prop: '' };

  function readHash() {
    const p = new URLSearchParams(location.hash.slice(1));
    state.tab = TABS.includes(p.get('tab') ?? '') ? p.get('tab') : 'overview';
    state.range = p.get('range') ?? '7d';
    state.from = p.get('from') ?? '';
    state.to = p.get('to') ?? '';
    state.event = p.get('event') ?? '';
    state.prop = p.get('prop') ?? '';
    state.filters = {};
    for (const k of FILTER_KEYS) {
      if (p.has(`f.${k}`)) {
        state.filters[k] = p.get(`f.${k}`);
      }
    }
  }

  function writeHash() {
    const p = new URLSearchParams();
    p.set('tab', state.tab);
    p.set('range', state.range);
    if (state.range === 'custom') {
      p.set('from', state.from);
      p.set('to', state.to);
    }
    if (state.event) {
      p.set('event', state.event);
    }
    if (state.prop) {
      p.set('prop', state.prop);
    }
    for (const [k, v] of Object.entries(state.filters)) {
      p.set(`f.${k}`, v);
    }
    history.replaceState(null, '', `#${p.toString()}`);
  }

  function localMidnight(d) {
    const x = new Date(d);
    x.setHours(0, 0, 0, 0);
    return x;
  }

  function currentRange() {
    const now = Date.now();
    const today = localMidnight(now);
    const daysBack = (n) => {
      const d = new Date(today);
      d.setDate(d.getDate() - n);
      return d.getTime();
    };
    switch (state.range) {
      case 'today':
        return { from: today.getTime(), to: now };
      case '24h':
        return { from: now - 86400000, to: now };
      case '30d':
        return { from: daysBack(29), to: now };
      case '90d':
        return { from: daysBack(89), to: now };
      case '12m': {
        const d = new Date(today);
        d.setDate(1);
        d.setMonth(d.getMonth() - 11);
        return { from: d.getTime(), to: now };
      }
      case 'all':
        return { from: new Date(2026, 0, 1).getTime(), to: now };
      case 'custom': {
        const f = state.from ? new Date(`${state.from}T00:00:00`) : new Date(daysBack(6));
        const t = state.to ? new Date(`${state.to}T00:00:00`) : today;
        t.setDate(t.getDate() + 1);
        return { from: f.getTime(), to: Math.min(t.getTime(), now + 1) };
      }
      default:
        return { from: daysBack(6), to: now };
    }
  }

  function unitFor(range) {
    const days = (range.to - range.from) / 86400000;
    return days <= 2 ? 'hour' : days <= 120 ? 'day' : 'month';
  }

  /** Bucket keys in the viewer's local time, matching the server's strftime output. */
  function bucketKeys(range, unit) {
    const keys = [];
    const pad = (n) => String(n).padStart(2, '0');
    const d = new Date(range.from);
    if (unit === 'hour') {
      d.setMinutes(0, 0, 0);
    } else {
      d.setHours(0, 0, 0, 0);
      if (unit === 'month') {
        d.setDate(1);
      }
    }
    while (d.getTime() < range.to && keys.length < 2000) {
      const ymd = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
      if (unit === 'hour') {
        keys.push(`${ymd} ${pad(d.getHours())}:00`);
        d.setHours(d.getHours() + 1);
      } else if (unit === 'day') {
        keys.push(ymd);
        d.setDate(d.getDate() + 1);
      } else {
        keys.push(`${d.getFullYear()}-${pad(d.getMonth() + 1)}`);
        d.setMonth(d.getMonth() + 1);
      }
    }
    return keys;
  }

  function bucketLabel(key, unit) {
    if (unit === 'hour') {
      return key.slice(11, 13) === '00' ? key.slice(5, 10) : key.slice(11, 16);
    }
    if (unit === 'month') {
      const [y, m] = key.split('-');
      return new Date(Number(y), Number(m) - 1, 1).toLocaleString(undefined, { month: 'short', year: '2-digit' });
    }
    const [y, m, d] = key.split('-');
    return new Date(Number(y), Number(m) - 1, Number(d)).toLocaleString(undefined, { month: 'short', day: 'numeric' });
  }

  // ── API ────────────────────────────────────────────────────────────────
  function query(extra = {}, withFilters = true) {
    const r = currentRange();
    const p = new URLSearchParams({ from: String(r.from), to: String(r.to), tz: String(-new Date().getTimezoneOffset()) });
    if (withFilters) {
      for (const [k, v] of Object.entries(state.filters)) {
        p.set(`f.${k}`, v);
      }
    }
    for (const [k, v] of Object.entries(extra)) {
      if (v !== undefined && v !== null && v !== '') {
        p.set(k, String(v));
      }
    }
    return p;
  }

  async function api(route, extra, withFilters) {
    const res = await fetch(`api/${route}?${query(extra, withFilters)}`, { headers: { 'x-phoenix-dash': '1' } });
    const body = await res.json().catch(() => ({}));
    if (!res.ok) {
      throw new Error(body.error ?? `${res.status} ${res.statusText}`);
    }
    return body;
  }

  async function post(route, body) {
    const res = await fetch(`api/${route}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'x-phoenix-dash': '1' },
      body: JSON.stringify(body ?? {}),
    });
    const out = await res.json().catch(() => ({}));
    if (!res.ok) {
      throw new Error(out.error ?? `${res.status} ${res.statusText}`);
    }
    return out;
  }

  // ── Tooltip ────────────────────────────────────────────────────────────
  const tip = $('#tooltip');
  function showTip(evt, title, rows) {
    tip.replaceChildren(
      h('div', { class: 't-title' }, title),
      ...rows.map((r) => h('div', { class: 't-row' },
        h('span', null, r.cls ? h('i', { class: `swatch legend-swatch ${r.cls}` }) : null, r.label),
        h('b', null, r.value))),
    );
    tip.hidden = false;
    const pad = 14;
    const w = tip.offsetWidth;
    const ht = tip.offsetHeight;
    let x = evt.clientX + pad;
    let y = evt.clientY + pad;
    if (x + w > window.innerWidth - 8) {
      x = evt.clientX - w - pad;
    }
    if (y + ht > window.innerHeight - 8) {
      y = evt.clientY - ht - pad;
    }
    tip.style.left = `${Math.max(8, x)}px`;
    tip.style.top = `${Math.max(8, y)}px`;
  }
  function hideTip() {
    tip.hidden = true;
  }

  // ── Charts ─────────────────────────────────────────────────────────────
  const redraws = [];
  /** Chart container → its data, so the Markdown export can emit a table. */
  const chartData = new WeakMap();

  function niceMax(v) {
    if (v <= 0) {
      return 4;
    }
    const exp = 10 ** Math.floor(Math.log10(v));
    const f = v / exp;
    const step = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10;
    return step * exp;
  }

  /** Bar with a 4px rounded data end, square at the baseline. */
  function barPath(x, y, w, hgt) {
    if (hgt <= 0) {
      return '';
    }
    const r = Math.min(4, w / 2, hgt);
    const y0 = y + hgt;
    return `M${x},${y0}V${y + r}Q${x},${y} ${x + r},${y}H${x + w - r}Q${x + w},${y} ${x + w},${y + r}V${y0}Z`;
  }

  /**
   * Grouped column chart. series: [{ name, cls: 'm1'|'m2', key: 's1'|'s2', values }]
   * One y-axis; a legend when there are two series; hover tooltip per bucket.
   */
  function columnChart(container, labels, series, opts = {}) {
    chartData.set(container, { labels, series, opts });
    const draw = () => {
      const width = Math.max(280, container.clientWidth || 800);
      const height = opts.height ?? 220;
      const m = { l: 44, r: 8, t: 8, b: 22 };
      const pw = width - m.l - m.r;
      const ph = height - m.t - m.b;
      const n = labels.length;
      const max = niceMax(Math.max(0, ...series.flatMap((sr) => sr.values)));
      const svg = s('svg', { viewBox: `0 0 ${width} ${height}`, height, role: 'img', 'aria-label': opts.label ?? 'chart' });
      for (let i = 0; i <= 4; i++) {
        const v = (max / 4) * i;
        const y = m.t + ph - (v / max) * ph;
        svg.append(s('line', { class: i === 0 ? 'baseline' : 'gridline', x1: m.l, x2: width - m.r, y1: y, y2: y }));
        svg.append(s('text', { class: 'tick', x: m.l - 6, y: y + 4, 'text-anchor': 'end' }, fmtNum(v)));
      }
      const band = pw / Math.max(1, n);
      const ns = series.length;
      const gap = 2;
      const barW = Math.max(1, Math.min(24, (band * 0.8 - gap * (ns - 1)) / ns));
      const groupW = barW * ns + gap * (ns - 1);
      const every = Math.max(1, Math.ceil(n / Math.max(2, Math.floor(pw / 70))));
      for (let i = 0; i < n; i++) {
        const x0 = m.l + band * i + (band - groupW) / 2;
        series.forEach((sr, j) => {
          const v = sr.values[i] ?? 0;
          const bh = (v / max) * ph;
          const d = barPath(x0 + j * (barW + gap), m.t + ph - bh, barW, bh);
          if (d) {
            svg.append(s('path', { class: sr.cls, d }));
          }
        });
        if (i % every === 0) {
          svg.append(s('text', { class: 'tick', x: m.l + band * i + band / 2, y: height - 6, 'text-anchor': 'middle' },
            opts.formatLabel ? opts.formatLabel(labels[i]) : labels[i]));
        }
        const hit = s('rect', { class: 'hit', x: m.l + band * i, y: m.t, width: band, height: ph });
        hit.addEventListener('mousemove', (evt) => showTip(evt, opts.tipTitle ? opts.tipTitle(labels[i]) : labels[i],
          series.map((sr) => ({ cls: sr.key, label: sr.name, value: fmtFull(sr.values[i] ?? 0) }))));
        hit.addEventListener('mouseleave', hideTip);
        svg.append(hit);
      }
      container.replaceChildren(svg);
    };
    redraws.push(draw);
    draw();
  }

  function legend(items) {
    return h('div', { class: 'legend' }, items.map((it) => h('span', { class: 'key' }, h('i', { class: `swatch ${it.cls}` }), it.label)));
  }

  function chartCard(title, sub, build) {
    const box = h('div', { class: 'chart' });
    const card = h('section', { class: 'card' }, h('h2', null, title), sub ? h('p', { class: 'sub' }, sub) : null);
    const extra = build(box);
    if (extra) {
      card.append(extra);
    }
    card.append(box);
    return card;
  }

  // ── Tables ─────────────────────────────────────────────────────────────

  /**
   * Breakdown rows with a proportional wash bar. cols: [{ label, get, fmt }];
   * the first numeric column drives the bar. onPick makes rows clickable.
   */
  function barTable(rows, { label, cols, onPick, selected, barOf }) {
    if (rows.length === 0) {
      return h('div', { class: 'empty' }, 'No data');
    }
    const barValue = barOf ?? cols[0].get;
    const max = Math.max(1, ...rows.map(barValue));
    const total = rows.reduce((a, r) => a + barValue(r), 0);
    return h('div', { class: 'scroll' }, h('table', { class: 'bars' },
      h('thead', null, h('tr', null, h('th', null, label), cols.map((c) => h('th', { class: 'num' }, c.label)))),
      h('tbody', null, rows.map((r) => {
        const bar = h('span', { class: 'bar' });
        bar.style.width = `${(barValue(r) / max) * 100}%`;
        const tr = h('tr', { class: [onPick ? 'clickable' : '', selected && selected(r) ? 'selected' : ''].join(' ').trim() || null },
          h('td', { class: 'label', title: r.title ?? r.labelText ?? '' }, bar, h('span', { class: 'text' }, r.labelText)),
          cols.map((c, i) => h('td', { class: 'num' }, (c.fmt ?? fmtNum)(c.get(r)),
            i === 0 && total > 0 && c.pct !== false ? h('span', { class: 'pct' }, fmtPct(c.get(r) / total)) : null)));
        if (onPick) {
          tr.addEventListener('click', () => onPick(r));
        }
        return tr;
      }))));
  }

  function plainTable(headers, rows) {
    if (rows.length === 0) {
      return h('div', { class: 'empty' }, 'No data');
    }
    return h('div', { class: 'scroll' }, h('table', null,
      h('thead', null, h('tr', null, headers.map((hd) => h('th', { class: hd.num ? 'num' : null }, hd.label)))),
      h('tbody', null, rows.map((cells) => h('tr', null, cells.map((c, i) => h('td', { class: headers[i].num ? 'num' : headers[i].wrap ? 'wrap' : null }, c)))))));
  }

  function pathLink(p) {
    return h('a', {
      href: '#',
      onclick: (e) => {
        e.preventDefault();
        addFilter('path', p);
      },
    }, p);
  }

  function statusLabel(level, text) {
    return h('span', { class: `status ${level}` }, text);
  }

  /** A card whose body loads per sub-tab. tabs: [{ id, label }], load(id) → Promise<Node>. */
  function tabbedCard(title, tabs, load) {
    const body = h('div', null, h('div', { class: 'loading' }, 'Loading…'));
    const buttons = tabs.map((t) => h('button', { 'aria-pressed': 'false' }, t.label));
    const pick = async (i) => {
      buttons.forEach((b, j) => b.setAttribute('aria-pressed', String(i === j)));
      body.replaceChildren(h('div', { class: 'loading' }, 'Loading…'));
      try {
        body.replaceChildren(await load(tabs[i].id));
      } catch (err) {
        body.replaceChildren(h('div', { class: 'error-box' }, err.message));
      }
    };
    buttons.forEach((b, i) => b.addEventListener('click', () => pick(i)));
    pick(0);
    return h('section', { class: 'card' },
      h('div', { class: 'card-head' }, h('h2', null, title), tabs.length > 1 ? h('div', { class: 'subtabs' }, buttons) : null),
      body);
  }

  function addFilter(key, value) {
    state.filters[key] = value;
    render();
  }

  function tile(label, value, delta) {
    return h('div', { class: 'tile' }, h('div', { class: 'label' }, label), h('div', { class: 'value' }, value), delta ?? null);
  }

  /** Signed change vs the previous period; arrow + sign carry direction, color only reinforces. */
  function deltaEl(cur, prev, higherIsGood = true) {
    if (!prev) {
      return h('div', { class: 'delta' }, cur ? 'new' : '–');
    }
    const ch = (cur - prev) / prev;
    if (Math.abs(ch) < 0.005) {
      return h('div', { class: 'delta' }, '→ 0% vs previous period');
    }
    const up = ch > 0;
    const good = up === higherIsGood;
    return h('div', { class: `delta ${good ? 'good' : 'bad'}` }, `${up ? '▲' : '▼'} ${up ? '+' : '−'}${fmtPct(Math.abs(ch))} vs previous period`);
  }

  // ── Views ──────────────────────────────────────────────────────────────

  async function viewOverview(root) {
    const range = currentRange();
    const unit = unitFor(range);
    const [summary, series] = await Promise.all([api('summary'), api('series', { unit })]);
    const c = summary.current;
    const p = summary.previous;
    root.append(h('div', { class: 'tiles' },
      tile('Views', fmtNum(c.views), deltaEl(c.views, p.views)),
      tile('Visitors', fmtNum(c.visitors), deltaEl(c.visitors, p.visitors)),
      tile('Visits', fmtNum(c.visits), deltaEl(c.visits, p.visits)),
      tile('Bounce rate', fmtPct(c.bounceRate), deltaEl(c.bounceRate, p.bounceRate, false)),
      tile('Visit duration', fmtDur(c.avgVisitSec), deltaEl(c.avgVisitSec, p.avgVisitSec)),
      tile('Downloads', fmtNum(c.downloads), deltaEl(c.downloads, p.downloads)),
    ));
    const keys = bucketKeys(range, series.unit);
    const byKey = new Map(series.points.map((pt) => [pt.t, pt]));
    root.append(h('div', { class: 'stack' }, chartCard('Views and visitors', `Per ${series.unit}, local time`, (box) => {
      const lg = legend([{ cls: 's1', label: 'Views' }, { cls: 's2', label: 'Visitors' }]);
      columnChart(box, keys, [
        { name: 'Views', cls: 'm1', key: 's1', values: keys.map((k) => byKey.get(k)?.views ?? 0) },
        { name: 'Visitors', cls: 'm2', key: 's2', values: keys.map((k) => byKey.get(k)?.visitors ?? 0) },
      ], { formatLabel: (k) => bucketLabel(k, series.unit), label: 'Views and visitors over time' });
      return lg;
    })));

    const cols = [
      { label: 'Visitors', get: (r) => r.visitors },
      { label: 'Views', get: (r) => r.views, pct: false },
    ];
    const breakdown = (dim, filterKey, limit = 100) => async () => {
      const rows = await api('breakdown', { dim, limit });
      return barTable(rows.map((r) => ({ ...r, labelText: dimLabel(dim, r.key) })), {
        label: '',
        cols,
        onPick: filterKey ? (r) => addFilter(filterKey, r.key) : null,
      });
    };
    const grid = h('div', { class: 'grid' });
    root.append(h('div', { class: 'stack' }, grid));
    grid.append(
      tabbedCard('Pages', [{ id: 'path', label: 'Pages' }, { id: 'entry', label: 'Entry' }, { id: 'exit', label: 'Exit' }, { id: 'query', label: 'With query' }],
        (id) => breakdown(id, id === 'query' ? null : 'path')()),
      tabbedCard('Referrers', [{ id: 'referrer', label: 'Referrers' }], () => breakdown('referrer', 'referrer')()),
      tabbedCard('Locations', [{ id: 'country', label: 'Countries' }, { id: 'region', label: 'Regions' }, { id: 'city', label: 'Cities' }],
        (id) => breakdown(id, id)()),
      tabbedCard('Environment', [{ id: 'browser', label: 'Browsers' }, { id: 'browserVersion', label: 'Versions' },
        { id: 'os', label: 'OS' }, { id: 'osVersion', label: 'OS versions' }, { id: 'device', label: 'Devices' }],
        (id) => breakdown(id, id)()),
      tabbedCard('Screens and languages', [{ id: 'screen', label: 'Screens' }, { id: 'lang', label: 'Languages' }], (id) => breakdown(id, id)()),
      tabbedCard('Sections', [{ id: 'kind', label: 'Sections' }], () => breakdown('kind', 'kind')()),
    );
    if (Object.keys(state.filters).length > 0) {
      root.append(h('p', { class: 'note' }, 'Filters apply to raw events only; days older than the retention window are excluded while a filter is set.'));
    }
  }

  let realtimeTimer = 0;
  async function viewRealtime(root) {
    const draw = async () => {
      const d = await api('realtime', {}, false);
      redraws.length = 0;
      const box = h('div');
      box.append(h('div', { class: 'tiles' },
        tile('Visitors, last 30 min', fmtNum(d.visitors)),
        tile('Views, last 30 min', fmtNum(d.views)),
        tile('Events, last 30 min', fmtNum(d.events)),
      ));
      const labels = Array.from({ length: 31 }, (_, i) => i);
      const byM = new Map(d.perMinute.map((pm) => [pm.m, pm]));
      box.append(h('div', { class: 'stack' }, chartCard('Views per minute', 'Last 30 minutes, refreshes every 10 seconds', (c) => {
        columnChart(c, labels, [
          { name: 'Views', cls: 'm1', key: 's1', values: labels.map((m) => byM.get(m)?.views ?? 0) },
        ], { height: 160, formatLabel: (m) => (m === 30 ? 'now' : `−${30 - m}m`), tipTitle: (m) => (m === 30 ? 'This minute' : `${30 - m} min ago`), label: 'Views per minute' });
        return null;
      })));
      const cols = [{ label: 'Visitors', get: (r) => r.visitors }, { label: 'Views', get: (r) => r.views, pct: false }];
      box.append(h('div', { class: 'grid' },
        h('section', { class: 'card' }, h('h2', null, 'Active pages'), barTable(d.pages.map((r) => ({ ...r, labelText: r.key })), { label: '', cols })),
        h('section', { class: 'card' }, h('h2', null, 'Countries'), barTable(d.countries.map((r) => ({ ...r, labelText: countryLabel(r.key) })), { label: '', cols })),
      ));
      box.append(h('div', { class: 'stack' }, h('section', { class: 'card' }, h('h2', null, 'Latest events'),
        plainTable([{ label: 'When' }, { label: 'Event' }, { label: 'Page', wrap: true }, { label: 'Where' }, { label: 'Detail', wrap: true }],
          d.recent.map((e) => [
            h('span', { class: 'when' }, timeAgo(e.ts)),
            e.name,
            e.path,
            `${e.country || '??'} · ${e.browser} · ${e.os}`,
            e.props ? Object.entries(e.props).slice(0, 4).map(([k, v]) => `${k}=${v}`).join('  ') : '',
          ])))));
      root.replaceChildren(box);
      for (const redraw of redraws) {
        redraw();
      }
    };
    await draw();
    realtimeTimer = window.setInterval(() => {
      if (document.visibilityState === 'visible' && state.tab === 'realtime') {
        draw().catch(() => {});
      }
    }, 10000);
  }

  async function viewEvents(root) {
    const names = await api('events');
    if (!state.event && names.length > 0) {
      state.event = names[0].name;
    }
    const list = h('section', { class: 'card' }, h('h2', null, 'Events'), h('p', { class: 'sub' }, 'Custom events (page views excluded). Click one for details.'),
      barTable(names.map((n) => ({ ...n, labelText: n.name })), {
        label: '',
        cols: [{ label: 'Count', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false }],
        onPick: (r) => {
          state.event = r.name;
          state.prop = '';
          render();
        },
        selected: (r) => r.name === state.event,
      }));
    const grid = h('div', { class: 'grid' }, list);
    root.append(grid);
    if (!state.event) {
      return;
    }
    const range = currentRange();
    const detail = await api('event', { name: state.event, unit: unitFor(range), prop: state.prop || undefined });
    if (!state.prop && detail.props.length > 0) {
      state.prop = detail.props[0].key;
      writeHash();
      Object.assign(detail, await api('event', { name: state.event, unit: unitFor(range), prop: state.prop }));
    }
    const keys = bucketKeys(range, detail.unit);
    const byKey = new Map(detail.series.map((pt) => [pt.t, pt.count]));
    const card = h('section', { class: 'card' }, h('h2', null, state.event), h('p', { class: 'sub' }, `Count per ${detail.unit}`));
    const box = h('div', { class: 'chart' });
    card.append(box);
    columnChart(box, keys, [{ name: state.event, cls: 'm1', key: 's1', values: keys.map((k) => byKey.get(k) ?? 0) }],
      { height: 170, formatLabel: (k) => bucketLabel(k, detail.unit), label: `${state.event} over time` });
    if (detail.props.length > 0) {
      card.append(h('hr', { class: 'sep' }), h('div', { class: 'card-head' }, h('h2', null, 'Properties'),
        h('div', { class: 'subtabs' }, detail.props.map((pr) => h('button', {
          'aria-pressed': String(pr.key === state.prop),
          onclick: () => {
            state.prop = pr.key;
            render();
          },
        }, pr.key)))));
      const v = detail.values;
      if (v?.stats) {
        const st = v.stats;
        card.append(h('dl', { class: 'kv' },
          h('dt', null, 'Samples'), h('dd', null, fmtFull(st.n)),
          h('dt', null, 'Median'), h('dd', null, fmtFull(st.p50)),
          h('dt', null, '95th percentile'), h('dd', null, fmtFull(st.p95)),
          h('dt', null, 'Min – max'), h('dd', null, `${fmtFull(st.min)} – ${fmtFull(st.max)}`),
          h('dt', null, 'Average'), h('dd', null, st.avg.toFixed(1)),
        ), h('hr', { class: 'sep' }));
      }
      card.append(barTable((v?.values ?? []).map((r) => ({ ...r, labelText: String(r.value) })), {
        label: state.prop,
        cols: [{ label: 'Count', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false }],
      }));
    }
    grid.append(card);
  }

  /** Headings reached on one page, as a funnel against that page's views. */
  function sectionFunnel(rows, views) {
    return barTable(rows.map((r) => ({ ...r, labelText: `${r.level === 3 ? ' ' : ''}${r.n}. #${r.id}`, rate: views > 0 ? r.visits / views : 0 })), {
      label: 'Heading',
      cols: [{ label: 'Reached', get: (r) => r.rate, fmt: (x) => fmtPct(x), pct: false }, { label: 'Visits', get: (r) => r.visits, pct: false }],
      barOf: (r) => r.rate,
    });
  }

  async function viewReading(root) {
    const d = await api('reading');
    const pctOf = (n, of) => (of > 0 ? fmtPct(n / of) : '–');
    root.append(h('div', { class: 'stack' }, h('section', { class: 'card' }, h('h2', null, 'Guide pages'),
      h('p', { class: 'sub' }, 'Scroll depth is per visit and measured through the article text; “completed” means 75% of it after at least 30 s. Average depth is how far down visits got.'),
      plainTable([{ label: 'Page', wrap: true }, { label: 'Views', num: true }, { label: 'Visitors', num: true }, { label: 'Reached 50%', num: true },
        { label: 'Reached end', num: true }, { label: 'Completed', num: true }, { label: 'Avg depth', num: true }, { label: 'Avg time', num: true },
        { label: 'Code copies', num: true }],
      d.pages.map((r) => [
        pathLink(r.path),
        fmtFull(r.views), fmtFull(r.visitors), pctOf(r.d50, r.views), pctOf(r.d100, r.views), pctOf(r.completes, r.views),
        r.avgDepth === null ? '–' : `${Math.round(r.avgDepth)}%`, fmtDur(r.avgSec), fmtFull(r.copies)])))));

    // Headings reached, one page at a time: the filtered page, else the most viewed one with data.
    const byPath = new Map();
    for (const r of d.sections) {
      const list = byPath.get(r.path) ?? [];
      list.push(r);
      byPath.set(r.path, list);
    }
    const viewsOf = new Map(d.pages.map((p) => [p.path, p.views]));
    const paths = [...byPath.keys()].sort((a, b) => (viewsOf.get(b) ?? 0) - (viewsOf.get(a) ?? 0));
    const funnelBody = h('div');
    const pick = h('select', { 'aria-label': 'Page' }, paths.map((p) => h('option', { value: p }, p)));
    const showFunnel = () => {
      const p = pick.value;
      funnelBody.replaceChildren(p ? sectionFunnel(byPath.get(p) ?? [], viewsOf.get(p) ?? 0) : h('div', { class: 'empty' }, 'No data'));
    };
    pick.value = state.filters.path && byPath.has(state.filters.path) ? state.filters.path : paths[0] ?? '';
    pick.addEventListener('change', showFunnel);
    showFunnel();
    root.append(h('div', { class: 'stack' }, h('section', { class: 'card' },
      h('div', { class: 'card-head' }, h('h2', null, 'Headings reached'), paths.length > 0 ? pick : null),
      h('p', { class: 'sub' }, 'Share of the page’s views that brought each heading into the top two thirds of the window'),
      funnelBody)));

    const s = d.search;
    const cnt = [{ label: 'Count', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false }];
    root.append(h('div', { class: 'grid' },
      h('section', { class: 'card' }, h('h2', null, 'Search box'),
        h('p', { class: 'sub' }, 'One use of the box per entry. Query text is never recorded, only its length.'),
        h('dl', { class: 'kv' },
          h('dt', null, 'Searches'), h('dd', null, fmtFull(s.count)),
          h('dt', null, 'Visitors searching'), h('dd', null, fmtFull(s.visitors)),
          h('dt', null, 'A result was chosen'), h('dd', null, pctOf(s.chosen, s.count)),
          h('dt', null, 'Found nothing'), h('dd', null, pctOf(s.zero, s.count)),
          h('dt', null, 'Average results shown'), h('dd', null, s.avgResults === null ? '–' : s.avgResults.toFixed(1)),
          h('dt', null, 'Average position chosen'), h('dd', null, s.avgRank === null ? '–' : s.avgRank.toFixed(1)),
          h('dt', null, 'Average query length'), h('dd', null, s.avgLen === null ? '–' : `${s.avgLen.toFixed(1)} characters`),
        ),
        h('hr', { class: 'sep' }),
        barTable(s.from.map((r) => ({ ...r, labelText: r.key })), {
          label: 'Searched from',
          cols: [{ label: 'Searches', get: (r) => r.count }, { label: 'Chosen', get: (r) => r.chosen, pct: false }],
        })),
      h('section', { class: 'card' }, h('h2', null, 'Tutorials opened'),
        h('p', { class: 'sub' }, '“From index” counts opens from the Interactive tutorials page'),
        barTable(d.tutorials.map((r) => ({ ...r, labelText: r.key })), {
          label: '',
          cols: [{ label: 'Opens', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false },
            { label: 'From index', get: (r) => r.fromIndex, pct: false }],
        })),
      h('section', { class: 'card' }, h('h2', null, 'How tutorials are reached'),
        h('p', { class: 'sub' }, 'The guide page before the tutorial, or outside the guide'),
        barTable(d.tutorialFrom.map((r) => ({ ...r, labelText: r.key === 'external' ? 'Another site' : r.key === 'direct' ? 'Direct / no referrer' : r.key })), { label: '', cols: cnt })),
      h('section', { class: 'card' }, h('h2', null, 'Theme toggles'),
        barTable(d.themes.map((r) => ({ ...r, labelText: r.key === 'dark' ? 'To dark' : r.key === 'light' ? 'To light' : r.key })), { label: '', cols: cnt })),
    ));
  }

  const OS_LABEL = { windows: 'Windows', macos: 'macOS', linux: 'Linux', '': 'Unknown', '(none)': 'Unknown' };
  const PACKAGE_LABEL = { installer: 'Installer', portable: 'Portable', appimage: 'AppImage', other: 'Other', '(none)': 'Unknown' };
  const TARGET_LABEL = {
    'github-releases': 'GitHub releases', 'github-issues': 'GitHub issues', github: 'GitHub repository', changelog: 'Changelog',
    linkedin: 'LinkedIn', mastodon: 'Mastodon', mail: 'Email', other: 'Other sites',
  };

  async function viewDownloads(root) {
    const range = currentRange();
    const d = await api('downloads', { unit: unitFor(range) });
    const t = d.tiles;
    root.append(h('div', { class: 'tiles' },
      tile('Downloads', fmtNum(t.downloads)),
      tile('Visitors downloading', fmtNum(t.downloaders)),
      tile('Share of visitors', fmtPct(t.rate, 1)),
      tile('Lesson files', fmtNum(t.lessonFiles)),
    ));
    const keys = bucketKeys(range, d.unit);
    const byKey = new Map(d.series.map((pt) => [pt.t, pt]));
    root.append(h('div', { class: 'stack' }, chartCard('Downloads over time', `Per ${d.unit}, local time`, (box) => {
      const lg = legend([{ cls: 's1', label: 'Phoenix' }, { cls: 's2', label: 'Lesson files' }]);
      columnChart(box, keys, [
        { name: 'Phoenix', cls: 'm1', key: 's1', values: keys.map((k) => byKey.get(k)?.apps ?? 0) },
        { name: 'Lesson files', cls: 'm2', key: 's2', values: keys.map((k) => byKey.get(k)?.lessons ?? 0) },
      ], { formatLabel: (k) => bucketLabel(k, d.unit), label: 'Downloads over time' });
      return lg;
    })));
    const cnt = [{ label: 'Downloads', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false }];
    root.append(h('div', { class: 'grid' },
      h('section', { class: 'card' }, h('h2', null, 'By file'),
        barTable(d.files.map((r) => ({ ...r, labelText: r.key })), { label: '', cols: cnt })),
      h('section', { class: 'card' }, h('h2', null, 'By platform'), h('p', { class: 'sub' }, 'The platform the file is for'),
        barTable(d.fileOs.map((r) => ({ ...r, labelText: OS_LABEL[r.key] ?? r.key })), { label: '', cols: cnt })),
      h('section', { class: 'card' }, h('h2', null, 'By kind'),
        barTable(d.packages.map((r) => ({ ...r, labelText: PACKAGE_LABEL[r.key] ?? r.key })), { label: '', cols: cnt })),
      h('section', { class: 'card' }, h('h2', null, 'Visitor OS and file'),
        h('p', { class: 'sub' }, 'The OS of the browser that clicked, against the platform of the file'),
        plainTable([{ label: 'Visitor OS' }, { label: 'File for' }, { label: 'Downloads', num: true }, { label: 'Visitors', num: true }],
          d.visitorOs.map((r) => [r.key, OS_LABEL[r.fileOs] ?? r.fileOs, fmtFull(r.count), fmtFull(r.visitors)]))),
      h('section', { class: 'card' }, h('h2', null, 'Download pages'), h('p', { class: 'sub' }, 'Where the download link was clicked'),
        barTable(d.pages.map((r) => ({ ...r, labelText: r.key })), { label: '', cols: cnt, onPick: (r) => addFilter('path', r.key) })),
      h('section', { class: 'card' }, h('h2', null, 'Lesson files'), h('p', { class: 'sub' }, 'Prepared artwork downloaded from tutorial pages'),
        barTable(d.lessons.map((r) => ({ ...r, labelText: r.key })), { label: '', cols: cnt })),
    ));
    const clicks = [{ label: 'Clicks', get: (r) => r.count }, { label: 'Visitors', get: (r) => r.visitors, pct: false }];
    root.append(h('div', { class: 'grid' },
      h('section', { class: 'card' }, h('h2', null, 'Outbound links'),
        barTable(d.outbound.map((r) => ({ ...r, labelText: TARGET_LABEL[r.key] ?? r.key })), { label: '', cols: clicks })),
      h('section', { class: 'card' }, h('h2', null, 'Outbound URLs'),
        barTable(d.outboundUrls.map((r) => ({ ...r, labelText: r.key === '(none)' ? 'Email' : r.key })), { label: '', cols: clicks })),
    ));
  }

  // ── Admin ──────────────────────────────────────────────────────────────

  const HEALTH_REASONS = [
    ['stored', 'Stored'], ['accepted', 'Accepted'], ['bot', 'Bots'], ['invalid', 'Invalid payload'],
    ['ip-rate', 'Per-IP rate limit'], ['ip-blocked', 'IP penalty box'], ['global-rate', 'Global rate limit (requests)'],
    ['global-events', 'Global event limit'],
    ['daily-cap', 'Daily cap'], ['db-full', 'Database size cap'], ['overload', 'Write queue full'],
    ['paused', 'Ingestion paused'], ['too-large', 'Body too large (requests)'], ['write-error', 'Write errors'],
  ];

  function field(label, input, hint) {
    return h('div', { class: 'field' }, h('label', null, label), input, hint ? h('span', { class: 'hint' }, hint) : null);
  }

  function dateToMs(value, endOfDay = false) {
    if (!value) {
      return undefined;
    }
    const d = new Date(`${value}T00:00:00`);
    if (endOfDay) {
      d.setDate(d.getDate() + 1);
    }
    return d.getTime();
  }

  async function runAction(btn, out, fn) {
    btn.disabled = true;
    out.textContent = 'Working…';
    try {
      out.textContent = await fn();
    } catch (err) {
      out.textContent = `Error: ${err.message}`;
    } finally {
      btn.disabled = false;
    }
  }

  async function viewAdmin(root) {
    const st = await api('status', {}, false);
    const set = st.settings;
    const pctFull = st.sizeBytes / (set.maxDbMB * 1048576);

    const storage = h('section', { class: 'card' }, h('h2', null, 'Storage'),
      h('div', { class: `meter ${pctFull > 1 ? 'full' : pctFull > 0.8 ? 'warn' : ''}` }, (() => {
        const bar = h('span');
        bar.style.width = `${Math.min(100, pctFull * 100)}%`;
        return bar;
      })()),
      h('p', { class: 'sub' }, `${fmtBytes(st.sizeBytes)} of ${fmtFull(set.maxDbMB)} MB cap (${fmtPct(pctFull)})`,
        st.dbFull ? ' — ' : '', st.dbFull ? statusLabel('critical', 'cap reached: new events are being dropped') : ''),
      h('dl', { class: 'kv' },
        h('dt', null, 'Raw events'), h('dd', null, fmtFull(st.events)),
        h('dt', null, 'Oldest / newest'), h('dd', null, st.oldest ? `${fmtWhen(st.oldest)} → ${fmtWhen(st.newest)}` : '–'),
        h('dt', null, 'Daily rollups'), h('dd', null, `${fmtFull(st.rollupDays)} days${st.firstRollupDay ? ` since ${st.firstRollupDay}` : ''}`),
        h('dt', null, 'Reclaimable space'), h('dd', null, fmtBytes(st.freeBytes)),
        h('dt', null, 'Stored today (UTC)'), h('dd', null, `${fmtFull(st.storedToday)} of ${fmtFull(set.dailyEventCap)} cap`),
        h('dt', null, 'Write queue'), h('dd', null, fmtFull(st.queue)),
        h('dt', null, 'Collector memory'), h('dd', null, `${st.rssMB} MB RSS, up ${fmtDur(st.uptimeSec)}`),
        h('dt', null, 'Tracked / blocked IPs'), h('dd', null, `${fmtFull(st.trackedIps)} / ${fmtFull(st.blockedIps)}`),
        h('dt', null, 'Location lookup'), h('dd', null, !st.geo ? statusLabel('warning', 'no geo.db — run update-geo')
          : st.geoCities ? statusLabel('good', 'city level') : statusLabel('warning', 'country only — run update-geo for regions and cities')),
        h('dt', null, 'Last maintenance'), h('dd', null, st.lastMaintenance ? (() => {
          const m = JSON.parse(st.lastMaintenance);
          return `${fmtWhen(Date.parse(m.at))}: rolled ${m.rolled} day(s), deleted ${fmtFull(m.deleted)} before ${m.cutoff}`;
        })() : 'never'),
      ));

    // Settings form.
    const inputs = {
      retentionDays: h('input', { type: 'number', min: 1, max: 3650, value: set.retentionDays }),
      dailyEventCap: h('input', { type: 'number', min: 100, value: set.dailyEventCap }),
      maxDbMB: h('input', { type: 'number', min: 16, value: set.maxDbMB }),
      perIpPerMinute: h('input', { type: 'number', min: 5, value: set.perIpPerMinute }),
      globalPerSecond: h('input', { type: 'number', min: 5, value: set.globalPerSecond }),
      globalEventsPerMinute: h('input', { type: 'number', min: 60, value: set.globalEventsPerMinute }),
    };
    const paused = h('input', { type: 'checkbox', id: 'paused' });
    paused.checked = set.ingestPaused;
    const saveOut = h('span', { class: 'result' });
    const saveBtn = h('button', { class: 'btn primary' }, 'Save settings');
    saveBtn.addEventListener('click', () => runAction(saveBtn, saveOut, async () => {
      const body = { ingestPaused: paused.checked };
      for (const [k, el] of Object.entries(inputs)) {
        body[k] = Number(el.value);
      }
      await post('settings', body);
      return 'Saved.';
    }));
    const settings = h('section', { class: 'card' }, h('h2', null, 'Settings'),
      h('div', { class: 'form top' },
        field('Keep raw events (days)', inputs.retentionDays, 'Default 396 (13 months). Daily totals are kept forever.'),
        field('Daily event cap', inputs.dailyEventCap, 'Stop storing for the rest of the UTC day past this'),
        field('Database size cap (MB)', inputs.maxDbMB, 'Stop storing while the file is larger'),
        field('Per-IP limit (events/min)', inputs.perIpPerMinute, 'Repeat offenders are ignored for 15 min'),
        field('Global limit (requests/s)', inputs.globalPerSecond, 'Excess requests get 429 without being read'),
        field('Global limit (events/min)', inputs.globalEventsPerMinute, 'Across all IPs; caps what a rotating-IP flood can store'),
        h('div', { class: 'field' }, h('label', null, 'Ingestion'), h('span', { class: 'check' }, paused, h('label', { for: 'paused' }, 'Pause (drop everything)'))),
      ),
      h('div', { class: 'actions' }, saveBtn, saveOut));

    // Health counters.
    const today = new Date().toISOString().slice(0, 10);
    const weekAgo = new Date(Date.now() - 6 * 86400000).toISOString().slice(0, 10);
    const sum = (reason, from) => st.health.filter((r) => r.reason === reason && r.day >= from).reduce((a, r) => a + r.count, 0);
    const health = h('section', { class: 'card' }, h('h2', null, 'Ingestion health'),
      h('p', { class: 'sub' }, 'Events unless noted. Today and 7 days are UTC days.'),
      plainTable([{ label: 'Outcome' }, { label: 'Today', num: true }, { label: '7 days', num: true }, { label: 'Since restart', num: true }],
        HEALTH_REASONS.map(([id, label]) => [
          id === 'stored' || id === 'accepted' ? label : (sum(id, weekAgo) > 0 ? statusLabel(id === 'bot' || id === 'invalid' ? 'warning' : 'serious', label) : label),
          fmtFull(sum(id, today)), fmtFull(sum(id, weekAgo)), fmtFull(st.sinceStart[id] ?? 0)])));

    // Cleanup.
    const cleanOut = h('div', { class: 'result' });
    const olderThan = h('input', { type: 'date' });
    const nameSel = h('select', null, h('option', { value: '' }, '— event —'),
      st.eventNames.map((e) => h('option', { value: e.name }, `${e.name} (${fmtFull(e.count)})`)));
    const pathPrefix = h('input', { type: 'text', placeholder: '/tutorial-' });
    const rangeFrom = h('input', { type: 'date' });
    const rangeTo = h('input', { type: 'date' });
    const alsoRollups = h('input', { type: 'checkbox', id: 'also-rollups' });
    const doCleanup = (btn, crit, describe) => {
      if (!window.confirm(`Permanently delete ${describe}? This cannot be undone.`)) {
        return;
      }
      runAction(btn, cleanOut, async () => {
        const r = await post('cleanup', crit);
        return `Deleted ${fmtFull(r.deleted)} raw event(s)${r.rollupsDeleted ? ` and ${fmtFull(r.rollupsDeleted)} rollup row(s)` : ''}.`;
      });
    };
    const b1 = h('button', { class: 'btn danger' }, 'Delete older');
    b1.addEventListener('click', () => {
      if (!olderThan.value) {
        cleanOut.textContent = 'Pick a date first.';
        return;
      }
      doCleanup(b1, { before: dateToMs(olderThan.value) }, `all raw events before ${olderThan.value}`);
    });
    const b2 = h('button', { class: 'btn danger' }, 'Delete event');
    b2.addEventListener('click', () => {
      if (!nameSel.value) {
        cleanOut.textContent = 'Pick an event first.';
        return;
      }
      doCleanup(b2, { name: nameSel.value }, `every '${nameSel.value}' event`);
    });
    const b3 = h('button', { class: 'btn danger' }, 'Delete path');
    b3.addEventListener('click', () => {
      if (!pathPrefix.value.startsWith('/')) {
        cleanOut.textContent = 'Enter a path prefix starting with /.';
        return;
      }
      doCleanup(b3, { pathPrefix: pathPrefix.value }, `all raw events on paths starting with ${pathPrefix.value}`);
    });
    const b4 = h('button', { class: 'btn danger' }, 'Delete range');
    b4.addEventListener('click', () => {
      if (!rangeFrom.value || !rangeTo.value) {
        cleanOut.textContent = 'Pick both dates.';
        return;
      }
      doCleanup(b4, { from: dateToMs(rangeFrom.value), to: dateToMs(rangeTo.value, true), alsoRollups: alsoRollups.checked },
        `raw events from ${rangeFrom.value} through ${rangeTo.value}${alsoRollups.checked ? ' and their daily totals' : ''}`);
    });
    const maintBtn = h('button', { class: 'btn' }, 'Run maintenance now');
    maintBtn.addEventListener('click', () => runAction(maintBtn, cleanOut, async () => {
      const r = await post('maintenance');
      return `Rolled up ${r.rolled} day(s); deleted ${fmtFull(r.deleted)} event(s) older than ${r.cutoff}.`;
    }));
    const vacBtn = h('button', { class: 'btn' }, 'Compact database (VACUUM)');
    vacBtn.addEventListener('click', () => {
      if (!window.confirm('VACUUM rewrites the whole file and pauses writes while it runs. Continue?')) {
        return;
      }
      runAction(vacBtn, cleanOut, async () => {
        const r = await post('vacuum');
        return `Compacted: ${fmtBytes(r.before)} → ${fmtBytes(r.after)}.`;
      });
    });
    const r = currentRange();
    const exportLink = h('a', { class: 'btn', href: `api/export?from=${r.from}&to=${r.to}`, download: '' }, 'Export current range (CSV)');
    const cleanup = h('section', { class: 'card' }, h('h2', null, 'Cleanup'),
      h('p', { class: 'sub' }, 'Deletes raw events in small chunks so collection keeps running. Space is reused automatically; compact to shrink the file.'),
      h('div', { class: 'form' },
        field('Raw events older than', olderThan), h('div', { class: 'field' }, b1),
        field('All events named', nameSel), h('div', { class: 'field' }, b2),
        field('All events on paths starting with', pathPrefix), h('div', { class: 'field' }, b3),
        field('From', rangeFrom), field('Through', rangeTo),
        h('div', { class: 'field' }, h('span', { class: 'check' }, alsoRollups, h('label', { for: 'also-rollups' }, 'Also delete daily totals'))),
        h('div', { class: 'field' }, b4)),
      h('div', { class: 'actions' }, maintBtn, vacBtn, exportLink),
      cleanOut);

    // Password.
    const cur = h('input', { type: 'password', autocomplete: 'current-password' });
    const next = h('input', { type: 'password', autocomplete: 'new-password', minlength: 10 });
    const pwOut = h('span', { class: 'result' });
    const pwBtn = h('button', { class: 'btn' }, 'Change password');
    pwBtn.addEventListener('click', () => runAction(pwBtn, pwOut, async () => {
      await post('password', { current: cur.value, next: next.value });
      cur.value = '';
      next.value = '';
      return 'Changed. Your browser will ask you to log in again.';
    }));
    const password = h('section', { class: 'card' }, h('h2', null, 'Dashboard password'),
      h('div', { class: 'form top' }, field('Current password', cur), field('New password', next, 'At least 10 characters')),
      h('div', { class: 'actions' }, pwBtn, pwOut));

    root.append(h('div', { class: 'grid' }, storage, settings), h('div', { class: 'stack' }, h('div', { class: 'grid' }, health, password), cleanup));
  }

  // ── Download (PDF, standalone HTML, Markdown) ──────────────────────────

  function localYmd(ts) {
    const d = new Date(ts);
    const pad = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
  }

  function fileBase() {
    return `phoenix-analytics-${state.tab}-${localYmd(Date.now())}`;
  }

  /** Title and context lines describing what the current view shows. */
  function reportMeta() {
    const tabName = document.querySelector(`#tabs button[data-tab="${state.tab}"]`)?.textContent ?? state.tab;
    const lines = [];
    if (state.tab === 'realtime') {
      lines.push('Last 30 minutes (snapshot)');
    } else {
      const r = currentRange();
      const name = state.range === 'custom' ? 'Custom range' : $('#range').selectedOptions[0]?.textContent ?? state.range;
      lines.push(`${name}: ${localYmd(r.from)} to ${localYmd(r.to - 1)}`);
      const filters = Object.entries(state.filters);
      if (filters.length > 0) {
        lines.push(`Filters: ${filters.map(([k, v]) => `${FILTER_NAMES[k] ?? k} = ${dimLabel(k, v)}`).join('; ')}`);
      }
    }
    lines.push(`Generated ${new Date().toLocaleString()}`);
    return { title: `Phoenix analytics: ${tabName}`, lines };
  }

  function saveFile(data, type, name) {
    const url = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], { type }));
    const a = h('a', { href: url, download: name });
    document.body.append(a);
    a.click();
    a.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function escHtml(text) {
    return text.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  }

  /** A self-contained page: the dashboard stylesheet inlined around a snapshot of the view. */
  function exportHtml() {
    const meta = reportMeta();
    const css = [...document.styleSheets].flatMap((sheet) => {
      try {
        return [...sheet.cssRules].map((rule) => rule.cssText);
      } catch {
        return [];
      }
    }).join('\n');
    const snap = $('#view').cloneNode(true);
    snap.querySelectorAll('.hit').forEach((el) => el.remove());
    snap.querySelectorAll('.subtabs button:not([aria-pressed="true"])').forEach((el) => el.remove());
    snap.querySelectorAll('a[href="#"]').forEach((a) => a.replaceWith(h('span', null, a.textContent)));
    const head = h('header', { class: 'report-head' }, h('h1', null, meta.title), meta.lines.map((l) => h('p', null, l)));
    const theme = document.documentElement.getAttribute('data-theme');
    const html = `<!DOCTYPE html>
<html lang="en"${theme ? ` data-theme="${theme}"` : ''}>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex, nofollow">
<title>${escHtml(meta.title)}</title>
<style>
${css}
</style>
</head>
<body class="exported">
<main class="view">
${head.outerHTML}
${snap.innerHTML}
</main>
<footer class="footer">IP geolocation by <a href="https://db-ip.com" rel="noopener">DB-IP</a> (CC BY 4.0).</footer>
</body>
</html>
`;
    saveFile(html, 'text/html;charset=utf-8', `${fileBase()}.html`);
  }

  // Markdown and PDF share one walk of the rendered view into report blocks:
  //   { type: 'heading', level: 2 | 3, text }   { type: 'caption', text }
  //   { type: 'table', headers, rows, numeric }  { type: 'chart', labels, series }
  // Cell text may hold one '\n': a message above its stack trace.

  const squash = (text) => text.replace(/\s+/g, ' ').trim();

  /** Cell text with the pieces that sit side by side on screen kept apart. */
  function cellText(td) {
    const c = td.cloneNode(true);
    c.querySelectorAll('.pct').forEach((el) => el.replaceWith(` (${el.textContent})`));
    let stack = '';
    c.querySelectorAll('.err-stack').forEach((el) => {
      stack = squash(el.textContent);
      el.remove();
    });
    const text = squash(c.textContent);
    return stack ? `${text}\n${stack}` : text;
  }

  function tableBlock(table) {
    const ths = [...table.querySelectorAll('thead th')];
    return {
      type: 'table',
      headers: ths.map((th, i) => squash(th.textContent) || (i === 0 ? 'Name' : '')),
      rows: [...table.querySelectorAll('tbody tr')].map((tr) => [...tr.children].map(cellText)),
      numeric: ths.map((th) => th.classList.contains('num')),
    };
  }

  function chartBlock(box) {
    const data = chartData.get(box);
    if (!data) {
      return null;
    }
    const { labels, series, opts } = data;
    return {
      type: 'chart',
      labels: labels.map((l) => String(opts.tipTitle ? opts.tipTitle(l) : l)),
      shortLabels: labels.map((l) => String(opts.formatLabel ? opts.formatLabel(l) : l)),
      series: series.map((sr) => ({ name: sr.name, key: sr.key, values: labels.map((_, i) => sr.values[i] ?? 0) })),
    };
  }

  function collectBlocks(el, out, card) {
    for (const c of el.children) {
      const cls = c.classList;
      const heading = (text) => {
        // The card title is level 2; later headings inside the same card are level 3.
        out.push({ type: 'heading', level: card && card.headings++ > 0 ? 3 : 2, text });
      };
      if (cls.contains('tiles')) {
        const tiles = [...c.children].map((t) => [t.querySelector('.label'), t.querySelector('.value'), t.querySelector('.delta')]
          .map((x) => (x ? squash(x.textContent) : '')));
        const hasDelta = tiles.some((t) => t[2]);
        out.push({
          type: 'table',
          headers: hasDelta ? ['Metric', 'Value', 'Change'] : ['Metric', 'Value'],
          rows: tiles.map((t) => (hasDelta ? t : t.slice(0, 2))),
          numeric: [false, true, false],
        });
      } else if (cls.contains('card')) {
        collectBlocks(c, out, { headings: 0 });
      } else if (cls.contains('card-head')) {
        const title = squash(c.querySelector('h2')?.textContent ?? '');
        const active = c.querySelector('.subtabs button[aria-pressed="true"]');
        const sub = active ? squash(active.textContent) : '';
        heading(sub && sub !== title ? `${title}: ${sub}` : title);
      } else if (c.tagName === 'H2') {
        heading(squash(c.textContent));
      } else if (c.tagName === 'P' || cls.contains('empty') || cls.contains('loading') || cls.contains('error-box')) {
        out.push({ type: 'caption', text: squash(c.textContent) });
      } else if (c.tagName === 'TABLE') {
        out.push(tableBlock(c));
      } else if (cls.contains('chart')) {
        const block = chartBlock(c);
        if (block) {
          out.push(block);
        }
      } else if (c.tagName === 'DL') {
        out.push({
          type: 'table',
          headers: ['', ''],
          rows: [...c.querySelectorAll('dt')].map((dt) => [squash(dt.textContent), squash(dt.nextElementSibling?.textContent ?? '')]),
          numeric: [],
        });
      } else if (c.tagName === 'HR' || cls.contains('legend') || c.tagName === 'svg') {
        continue;
      } else if (c.children.length > 0) {
        collectBlocks(c, out, card);
      } else if (c.textContent.trim()) {
        out.push({ type: 'caption', text: squash(c.textContent) });
      }
    }
  }

  function reportBlocks() {
    const out = [];
    collectBlocks($('#view'), out, null);
    return out;
  }

  // ── Markdown ──

  function mdEscape(text) {
    return text.replace(/\\/g, '\\\\').replace(/\|/g, '\\|');
  }

  function mdCell(text) {
    const [main, stack] = text.split('\n');
    return stack ? `${mdEscape(main)}<br>\`${mdEscape(stack).replace(/`/g, "'")}\`` : mdEscape(main);
  }

  function mdTable(headers, rows, numeric = []) {
    if (rows.length === 0) {
      return '_No data_';
    }
    const line = (cells) => `| ${cells.join(' | ')} |`;
    return [
      line(headers.map((x) => mdEscape(x) || ' ')),
      line(headers.map((_, i) => (numeric[i] ? '---:' : '---'))),
      ...rows.map((r) => line(r.map(mdCell))),
    ].join('\n');
  }

  function exportMarkdown() {
    const meta = reportMeta();
    const out = [`# ${meta.title}`, '', meta.lines.join('  \n'), ''];
    for (const b of reportBlocks()) {
      if (b.type === 'heading') {
        out.push(`${'#'.repeat(b.level)} ${b.text}`, '');
      } else if (b.type === 'caption') {
        out.push(`_${mdEscape(b.text)}_`, '');
      } else if (b.type === 'table') {
        out.push(mdTable(b.headers, b.rows, b.numeric), '');
      } else if (b.type === 'chart') {
        out.push(mdTable(['Period', ...b.series.map((sr) => sr.name)],
          b.labels.map((l, i) => [l, ...b.series.map((sr) => fmtFull(sr.values[i]))]),
          [false, ...b.series.map(() => true)]), '');
      }
    }
    saveFile(`${out.join('\n').replace(/\n{3,}/g, '\n\n').trim()}\n`, 'text/markdown;charset=utf-8', `${fileBase()}.md`);
  }

  // ── PDF (jsPDF + AutoTable, vendored and loaded on first use) ──

  let pdfLibs = null;
  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const el = h('script', { src });
      el.addEventListener('load', resolve);
      el.addEventListener('error', () => reject(new Error(`couldn't load ${src}`)));
      document.head.append(el);
    });
  }
  function loadPdfLibs() {
    // AutoTable's browser build attaches to the jspdf global, so load it second.
    pdfLibs ??= loadScript('jspdf.umd.min.js').then(() => loadScript('jspdf.plugin.autotable.min.js')).catch((err) => {
      pdfLibs = null;
      throw err;
    });
    return pdfLibs;
  }

  /**
   * The built-in PDF fonts only cover Windows-1252, so swap or drop what they
   * can't draw (flags, arrows) instead of letting it print as garbage.
   */
  const WIN1252_EXTRA = new Set('€‚ƒ„…†‡ˆ‰Š‹ŒŽ‘’“”•–—˜™š›œžŸ');
  function pdfText(text) {
    return text
      .replace(/[\u{1F1E6}-\u{1F1FF}]/gu, '')
      .replace(/→/g, '->').replace(/←/g, '<-').replace(/[▲▼]\s?/g, '').replace(/−/g, '-')
      .replace(/≥/g, '>=').replace(/≤/g, '<=').replace(/↻/g, '')
      .replace(/./gu, (ch) => (ch.codePointAt(0) <= 0xff || WIN1252_EXTRA.has(ch) ? ch : '?'))
      .trim();
  }

  // Light-theme chart tokens from style.css, as RGB for jsPDF.
  const PDF_COLOR = {
    text: [11, 11, 11], text2: [82, 81, 78], muted: [137, 135, 129], grid: [225, 224, 217], axis: [195, 194, 183],
    head: [243, 243, 240], s1: [42, 120, 214], s2: [235, 104, 52],
  };

  function pdfChart(doc, b, x, y, width) {
    const height = 150;
    const m = { l: 36, b: 16, t: 14 };
    const pw = width - m.l;
    const ph = height - m.t - m.b;
    const n = b.labels.length;
    const max = niceMax(Math.max(0, ...b.series.flatMap((sr) => sr.values)));
    // Legend.
    let lx = x + m.l;
    doc.setFontSize(7);
    for (const sr of b.series) {
      doc.setFillColor(...PDF_COLOR[sr.key]);
      doc.rect(lx, y + 2, 6, 6, 'F');
      doc.setTextColor(...PDF_COLOR.text2);
      doc.text(pdfText(sr.name), lx + 9, y + 7.5);
      lx += 14 + doc.getTextWidth(pdfText(sr.name));
    }
    // Grid and y ticks.
    for (let i = 0; i <= 4; i++) {
      const gy = y + m.t + ph - (ph * i) / 4;
      doc.setDrawColor(...(i === 0 ? PDF_COLOR.axis : PDF_COLOR.grid));
      doc.setLineWidth(0.5);
      doc.line(x + m.l, gy, x + width, gy);
      doc.setTextColor(...PDF_COLOR.muted);
      doc.text(fmtNum((max / 4) * i), x + m.l - 4, gy + 2.5, { align: 'right' });
    }
    // Bars, grouped per bucket.
    const band = pw / Math.max(1, n);
    const ns = b.series.length;
    const barW = Math.max(0.5, Math.min(14, (band * 0.8 - (ns - 1)) / ns));
    const groupW = barW * ns + (ns - 1);
    const every = Math.max(1, Math.ceil(n / Math.max(2, Math.floor(pw / 50))));
    for (let i = 0; i < n; i++) {
      const x0 = x + m.l + band * i + (band - groupW) / 2;
      b.series.forEach((sr, j) => {
        const bh = (sr.values[i] / max) * ph;
        if (bh > 0) {
          doc.setFillColor(...PDF_COLOR[sr.key]);
          doc.rect(x0 + j * (barW + 1), y + m.t + ph - bh, barW, bh, 'F');
        }
      });
      if (i % every === 0) {
        doc.setTextColor(...PDF_COLOR.muted);
        doc.text(pdfText(b.shortLabels[i]), x + m.l + band * i + band / 2, y + height - 4, { align: 'center' });
      }
    }
    return y + height;
  }

  async function exportPdf() {
    await loadPdfLibs();
    const meta = reportMeta();
    const blocks = reportBlocks();
    const { jsPDF } = window.jspdf;
    const doc = new jsPDF({ unit: 'pt', format: 'letter' });
    doc.setProperties({ title: meta.title, creator: 'Phoenix analytics' });
    const margin = 40;
    const pageW = doc.internal.pageSize.getWidth();
    const pageH = doc.internal.pageSize.getHeight();
    const width = pageW - margin * 2;
    // Start a new page when fewer than `need` points remain.
    const room = (y, need) => {
      if (y + need > pageH - margin) {
        doc.addPage();
        return margin;
      }
      return y;
    };

    let y = margin;
    doc.setFont('helvetica', 'bold');
    doc.setFontSize(18);
    doc.setTextColor(...PDF_COLOR.text);
    doc.text(pdfText(meta.title), margin, y + 14);
    y += 24;
    doc.setFont('helvetica', 'normal');
    doc.setFontSize(9);
    doc.setTextColor(...PDF_COLOR.text2);
    for (const line of meta.lines) {
      doc.text(pdfText(line), margin, y + 9);
      y += 12;
    }
    y += 8;

    for (const b of blocks) {
      if (b.type === 'heading') {
        // Keep a heading with at least the start of what follows it.
        y = room(y + 6, 70);
        doc.setFont('helvetica', 'bold');
        doc.setFontSize(b.level === 2 ? 12 : 10);
        doc.setTextColor(...PDF_COLOR.text);
        doc.text(pdfText(b.text), margin, y + 10);
        y += b.level === 2 ? 16 : 14;
      } else if (b.type === 'caption') {
        doc.setFont('helvetica', 'normal');
        doc.setFontSize(8);
        doc.setTextColor(...PDF_COLOR.text2);
        const lines = doc.splitTextToSize(pdfText(b.text), width);
        y = room(y, lines.length * 10);
        doc.text(lines, margin, y + 8);
        y += lines.length * 10 + 4;
      } else if (b.type === 'chart') {
        y = room(y, 160);
        doc.setFont('helvetica', 'normal');
        y = pdfChart(doc, b, margin, y, width) + 10;
      } else if (b.type === 'table') {
        if (b.rows.length === 0) {
          continue;
        }
        const columnStyles = {};
        b.numeric.forEach((isNum, i) => {
          if (isNum) {
            columnStyles[i] = { halign: 'right' };
          }
        });
        const plainHead = b.headers.every((x) => !x);
        doc.autoTable({
          startY: y,
          margin: { left: margin, right: margin, top: margin, bottom: margin },
          head: plainHead ? undefined : [b.headers.map(pdfText)],
          body: b.rows.map((r) => r.map(pdfText)),
          theme: 'plain',
          styles: { font: 'helvetica', fontSize: 8, cellPadding: { top: 3, bottom: 3, left: 4, right: 4 }, textColor: PDF_COLOR.text, overflow: 'linebreak', lineColor: PDF_COLOR.grid },
          headStyles: { fontStyle: 'bold', textColor: PDF_COLOR.text2, fillColor: PDF_COLOR.head },
          columnStyles,
          bodyStyles: { lineWidth: { bottom: 0.5 } },
          rowPageBreak: 'avoid',
          didParseCell: (data) => {
            if (data.section === 'head' && b.numeric[data.column.index]) {
              data.cell.styles.halign = 'right';
            }
            // Error cells carry their stack trace on a second line; set them smaller.
            if (data.section === 'body' && typeof data.cell.raw === 'string' && data.cell.raw.includes('\n')) {
              data.cell.styles.fontSize = 7;
            }
          },
        });
        y = doc.lastAutoTable.finalY + 12;
      }
    }

    // Footer on every page.
    const pages = doc.getNumberOfPages();
    for (let i = 1; i <= pages; i++) {
      doc.setPage(i);
      doc.setFont('helvetica', 'normal');
      doc.setFontSize(7);
      doc.setTextColor(...PDF_COLOR.muted);
      doc.text(pdfText(meta.title), margin, pageH - 20);
      doc.text(`Page ${i} of ${pages}`, pageW - margin, pageH - 20, { align: 'right' });
    }
    saveFile(doc.output('blob'), 'application/pdf', `${fileBase()}.pdf`);
  }

  async function runExport(format) {
    const exporters = { pdf: exportPdf, html: exportHtml, md: exportMarkdown };
    try {
      await exporters[format]();
    } catch (err) {
      window.alert(`Download failed: ${err.message}`);
    }
  }

  // ── Shell ──────────────────────────────────────────────────────────────

  function renderFilters() {
    const box = $('#filters');
    const entries = Object.entries(state.filters);
    box.hidden = entries.length === 0 || state.tab === 'realtime' || state.tab === 'admin';
    box.replaceChildren(...entries.map(([k, v]) => h('span', { class: 'chip' },
      FILTER_NAMES[k] ?? k, ': ', h('b', null, dimLabel(k === 'referrer' ? 'referrer' : k, v)),
      h('button', { 'aria-label': `Remove ${FILTER_NAMES[k]} filter`, onclick: () => {
        delete state.filters[k];
        render();
      } }, '×'))));
  }

  const VIEWS = { overview: viewOverview, realtime: viewRealtime, events: viewEvents, reading: viewReading, downloads: viewDownloads, admin: viewAdmin };
  let renderSeq = 0;

  async function render() {
    const seq = ++renderSeq;
    window.clearInterval(realtimeTimer);
    hideTip();
    redraws.length = 0;
    writeHash();
    for (const b of document.querySelectorAll('#tabs button')) {
      b.setAttribute('aria-current', b.dataset.tab === state.tab ? 'page' : 'false');
    }
    $('#range').value = state.range;
    $('#custom-range').hidden = state.range !== 'custom';
    $('#custom-from').value = state.from;
    $('#custom-to').value = state.to;
    // Admin is settings and forms, not a report.
    $('#download').hidden = state.tab === 'admin';
    renderFilters();
    const view = $('#view');
    const root = h('div');
    view.replaceChildren(h('div', { class: 'loading' }, 'Loading…'));
    try {
      await VIEWS[state.tab](root);
      if (seq === renderSeq) {
        view.replaceChildren(root);
        for (const draw of redraws) {
          draw();
        }
      }
    } catch (err) {
      if (seq === renderSeq) {
        view.replaceChildren(h('div', { class: 'error-box' }, `Couldn't load: ${err.message}`));
      }
    }
  }

  function applyTheme(t) {
    if (t) {
      document.documentElement.setAttribute('data-theme', t);
    } else {
      document.documentElement.removeAttribute('data-theme');
    }
  }

  function init() {
    let theme = null;
    try {
      theme = localStorage.getItem('phoenix-dash-theme');
    } catch {
      theme = null;
    }
    applyTheme(theme);
    $('#theme').addEventListener('click', () => {
      const dark = document.documentElement.getAttribute('data-theme') === 'dark'
        || (!document.documentElement.getAttribute('data-theme') && window.matchMedia('(prefers-color-scheme: dark)').matches);
      const next = dark ? 'light' : 'dark';
      applyTheme(next);
      try {
        localStorage.setItem('phoenix-dash-theme', next);
      } catch {
        // Storage blocked: theme lasts for this page only.
      }
    });
    for (const b of document.querySelectorAll('#tabs button')) {
      b.addEventListener('click', () => {
        state.tab = b.dataset.tab;
        render();
      });
    }
    $('#range').addEventListener('change', (e) => {
      state.range = e.target.value;
      if (state.range === 'custom' && !state.from) {
        const d = new Date();
        state.to = d.toISOString().slice(0, 10);
        d.setDate(d.getDate() - 6);
        state.from = d.toISOString().slice(0, 10);
      }
      render();
    });
    $('#custom-from').addEventListener('change', (e) => {
      state.from = e.target.value;
      render();
    });
    $('#custom-to').addEventListener('change', (e) => {
      state.to = e.target.value;
      render();
    });
    $('#refresh').addEventListener('click', render);
    const download = $('#download');
    for (const b of download.querySelectorAll('button[data-format]')) {
      b.addEventListener('click', () => {
        download.open = false;
        runExport(b.dataset.format);
      });
    }
    document.addEventListener('click', (e) => {
      if (download.open && !download.contains(e.target)) {
        download.open = false;
      }
    });
    let resizeTimer = 0;
    window.addEventListener('resize', () => {
      window.clearTimeout(resizeTimer);
      resizeTimer = window.setTimeout(() => {
        for (const draw of redraws) {
          draw();
        }
      }, 150);
    });
    // Back/forward and hand-edited URLs (writeHash uses replaceState, which
    // doesn't fire this).
    window.addEventListener('hashchange', () => {
      readHash();
      render();
    });
    readHash();
    render();
  }

  init();
})();
