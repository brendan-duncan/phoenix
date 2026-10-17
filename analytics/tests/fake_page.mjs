// Just enough of a browser to run analytics/phoenix_analytics.js under node:
// a small DOM (elements, simple selectors, events), location, storage, timers
// run by hand, sendBeacon capture and a hand-driven IntersectionObserver.
import fs from 'node:fs';
import vm from 'node:vm';

const CLIENT = fs.readFileSync(new URL('../phoenix_analytics.js', import.meta.url), 'utf8');

// ── Selectors: tag, .class, [attr], #id, compound, descendant, comma lists ──

function parseCompound(text) {
  const out = { tag: '', classes: [], attrs: [], id: '' };
  const re = /([a-z0-9]+)|\.([\w-]+)|\[([\w-]+)\]|#([\w-]+)/gi;
  let m;
  while ((m = re.exec(text))) {
    if (m[1]) {
      out.tag = m[1].toUpperCase();
    } else if (m[2]) {
      out.classes.push(m[2]);
    } else if (m[3]) {
      out.attrs.push(m[3]);
    } else {
      out.id = m[4];
    }
  }
  return out;
}

function matchCompound(el, c) {
  if (!el || !el.tagName) {
    return false;
  }
  if (c.tag && el.tagName !== c.tag) {
    return false;
  }
  if (c.id && el.id !== c.id) {
    return false;
  }
  const cls = el.className.split(/\s+/);
  return c.classes.every((k) => cls.includes(k)) && c.attrs.every((a) => el.hasAttribute(a));
}

function matches(el, selector) {
  return selector.split(',').some((part) => {
    const chain = part.trim().split(/\s+/).map(parseCompound);
    if (!matchCompound(el, chain[chain.length - 1])) {
      return false;
    }
    let i = chain.length - 2;
    for (let p = el.parent; p && i >= 0; p = p.parent) {
      if (matchCompound(p, chain[i])) {
        i--;
      }
    }
    return i < 0;
  });
}

export class El {
  constructor(page, tag, attrs = {}, children = []) {
    this.page = page;
    this.tagName = tag.toUpperCase();
    this.attrs = {};
    this.children = [];
    this.parent = null;
    this.hidden = false;
    this.value = '';
    this.nodeType = 1;
    this.rect = { top: 0, bottom: 0 };
    for (const [k, v] of Object.entries(attrs)) {
      this.setAttribute(k, v);
    }
    for (const c of children) {
      this.append(c);
    }
  }

  get id() {
    return this.attrs.id ?? '';
  }

  get className() {
    return this.attrs.class ?? '';
  }

  get href() {
    return this.attrs.href === undefined ? '' : new URL(this.attrs.href, this.page.url).href;
  }

  get parentElement() {
    return this.parent;
  }

  setAttribute(k, v) {
    this.attrs[k] = String(v);
  }

  getAttribute(k) {
    return this.attrs[k] ?? null;
  }

  hasAttribute(k) {
    return k in this.attrs;
  }

  removeAttribute(k) {
    delete this.attrs[k];
  }

  append(child) {
    child.parent = this;
    this.children.push(child);
    return child;
  }

  *descendants() {
    for (const c of this.children) {
      yield c;
      yield* c.descendants();
    }
  }

  closest(selector) {
    for (let el = this; el; el = el.parent) {
      if (matches(el, selector)) {
        return el;
      }
    }
    return null;
  }

  querySelectorAll(selector) {
    return [...this.descendants()].filter((el) => matches(el, selector));
  }

  querySelector(selector) {
    return this.querySelectorAll(selector)[0] ?? null;
  }

  getBoundingClientRect() {
    return { top: this.rect.top - this.page.window.scrollY, bottom: this.rect.bottom - this.page.window.scrollY };
  }
}

class Target {
  constructor() {
    this.listeners = [];
  }

  addEventListener(type, fn, opts) {
    const capture = opts === true || (opts && opts.capture);
    this.listeners.push({ type, fn, capture: !!capture });
  }

  removeEventListener() {}

  fire(type, event = {}) {
    for (const l of this.listeners.filter((x) => x.type === type)) {
      l.fn({ type, ...event });
    }
  }
}

/**
 * A page at `url` whose body holds the elements made by `build(page)`, with
 * the client script run in it. `env` overrides navigator fields, storage and
 * the referrer.
 */
export function loadPage(url, { build, navigator = {}, referrer = '', localItems = {}, scrollHeight = 5000 } = {}) {
  const page = { url, beacons: [], fetches: [], timers: [], observers: [] };
  const u = new URL(url);
  const location = {
    href: u.href, protocol: u.protocol, host: u.host, hostname: u.hostname, pathname: u.pathname,
    search: u.search, hash: u.hash, origin: u.origin,
  };
  const storage = (init) => {
    const m = new Map(Object.entries(init));
    return {
      getItem: (k) => (m.has(k) ? m.get(k) : null),
      setItem: (k, v) => m.set(k, String(v)),
      removeItem: (k) => m.delete(k),
      map: m,
    };
  };
  const document = new Target();
  const window = new Target();
  page.window = window;
  page.document = document;
  document.documentElement = new El(page, 'html');
  document.documentElement.scrollHeight = scrollHeight;
  document.body = document.documentElement.append(new El(page, 'body'));
  document.referrer = referrer;
  document.visibilityState = 'visible';
  document.activeElement = document.body;
  document.querySelector = (s) => document.documentElement.querySelector(s);
  document.querySelectorAll = (s) => document.documentElement.querySelectorAll(s);
  document.getSelection = () => page.selection ?? null;
  if (build) {
    build(page, (tag, attrs, ...children) => new El(page, tag, attrs, children), document.body);
  }

  let now = 1000;
  class Blob {
    constructor(parts) {
      this.text = parts.join('');
    }
  }
  class IntersectionObserver {
    constructor(cb, opts) {
      this.cb = cb;
      this.opts = opts;
      this.targets = new Set();
      page.observers.push(this);
    }

    observe(el) {
      this.targets.add(el);
    }

    unobserve(el) {
      this.targets.delete(el);
    }
  }
  Object.assign(window, {
    window,
    document,
    location,
    navigator: {
      language: 'en-US',
      maxTouchPoints: 0,
      webdriver: false,
      doNotTrack: null,
      sendBeacon: (endpoint, blob) => {
        page.beacons.push({ endpoint, body: JSON.parse(blob.text) });
        return true;
      },
      ...navigator,
    },
    screen: { width: 1920, height: 1080 },
    localStorage: storage(localItems),
    sessionStorage: storage({}),
    performance: { now: () => now, getEntriesByType: () => [] },
    crypto: { getRandomValues: (a) => a.fill(7) },
    fetch: (...args) => {
      page.fetches.push(args);
      return Promise.resolve();
    },
    URL,
    URLSearchParams,
    Blob,
    IntersectionObserver,
    scrollY: 0,
    innerHeight: 800,
    setTimeout: (fn, ms = 0) => {
      page.timers.push({ fn, at: now + ms });
      return page.timers.length;
    },
    clearTimeout: (id) => {
      if (id && page.timers[id - 1]) {
        page.timers[id - 1].fn = null;
      }
    },
    Date: { now: () => now },
    Object,
    String,
    Math,
    JSON,
    Uint8Array,
  });
  /** Advance the clock by `ms`, running timers that come due. */
  page.advance = (ms) => {
    now += ms;
    for (const t of page.timers) {
      if (t.fn && t.at <= now) {
        const fn = t.fn;
        t.fn = null;
        fn();
      }
    }
  };
  page.click = (el) => document.fire('click', { target: el });
  page.key = (el, key) => document.fire('keydown', { target: el, key });
  page.type = (input, value) => {
    input.value = value;
    document.fire('input', { target: input });
  };
  page.blur = (el, next = document.body) => {
    document.activeElement = next;
    document.fire('focusout', { target: el });
  };
  page.hide = () => {
    document.visibilityState = 'hidden';
    document.fire('visibilitychange');
    window.fire('pagehide');
  };
  page.scrollTo = (y) => {
    window.scrollY = y;
    window.fire('scroll');
    page.advance(300);
  };
  /** Report `el` as intersecting to every observer watching it. */
  page.intersect = (el) => {
    for (const o of page.observers) {
      if (o.targets.has(el)) {
        o.cb([{ target: el, isIntersecting: true }]);
      }
    }
  };
  /** Every event sent so far, flattened across beacons. */
  page.events = () => page.beacons.flatMap((b) => b.body.e);
  page.named = (n) => page.events().filter((e) => e.n === n);

  vm.createContext(window);
  vm.runInContext(CLIENT, window, { filename: 'phoenix_analytics.js' });
  return page;
}
