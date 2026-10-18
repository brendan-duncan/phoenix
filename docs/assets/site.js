// Phoenix user guide: builds the header, sidebar, page contents, search and
// page links around each page's <main>, so every page only carries its text.

// The Phoenix release this guide describes. CMake warns when it falls
// behind the project version, so update it with each release.
const VERSION = "0.19.0";

const NAV = [
  { group: "Get started", pages: [
    { href: "index.html", title: "Welcome to Phoenix" },
    { href: "get-started.html", title: "Install and update" },
    { href: "viewer.html", title: "Phoenix Viewer and Open with" },
    { href: "tutorials.html", title: "Interactive tutorials" },
  ] },
  { group: "Tutorials", pages: [
    { href: "first-animation.html", title: "Your first animation" },
    { href: "tutorial-merge-and-object-drawing.html", title: "Merge and object drawing" },
    { href: "tutorial-frame-by-frame.html", title: "Frame-by-frame and onion skinning" },
    { href: "tutorial-symbols-in-place.html", title: "Symbols and editing in place" },
    { href: "tutorial-shape-tween.html", title: "Shape tweens" },
    { href: "tutorial-motion-tween.html", title: "Motion tweens and the Motion Editor" },
    { href: "tutorial-masks.html", title: "Masks" },
    { href: "tutorial-layer-parenting.html", title: "Rigging with layer parenting" },
    { href: "tutorial-behaviors.html", title: "Motion with behaviors" },
    { href: "tutorial-own-behavior.html", title: "Write your own behavior" },
    { href: "tutorial-crowd.html", title: "A crowd from one cycle" },
    { href: "tutorial-draw-on.html", title: "Draw-on lines" },
    { href: "tutorial-animated-title.html", title: "An animated title" },
    { href: "tutorial-particles.html", title: "Particles" },
    { href: "tutorial-matte-reveal.html", title: "Track mattes and adjustment layers" },
    { href: "tutorial-lower-thirds.html", title: "Lower thirds from a template" },
    { href: "tutorial-bouncy-antenna.html", title: "A bouncy antenna" },
    { href: "tutorial-motion-blur-echo.html", title: "Motion blur, echo and time" },
    { href: "tutorial-glow-and-wipes.html", title: "Glow and wipes" },
    { href: "tutorial-dashes-and-text-path.html", title: "Dashes and text on a path" },
    { href: "tutorial-sound-visualizer.html", title: "A sound visualizer" },
    { href: "tutorial-controls-null.html", title: "Controls on a null" },
    { href: "tutorial-3d-cards.html", title: "3D cards in the light" },
    { href: "tutorial-motion-sketch.html", title: "Motion Sketch" },
    { href: "tutorial-template-fit.html", title: "A template that fits" },
    { href: "tutorial-node-graph.html", title: "Glow through a node graph" },
    { href: "tutorial-instance-effects.html", title: "Effects on one instance" },
    { href: "tutorial-lip-sync.html", title: "Lip sync" },
    { href: "tutorial-head-turn.html", title: "A head turn on a controller" },
    { href: "tutorial-multiplane-camera.html", title: "Multiplane camera" },
    { href: "tutorial-loop-cycle.html", title: "A cycle with Loop" },
    { href: "tutorial-linked-palettes.html", title: "Linked palettes" },
    { href: "tutorial-linked-masters.html", title: "Linked master symbols" },
    { href: "tutorial-x-sheet.html", title: "Timing with the X-sheet" },
  ] },
  { group: "Workspace", pages: [
    { href: "workspace.html", title: "The workspace" },
    { href: "stage.html", title: "Stage, zoom and view" },
    { href: "documents.html", title: "Documents and files" },
    { href: "import.html", title: "Importing artwork" },
  ] },
  { group: "Drawing", pages: [
    { href: "drawing-models.html", title: "Merge and object drawing" },
    { href: "drawing-tools.html", title: "Drawing tools" },
    { href: "selecting.html", title: "Selecting and transforming" },
    { href: "3d.html", title: "3D rotation and translation" },
    { href: "color.html", title: "Color, fills and strokes" },
    { href: "ink-and-paint.html", title: "Ink, paint and palettes" },
    { href: "text.html", title: "Text" },
  ] },
  { group: "Symbols", pages: [
    { href: "symbols.html", title: "Symbols and instances" },
    { href: "library.html", title: "The library" },
    { href: "effects.html", title: "Filters, blends and color effects" },
  ] },
  { group: "Animation", pages: [
    { href: "timeline.html", title: "Timeline and layers" },
    { href: "frames.html", title: "Frames and keyframes" },
    { href: "tweens.html", title: "Tweens and easing" },
    { href: "masks-guides.html", title: "Masks and guides" },
    { href: "bones.html", title: "Bones and inverse kinematics" },
    { href: "rigging.html", title: "Rigs and motion" },
    { href: "motion-graphics.html", title: "Motion graphics" },
    { href: "behaviors.html", title: "Behaviors" },
    { href: "node-graph.html", title: "Node graphs" },
    { href: "sound.html", title: "Sound" },
    { href: "video.html", title: "Video" },
    { href: "actionscript.html", title: "ActionScript and Test Movie" },
  ] },
  { group: "Output", pages: [
    { href: "export.html", title: "Exporting and Publishing" },
    { href: "command-line.html", title: "Command line" },
  ] },
  { group: "Studio pipeline", pages: [
    { href: "howto-production.html", title: "How to run an episode" },
    { href: "pipeline.html", title: "Shots, assets and review" },
    { href: "version-control.html", title: "Version control" },
    { href: "scripting.html", title: "Scripting with JSFL and Python" },
    { href: "ai-assistants.html", title: "AI assistants" },
  ] },
  { group: "Reference", pages: [
    { href: "shortcuts.html", title: "Keyboard shortcuts" },
    { href: "preferences.html", title: "Settings and preferences" },
    { href: "animate.html", title: "Coming from Animate" },
    { href: "harmony.html", title: "Coming from Harmony" },
    { href: "changelog.html", title: "What's new" },
  ] },
  { group: "Developers", pages: [
    { href: "phx-format.html", title: "The Phoenix document format" },
    { href: "fla-format.html", title: "The FLA and XFL format" },
    { href: "binary-fla-format.html", title: "The binary FLA format" },
  ] },
];

const ICONS = {
  mail: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="m3 7 9 6 9-6"/></svg>',
  mastodon: '<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M21.3 13.9c-.3 1.5-2.6 3.2-5.3 3.5-1.4.2-2.8.3-4.2.3-2.4-.1-4.3-.6-4.3-.6v.7c.3 2.4 2.4 2.6 4.3 2.6 2 .1 3.8-.5 3.8-.5l.1 1.8s-1.4.7-3.9.9c-1.4.1-3.1 0-5.1-.6C2.4 21.3 1.6 16.5 1.5 11.6V7.7C1.5 2.9 4.7 1.4 4.7 1.4 6.3.7 9.1.4 12 .4h.1c2.9 0 5.7.3 7.3 1 0 0 3.2 1.5 3.2 6.3 0 0 0 3.5-1.3 6.2zM17.9 8.1c0-1.2-.3-2.1-.9-2.8-.6-.7-1.5-1-2.5-1-1.2 0-2.1.4-2.6 1.4L11.3 6.6l-.6-.9c-.6-1-1.4-1.4-2.6-1.4-1 0-1.9.3-2.5 1-.6.7-.9 1.6-.9 2.8V14h2.3V8.3c0-1.2.5-1.8 1.5-1.8 1.1 0 1.7.7 1.7 2.2v3.1H12V8.7c0-1.5.6-2.2 1.7-2.2 1 0 1.5.6 1.5 1.8V14h2.3z"/></svg>',
  search: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>',
  menu: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 6h16M4 12h16M4 18h16"/></svg>',
  sun: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
  moon: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>',
  github: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 .5a11.5 11.5 0 0 0-3.6 22.4c.6.1.8-.3.8-.6v-2c-3.2.7-3.9-1.5-3.9-1.5-.5-1.3-1.3-1.7-1.3-1.7-1-.7.1-.7.1-.7 1.2.1 1.8 1.2 1.8 1.2 1 1.8 2.7 1.3 3.4 1 .1-.8.4-1.3.7-1.6-2.6-.3-5.3-1.3-5.3-5.7 0-1.3.5-2.3 1.2-3.1-.1-.3-.5-1.5.1-3.1 0 0 1-.3 3.2 1.2a11 11 0 0 1 5.8 0c2.2-1.5 3.2-1.2 3.2-1.2.6 1.6.2 2.8.1 3.1.8.8 1.2 1.8 1.2 3.1 0 4.4-2.7 5.4-5.3 5.7.4.4.8 1.1.8 2.2v3.3c0 .3.2.7.8.6A11.5 11.5 0 0 0 12 .5z"/></svg>',
};

const REPO = "https://github.com/brendan-duncan/phoenix";
const CONTACT = {
  email: "brendan.duncan@gmail.com",
  mastodon: "https://mastodon.gamedev.place/@brendan",
};

function storageGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function storageSet(key, value) { try { localStorage.setItem(key, value); } catch { /* private window */ } }

function currentPage() {
  const name = location.pathname.split("/").pop() || "index.html";
  return name.endsWith(".html") ? name : "index.html";
}

function applyTheme(theme) {
  if (theme) document.documentElement.setAttribute("data-theme", theme);
  else document.documentElement.removeAttribute("data-theme");
}

function isDark() {
  const set = document.documentElement.getAttribute("data-theme");
  if (set) return set === "dark";
  return matchMedia("(prefers-color-scheme: dark)").matches;
}

applyTheme(storageGet("phoenix-docs-theme"));

function slug(text) {
  return text.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
}

function buildHeader() {
  const header = document.createElement("header");
  header.className = "site-header";
  header.innerHTML = `
    <button class="icon-button menu-button" aria-label="Open navigation">${ICONS.menu}</button>
    <a class="brand" href="index.html"><img src="assets/img/logo.png" alt=""> Phoenix <span>Animation ${VERSION}</span></a>
    <div class="header-spacer"></div>
    <div class="search">
      ${ICONS.search}
      <input type="search" placeholder="Search the guide" aria-label="Search the guide" autocomplete="off">
      <div class="search-results" hidden></div>
    </div>
    <button class="icon-button theme-button" aria-label="Toggle dark mode"></button>
    <a class="icon-button github-link" href="${REPO}" aria-label="Phoenix on GitHub">${ICONS.github}</a>`;
  document.body.prepend(header);

  const themeButton = header.querySelector(".theme-button");
  const paintThemeButton = () => { themeButton.innerHTML = isDark() ? ICONS.sun : ICONS.moon; };
  paintThemeButton();
  themeButton.addEventListener("click", () => {
    const next = isDark() ? "light" : "dark";
    applyTheme(next);
    storageSet("phoenix-docs-theme", next);
    paintThemeButton();
  });

  header.querySelector(".menu-button").addEventListener("click", () => document.body.classList.toggle("nav-open"));
}

function buildSidebar(layout, page) {
  const nav = document.createElement("nav");
  nav.className = "sidebar";
  nav.setAttribute("aria-label", "Guide");
  nav.innerHTML = NAV.map(group => `
    <h4>${group.group}</h4>
    <ul>${group.pages.map(p => `<li><a href="${p.href}"${p.href === page ? ' aria-current="page"' : ""}>${p.title}</a></li>`).join("")}</ul>`).join("");
  layout.prepend(nav);
  nav.addEventListener("click", e => { if (e.target.closest("a")) document.body.classList.remove("nav-open"); });
  const active = nav.querySelector('[aria-current="page"]');
  if (active) active.scrollIntoView({ block: "center" });
}

function buildToc(layout, content) {
  const headings = [...content.querySelectorAll("h2, h3")].filter(h => !h.closest(".card, .pager"));
  const labels = new Map();
  for (const h of headings) {
    labels.set(h, h.textContent.trim().replace(/&/g, "&amp;").replace(/</g, "&lt;"));
    if (!h.id) h.id = slug(h.textContent);
    const a = document.createElement("a");
    a.className = "anchor";
    a.href = `#${h.id}`;
    a.textContent = "#";
    a.setAttribute("aria-hidden", "true");
    h.append(a);
  }

  const toc = document.createElement("aside");
  toc.className = "toc";
  if (headings.length > 1) {
    toc.innerHTML = `<h4>On this page</h4><ul>${headings.map(h =>
      `<li><a href="#${h.id}" class="${h.tagName === "H3" ? "sub" : ""}">${labels.get(h)}</a></li>`).join("")}</ul>`;
  }
  layout.append(toc);

  const links = [...toc.querySelectorAll("a")];
  if (!links.length) return;
  const observer = new IntersectionObserver(entries => {
    for (const entry of entries) {
      if (!entry.isIntersecting) continue;
      links.forEach(l => l.classList.toggle("active", l.getAttribute("href") === `#${entry.target.id}`));
    }
  }, { rootMargin: "-70px 0px -70% 0px" });
  headings.forEach(h => observer.observe(h));
}

function buildPager(content, page) {
  const pages = NAV.flatMap(g => g.pages);
  const i = pages.findIndex(p => p.href === page);
  if (i < 0) return;
  const prev = pages[i - 1], next = pages[i + 1];
  const pager = document.createElement("nav");
  pager.className = "pager";
  pager.setAttribute("aria-label", "Previous and next page");
  pager.innerHTML =
    (prev ? `<a class="prev" href="${prev.href}"><small>Previous</small>${prev.title}</a>` : "") +
    (next ? `<a class="next" href="${next.href}"><small>Next</small>${next.title}</a>` : "");
  content.append(pager);
}

// The footer every page ends with: the version, where to report a mistake,
// the copyright, the privacy statement and how to reach the author.
function buildFooter(content) {
  const footer = document.createElement("footer");
  footer.className = "site-footer";
  footer.innerHTML =
    `<p>Phoenix ${VERSION} (<a href="changelog.html">what's new</a>). Found a mistake in this guide? <a href="${REPO}/issues">Open an issue</a>.</p>` +
    `<p class="contact">` +
    `<a href="mailto:${CONTACT.email}">${ICONS.mail}<span>${CONTACT.email}</span></a>` +
    //`<a href="${CONTACT.mastodon}" rel="me">${ICONS.mastodon}<span>Mastodon</span></a>` +
    `</p>` +
    `<p class="copyright">Copyright &copy; 2026 Brendan Duncan. All rights reserved. <a href="privacy.html">Privacy</a></p>`;
  content.append(footer);
}

// Search: every page's headings, loaded once from the pages themselves.
let searchIndex = null;

async function loadSearchIndex() {
  if (searchIndex) return searchIndex;
  const pages = NAV.flatMap(g => g.pages);
  const entries = [];
  await Promise.all(pages.map(async p => {
    entries.push({ title: p.title, page: p.title, href: p.href, text: p.title.toLowerCase() });
    try {
      const html = await (await fetch(p.href)).text();
      const doc = new DOMParser().parseFromString(html, "text/html");
      const main = doc.querySelector("main");
      if (!main) return;
      const keywords = (doc.querySelector('meta[name="keywords"]')?.content || "").toLowerCase();
      entries[entries.length - 1].text += " " + keywords;
      for (const h of main.querySelectorAll("h2, h3")) {
        if (h.closest(".card")) continue;
        const id = h.id || slug(h.textContent);
        let body = "";
        for (let el = h.nextElementSibling; el && !/^H[23]$/.test(el.tagName); el = el.nextElementSibling) body += " " + el.textContent;
        entries.push({ title: h.textContent.trim(), page: p.title, href: `${p.href}#${id}`, text: (h.textContent + " " + body).toLowerCase() });
      }
    } catch { /* opened from disk: titles only */ }
  }));
  searchIndex = entries;
  return entries;
}

function setupSearch() {
  const input = document.querySelector(".search input");
  const results = document.querySelector(".search-results");
  let selected = 0;

  const render = async () => {
    const q = input.value.trim().toLowerCase();
    if (!q) { results.hidden = true; return; }
    const words = q.split(/\s+/);
    const index = await loadSearchIndex();
    const scored = index
      .filter(e => words.every(w => e.text.includes(w)))
      .map(e => ({ e, score: words.reduce((s, w) => s + (e.title.toLowerCase().includes(w) ? 10 : 1), 0) }))
      .sort((a, b) => b.score - a.score)
      .slice(0, 12);
    selected = 0;
    results.innerHTML = scored.length
      ? scored.map((s, i) => `<a href="${s.e.href}" class="${i === 0 ? "active" : ""}">${s.e.title}${s.e.title !== s.e.page ? `<small>${s.e.page}</small>` : ""}</a>`).join("")
      : '<div class="empty">Nothing found</div>';
    results.hidden = false;
  };

  input.addEventListener("input", render);
  input.addEventListener("focus", () => { if (input.value) render(); });
  input.addEventListener("keydown", e => {
    const items = [...results.querySelectorAll("a")];
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      if (!items.length) return;
      selected = (selected + (e.key === "ArrowDown" ? 1 : items.length - 1)) % items.length;
      items.forEach((a, i) => a.classList.toggle("active", i === selected));
    } else if (e.key === "Enter" && items[selected]) {
      location.href = items[selected].href;
    } else if (e.key === "Escape") {
      results.hidden = true;
      input.blur();
    }
  });
  document.addEventListener("click", e => { if (!e.target.closest(".search")) results.hidden = true; });
  document.addEventListener("keydown", e => {
    if (e.key === "/" && document.activeElement !== input) { e.preventDefault(); input.focus(); }
  });
}

function setupLightbox(content) {
  content.addEventListener("click", e => {
    const img = e.target.closest("figure img");
    if (!img) return;
    const box = document.createElement("div");
    box.className = "lightbox";
    box.innerHTML = `<img src="${img.currentSrc || img.src}" alt="${img.alt}">`;
    const close = () => { box.remove(); document.removeEventListener("keydown", onKey); };
    const onKey = ev => { if (ev.key === "Escape") close(); };
    box.addEventListener("click", close);
    document.addEventListener("keydown", onKey);
    document.body.append(box);
  });
}

// Site analytics (analytics/README.md): loaded only on phoenixanimate.com,
// so the guide opened from disk or a local server never sends anything.
// tools/deploy_site.py publishes the script as assets/analytics.js.
const ANALYTICS_HOSTS = ["phoenixanimate.com", "www.phoenixanimate.com"];

function loadAnalytics() {
  if (!ANALYTICS_HOSTS.includes(location.hostname)) return;
  const script = document.createElement("script");
  script.src = "assets/analytics.js";
  script.async = true;
  document.head.append(script);
}

function wrapTables(content) {
  for (const table of content.querySelectorAll("table")) {
    if (table.parentElement.classList.contains("table-wrap")) continue;
    const wrap = document.createElement("div");
    wrap.className = "table-wrap";
    table.replaceWith(wrap);
    wrap.append(table);
    // On a phone a table too wide for the screen stacks its rows (site.css);
    // with three or more columns the cells after the first carry their
    // column's name.
    const heads = [...table.querySelectorAll("thead th")].map(th => th.textContent.trim());
    // Pairs of columns side by side (Key, Tool, Key, Tool) stack a pair to
    // a line.
    if (heads.length === 4 && heads[0] === heads[2] && heads[1] === heads[3]) {
      table.classList.add("pairs");
    } else if (heads.length >= 3) {
      table.classList.add("labeled");
      for (const row of table.querySelectorAll("tbody tr")) {
        [...row.cells].forEach((cell, i) => { if (heads[i]) cell.dataset.label = heads[i]; });
      }
    }
  }
  fitTables(content);
  // Measured again once the fonts are in, which can widen a table.
  document.fonts?.ready.then(() => fitTables(content));
  let pending = false;
  addEventListener("resize", () => {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; fitTables(content); });
  });
}

/// Stacks, on a phone, the tables that would not fit across it.
function fitTables(content) {
  const phone = matchMedia("(max-width: 640px)").matches;
  for (const table of content.querySelectorAll(".table-wrap > table")) {
    table.classList.remove("stacked");
    if (phone && table.scrollWidth > table.parentElement.clientWidth + 1)
      table.classList.add("stacked");
  }
}

document.addEventListener("DOMContentLoaded", () => {
  const page = currentPage();
  const main = document.querySelector("main");
  const content = main.querySelector(".content") || main;
  const layout = document.createElement("div");
  layout.className = "layout";
  main.replaceWith(layout);
  layout.append(main);

  buildHeader();
  buildSidebar(layout, page);
  wrapTables(content);
  buildToc(layout, content);
  buildPager(content, page);
  buildFooter(content);
  // Where a page names the current version (the downloads), it is this one.
  for (const el of document.querySelectorAll(".phoenix-version")) el.textContent = VERSION;
  for (const el of document.querySelectorAll("[data-version-text]")) el.textContent = el.dataset.versionText.replace("{}", VERSION);
  setupSearch();
  setupLightbox(content);
  // Last, so it sees the finished page (headings with ids, header, footer).
  loadAnalytics();
});
