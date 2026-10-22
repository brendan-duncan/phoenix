// What a new document can start from besides a blank stage: Phoenix's
// starter templates (resources/templates, the Start Screen's) and its
// tutorials (resources/tutorials, Help > Tutorials). Both are read from
// Phoenix's resources once, when the server starts.

import fs from 'node:fs';
import path from 'node:path';

const ID_RE = /^[a-z0-9][a-z0-9_-]*$/i;

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return null;
  }
}

function firstExisting(folder, names) {
  return names.map((n) => path.join(folder, n)).find((f) => fs.existsSync(f)) ?? null;
}

/** A document to open: an XFL folder's DOMDocument.xml, or the file itself. */
function documentPath(file) {
  if (!file || !fs.existsSync(file)) {
    return null;
  }
  return fs.statSync(file).isDirectory() ? path.join(file, 'DOMDocument.xml') : file;
}

export class Starters {
  constructor(resourcesDir) {
    this.templates = Starters.readTemplates(path.join(resourcesDir, 'templates'));
    this.tutorials = Starters.readTutorials(path.join(resourcesDir, 'tutorials'));
  }

  static entries(dir) {
    if (!fs.existsSync(dir)) {
      return [];
    }
    return fs.readdirSync(dir, { withFileTypes: true }).filter((e) => e.isDirectory() && ID_RE.test(e.name)).map((e) => ({ id: e.name, folder: path.join(dir, e.name) }));
  }

  static readTemplates(dir) {
    const out = [];
    for (const { id, folder } of Starters.entries(dir)) {
      const info = readJson(path.join(folder, 'template.json'));
      const document = documentPath(folder);
      if (!info || !document || !fs.existsSync(document)) {
        continue;
      }
      out.push({ id, title: String(info.title ?? id), summary: String(info.summary ?? ''), order: Number(info.order ?? 100), document, thumbnail: firstExisting(folder, ['thumbnail.png']) });
    }
    return out.sort((a, b) => a.order - b.order || a.title.localeCompare(b.title));
  }

  static readTutorials(dir) {
    const out = [];
    for (const { id, folder } of Starters.entries(dir)) {
      const lesson = readJson(path.join(folder, 'lesson.json'));
      if (!lesson) {
        continue;
      }
      const start = lesson.start ?? {};
      const fresh = start.new ?? (start.document ? null : {});
      const document = start.document ? documentPath(path.join(folder, start.document)) : null;
      if (start.document && !document) {
        continue;
      }
      out.push({
        id,
        title: String(lesson.title ?? id),
        summary: String(lesson.summary ?? ''),
        minutes: Number(lesson.minutes ?? 5),
        order: Number(lesson.order ?? 100),
        tags: Array.isArray(lesson.tags) ? lesson.tags.map(String) : [],
        // A lesson starts on a new document of a size, or on its own start document.
        stage: fresh ? { width: Number(fresh.width ?? 1280), height: Number(fresh.height ?? 720), fps: Number(fresh.fps ?? 24) } : null,
        document,
        thumbnail: firstExisting(folder, ['thumbnail.gif', 'thumbnail.png']),
      });
    }
    return out.sort((a, b) => a.order - b.order || a.title.localeCompare(b.title));
  }

  template(id) {
    return this.templates.find((t) => t.id === id) ?? null;
  }

  tutorial(id) {
    return this.tutorials.find((t) => t.id === id) ?? null;
  }

  /** What the dashboard lists. */
  view() {
    return {
      templates: this.templates.map((t) => ({ id: t.id, title: t.title, summary: t.summary, thumbnailUrl: t.thumbnail ? `/api/starters/templates/${t.id}/thumbnail` : null })),
      tutorials: this.tutorials.map((t) => ({
        id: t.id, title: t.title, summary: t.summary, minutes: t.minutes, tags: t.tags,
        thumbnailUrl: t.thumbnail ? `/api/starters/tutorials/${t.id}/thumbnail` : null,
      })),
    };
  }
}
