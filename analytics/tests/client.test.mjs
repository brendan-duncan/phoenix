// The client script (analytics/phoenix_analytics.js) in a fake page: where it
// sends, what it skips, and the events the guide's links, search box and
// headings produce.
import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { loadPage } from './fake_page.mjs';

const SITE = 'https://phoenixanimate.com';

/** A guide page the way site.js leaves it: header with search and theme button, sidebar, article, footer. */
function guide(page, el, body) {
  const header = body.append(el('header', { class: 'site-header' },
    el('div', { class: 'search' }, el('input', { type: 'search' }), el('div', { class: 'search-results' })),
    el('button', { class: 'icon-button theme-button' }),
    el('a', { class: 'icon-button github-link', href: 'https://github.com/brendan-duncan/phoenix' })));
  page.input = header.querySelector('input');
  page.results = header.querySelector('.search-results');
  page.results.hidden = true;
  page.themeButton = header.querySelector('.theme-button');
  body.append(el('nav', { class: 'sidebar' }, el('a', { href: 'timeline.html' })));
  const layout = body.append(el('div', { class: 'layout' }));
  const main = layout.append(el('main'));
  const content = main.append(el('article', { class: 'content' }));
  content.rect = { top: 100, bottom: 4100 };
  page.content = content;
  page.links = {
    setup: content.append(el('a', { href: 'https://phoenixanimate.com/download/Phoenix-win-x64-Setup.exe' })),
    pkg: content.append(el('a', { href: 'https://phoenixanimate.com/download/Phoenix-osx-arm64-Setup.pkg' })),
    macZip: content.append(el('a', { href: 'https://phoenixanimate.com/download/Phoenix-osx-arm64-Portable.zip' })),
    winZip: content.append(el('a', { href: 'https://phoenixanimate.com/download/Phoenix-win-x64-Portable.zip' })),
    appImage: content.append(el('a', { href: 'https://phoenixanimate.com/download/Phoenix-linux-x64.AppImage' })),
    lesson: content.append(el('a', { href: 'assets/tutorials/crowd.fla' })),
    releases: content.append(el('a', { href: 'https://github.com/brendan-duncan/phoenix/releases' })),
    latest: content.append(el('a', { href: 'https://github.com/brendan-duncan/phoenix/releases/latest' })),
    ghAsset: content.append(el('a', { href: 'https://github.com/brendan-duncan/phoenix/releases/latest/download/Phoenix-win-x64-Setup.exe' })),
    internal: content.append(el('a', { href: 'timeline.html#layers' })),
  };
  page.links.code = content.append(el('code', {}, page.links.codeLink = el('a', { href: 'https://ffmpeg.org/' })));
  page.headings = [
    content.append(el('h2', { id: 'install' })),
    content.append(el('h3', { id: 'windows' })),
    content.append(el('h2', { id: 'update' })),
  ];
  content.append(el('section', { class: 'card' }, el('h2', { id: 'card-heading' })));
  const pager = content.append(el('nav', { class: 'pager' }, el('a', { href: 'viewer.html' })));
  pager.rect = { top: 3900, bottom: 4000 };
  const footer = content.append(el('footer', { class: 'site-footer' }));
  page.links.issues = footer.append(el('a', { href: 'https://github.com/brendan-duncan/phoenix/issues' }));
  page.links.mail = footer.append(el('a', { href: 'mailto:someone@example.com' }));
  page.links.linkedin = footer.append(el('a', { href: 'https://www.linkedin.com/in/someone' }));
  page.links.mastodon = footer.append(el('a', { href: 'https://mastodon.gamedev.place/@someone' }));
  page.links.privacy = footer.append(el('a', { href: 'privacy.html' }));
}

function open(path = '/get-started.html', opts = {}) {
  return loadPage(path.startsWith('http') || path.startsWith('file:') ? path : `${SITE}${path}`, { build: guide, ...opts });
}

function sent(page) {
  page.hide();
  return page.events();
}

describe('where it sends', () => {
  it('sends a page view from phoenixanimate.com and www.phoenixanimate.com', () => {
    for (const host of ['https://phoenixanimate.com', 'https://www.phoenixanimate.com']) {
      const page = open(`${host}/get-started.html`);
      const events = sent(page);
      assert.equal(page.beacons[0].endpoint, '/a/collect');
      assert.equal(page.beacons[0].body.h, new URL(host).hostname);
      assert.equal(events[0].n, 'pageview');
      assert.equal(events[0].u, '/get-started.html');
    }
  });

  it('sends nothing from file://, localhost or any other host', () => {
    for (const url of [
      'file:///D:/src/phoenix/website/get-started.html',
      'http://localhost:8000/get-started.html',
      'http://127.0.0.1:5500/get-started.html',
      'https://brendan-duncan.github.io/phoenix/get-started.html',
      'https://phoenixanimate.com.example.net/get-started.html',
      'https://taosengine.com/get-started.html',
    ]) {
      const page = open(url);
      page.click(page.links.setup);
      page.type(page.input, 'onion');
      page.advance(10000);
      page.hide();
      assert.equal(page.beacons.length, 0, url);
      assert.equal(page.fetches.length, 0, url);
      // track() is there for page code, but does nothing.
      page.window.phoenixAnalytics.track('custom', { a: 1 });
      page.hide();
      assert.equal(page.beacons.length, 0, url);
    }
  });

  it('honors Do Not Track, Global Privacy Control and automation', () => {
    for (const navigator of [{ doNotTrack: '1' }, { globalPrivacyControl: true }, { webdriver: true }]) {
      const page = open('/', { navigator });
      assert.equal(sent(page).length, 0, JSON.stringify(navigator));
    }
  });

  it('remembers ?notrack and undoes it with ?track', () => {
    const off = open('/?notrack');
    assert.equal(sent(off).length, 0);
    assert.equal(off.window.localStorage.getItem('phoenix.analytics.off'), '1');
    const stillOff = open('/', { localItems: { 'phoenix.analytics.off': '1' } });
    assert.equal(sent(stillOff).length, 0);
    const back = open('/?track', { localItems: { 'phoenix.analytics.off': '1' } });
    assert.ok(sent(back).length > 0);
    assert.equal(back.window.localStorage.getItem('phoenix.analytics.off'), null);
  });

  it('keeps a session id in sessionStorage and sets no cookie', () => {
    const page = open('/');
    sent(page);
    const sid = page.beacons[0].body.sid;
    assert.match(sid, /^[a-z0-9]{12}$/);
    assert.equal(page.window.sessionStorage.getItem('phoenix.sid'), sid);
    assert.equal(page.document.cookie, undefined);
  });

  it('falls back to fetch when sendBeacon refuses', () => {
    const page = open('/', { navigator: { sendBeacon: () => false } });
    page.hide();
    assert.equal(page.fetches.length, 1);
    assert.equal(page.fetches[0][0], '/a/collect');
    assert.equal(page.fetches[0][1].credentials, 'omit');
  });
});

describe('links', () => {
  it('records downloads with the file, its platform and kind', () => {
    const page = open();
    const l = page.links;
    for (const a of [l.setup, l.pkg, l.macZip, l.winZip, l.appImage, l.ghAsset]) {
      page.click(a);
    }
    const downloads = sent(page).filter((e) => e.n === 'download');
    assert.deepEqual(downloads.map((e) => e.d), [
      { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', package: 'installer', via: 'site', where: 'page' },
      { file: 'Phoenix-osx-arm64-Setup.pkg', os: 'macos', package: 'installer', via: 'site', where: 'page' },
      { file: 'Phoenix-osx-arm64-Portable.zip', os: 'macos', package: 'portable', via: 'site', where: 'page' },
      { file: 'Phoenix-win-x64-Portable.zip', os: 'windows', package: 'portable', via: 'site', where: 'page' },
      { file: 'Phoenix-linux-x64.AppImage', os: 'linux', package: 'appimage', via: 'site', where: 'page' },
      { file: 'Phoenix-win-x64-Setup.exe', os: 'windows', package: 'installer', via: 'github', where: 'page' },
    ]);
    // The page is the event's own URL.
    assert.ok(downloads.every((e) => e.u === '/get-started.html'));
  });

  it('records lesson files separately', () => {
    const page = open('/tutorial-crowd.html');
    page.click(page.links.lesson);
    assert.deepEqual(sent(page).filter((e) => e.n === 'lesson-download').map((e) => e.d), [{ file: 'crowd.fla', where: 'page' }]);
  });

  it('records outbound links by target, and not internal ones', () => {
    const page = open();
    const l = page.links;
    for (const a of [l.releases, l.latest, l.issues, l.mail, l.linkedin, l.mastodon, l.codeLink, l.internal, l.privacy]) {
      page.click(a);
    }
    page.click(page.document.querySelector('.github-link'));
    const out = sent(page).filter((e) => e.n === 'outbound').map((e) => [e.d.target, e.d.where]);
    assert.deepEqual(out, [
      ['github-releases', 'page'],
      ['github-releases', 'page'],
      ['github-issues', 'footer'],
      ['mail', 'footer'],
      ['linkedin', 'footer'],
      ['mastodon', 'footer'],
      ['other', 'page'],
      ['github', 'header'],
    ]);
    const mail = page.named('outbound').find((e) => e.d.target === 'mail');
    assert.ok(!JSON.stringify(mail).includes('example.com'), 'no mail address');
  });

  it('classifies links without a page', () => {
    const { classifyLink, pageKind } = open().window.phoenixAnalytics;
    assert.equal(classifyLink('https://phoenixanimate.com/timeline.html', 'phoenixanimate.com'), null);
    assert.equal(classifyLink('javascript:void(0)', 'phoenixanimate.com'), null);
    assert.equal(classifyLink('https://www.phoenixanimate.com/download/Phoenix-linux-x64.AppImage', 'phoenixanimate.com').name, 'download');
    assert.equal(pageKind('/'), 'home');
    assert.equal(pageKind('/index.html'), 'home');
    assert.equal(pageKind('/get-started.html'), 'install');
    assert.equal(pageKind('/tutorials.html'), 'tutorials');
    assert.equal(pageKind('/first-animation.html'), 'tutorial');
    assert.equal(pageKind('/tutorial-masks.html'), 'tutorial');
    assert.equal(pageKind('/privacy.html'), 'about');
    assert.equal(pageKind('/timeline.html'), 'guide');
  });
});

describe('search box', () => {
  function showResults(page, n) {
    page.results.children = [];
    for (let i = 0; i < n; i++) {
      const a = page.results.append(new page.content.constructor(page, 'a', { href: `timeline.html#r${i}`, class: i === 0 ? 'active' : '' }));
      a.parent = page.results;
    }
    page.results.hidden = false;
  }

  it('records a chosen result without the query text', () => {
    const page = open();
    page.type(page.input, 'onion');
    page.type(page.input, 'onion skin');
    showResults(page, 5);
    page.advance(500);
    page.blur(page.input);
    page.click(page.results.children[2]);
    page.advance(500);
    const searches = sent(page).filter((e) => e.n === 'search');
    assert.deepEqual(searches.map((e) => e.d), [{ len: 10, words: 2, results: 5, chosen: true, rank: 3 }]);
    assert.ok(!JSON.stringify(page.beacons).includes('onion'), 'query text never leaves the page');
  });

  it('records Enter on the highlighted result', () => {
    const page = open();
    page.type(page.input, 'mask');
    showResults(page, 3);
    page.key(page.input, 'Enter');
    assert.deepEqual(sent(page).filter((e) => e.n === 'search').map((e) => e.d), [{ len: 4, words: 1, results: 3, chosen: true, rank: 1 }]);
  });

  it('records an abandoned search, and one that found nothing', () => {
    const page = open();
    page.type(page.input, 'zzzz');
    page.results.hidden = false;
    page.advance(500);
    page.blur(page.input);
    page.advance(500);
    page.type(page.input, 'bones');
    page.key(page.input, 'Escape');
    page.type(page.input, 'x');
    page.type(page.input, '');
    const searches = sent(page).filter((e) => e.n === 'search').map((e) => e.d);
    assert.deepEqual(searches, [
      { len: 4, words: 1, results: 0, chosen: false },
      { len: 5, words: 1, results: -1, chosen: false },
      { len: 1, words: 1, results: -1, chosen: false },
    ]);
    assert.ok(!JSON.stringify(page.beacons).includes('zzzz'));
  });
});

describe('reading', () => {
  it('reports scroll depth through the article, and reading it through', () => {
    const page = open('/timeline.html');
    page.scrollTo(1000);
    page.advance(31000);
    page.scrollTo(3200);
    const events = sent(page);
    // Article runs from 100 to the pager at 3900; the window is 800 tall.
    assert.deepEqual(events.filter((e) => e.n === 'read-depth').map((e) => e.d.depth), [25, 50, 75, 100]);
    assert.equal(events.filter((e) => e.n === 'read-complete').length, 1);
    const time = events.find((e) => e.n === 'time-on-page');
    assert.equal(time.d.depth, 100);
    assert.ok(time.d.seconds >= 31);
  });

  it('reports headings reached once each, skipping cards', () => {
    const page = open('/timeline.html');
    assert.equal(page.observers.length, 1);
    assert.deepEqual([...page.observers[0].targets].map((h) => h.id), ['install', 'windows', 'update']);
    page.intersect(page.headings[0]);
    page.intersect(page.headings[1]);
    page.intersect(page.headings[0]);
    const events = sent(page);
    assert.deepEqual(events.filter((e) => e.n === 'section').map((e) => e.d), [
      { id: 'install', n: 1, of: 3, level: 2 },
      { id: 'windows', n: 2, of: 3, level: 3 },
    ]);
    const time = events.find((e) => e.n === 'time-on-page');
    assert.equal(time.d.sections, 2);
    assert.equal(time.d.of, 3);
  });

  it('records tutorial pages opened and where from', () => {
    const fromIndex = open('/tutorial-masks.html', { referrer: `${SITE}/tutorials.html` });
    assert.deepEqual(sent(fromIndex).find((e) => e.n === 'tutorial-open').d, { tutorial: 'tutorial-masks', from: '/tutorials' });
    const external = open('/first-animation.html', { referrer: 'https://www.google.com/' });
    assert.deepEqual(sent(external).find((e) => e.n === 'tutorial-open').d, { tutorial: 'first-animation', from: 'external' });
    const direct = open('/tutorial-crowd.html');
    assert.equal(sent(direct).find((e) => e.n === 'tutorial-open').d.from, 'direct');
    assert.equal(sent(open('/timeline.html')).filter((e) => e.n === 'tutorial-open').length, 0);
  });

  it('records the theme toggle and code copies', () => {
    const page = open();
    page.click(page.themeButton);
    page.document.documentElement.setAttribute('data-theme', 'dark');
    page.advance(1);
    page.selection = { anchorNode: page.links.codeLink };
    page.document.fire('copy');
    const events = sent(page);
    assert.deepEqual(events.find((e) => e.n === 'theme').d, { to: 'dark' });
    assert.equal(events.filter((e) => e.n === 'code-copy').length, 1);
  });
});

describe('batching', () => {
  it('sends in batches of at most 20 and after 5 seconds', () => {
    const page = open();
    for (let i = 0; i < 25; i++) {
      page.window.phoenixAnalytics.track('custom', { i });
    }
    assert.equal(page.beacons.length, 1);
    assert.equal(page.beacons[0].body.e.length, 20);
    page.advance(5000);
    assert.equal(page.beacons.length, 2);
    assert.equal(page.events().length, 26);
  });
});
