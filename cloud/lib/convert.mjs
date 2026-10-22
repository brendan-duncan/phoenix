// Phoenix, run with no window, does the work the server can't do itself:
// making a new document, turning an upload (FLA, SWF, SVG, EPS, AI) into a
// .phx, and drawing a document's first frame as its thumbnail. Each job is a
// JSFL script for `Phoenix run-script` and a `Phoenix render`, in a folder
// of its own with settings of its own (the person's own Phoenix settings
// are never touched), one job at a time, with a time limit.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { HttpError } from './http.mjs';
import { newId } from './ids.mjs';
import { isPhx, PhxError, splitContainer, withThumbnail } from './phx.mjs';

/** Upload types and how Phoenix takes each: opened as a document, or imported as artwork. */
export const UPLOAD_KINDS = { phx: 'phx', fla: 'document', swf: 'document', svg: 'artwork', eps: 'artwork', ai: 'artwork' };
export const THUMBNAIL_WIDTH = 256;
const MAX_QUEUE = 20;
const TIMEOUT_MS = 3 * 60 * 1000;

export class ConvertError extends HttpError {
  constructor(message) {
    super(422, 'convert', message);
  }
}

/** An SVG's own size from its width and height, or its viewBox. */
export function svgSize(text) {
  const tag = /<svg\b[^>]*>/i.exec(text)?.[0];
  if (!tag) {
    return null;
  }
  const attr = (name) => new RegExp(`\\s${name}\\s*=\\s*["']([^"']+)["']`, 'i').exec(tag)?.[1];
  const length = (value) => {
    const m = /^\s*([\d.]+)\s*(px|pt|in|cm|mm)?\s*$/i.exec(value ?? '');
    if (!m) {
      return null;
    }
    const scale = { px: 1, pt: 4 / 3, in: 96, cm: 96 / 2.54, mm: 96 / 25.4 }[(m[2] ?? 'px').toLowerCase()];
    return Number(m[1]) * scale;
  };
  let width = length(attr('width'));
  let height = length(attr('height'));
  if (!width || !height) {
    const box = (attr('viewBox') ?? '').trim().split(/[\s,]+/).map(Number);
    if (box.length === 4 && box[2] > 0 && box[3] > 0) {
      [width, height] = [box[2], box[3]];
    }
  }
  return width && height ? { width: Math.round(width), height: Math.round(height) } : null;
}

const uri = (file) => `FLfile.platformPathToURI(${JSON.stringify(file)})`;

function script(job) {
  const lines = [`var out = ${uri(job.out)};`];
  if (job.kind === 'new') {
    lines.push(
      'var doc = fl.createDocument();',
      `doc.width = ${job.width}; doc.height = ${job.height}; doc.frameRate = ${job.fps};`,
      `doc.backgroundColor = ${JSON.stringify(job.background)};`);
  } else if (job.kind === 'document') {
    lines.push(`var doc = fl.openDocument(${uri(job.input)});`);
  } else {
    // Artwork comes in on the stage; the stage takes its size from the
    // artwork's own page when it says one, or fits round what came in.
    lines.push(
      'var doc = fl.createDocument();',
      `doc.importFile(${uri(job.input)}, false);`,
      'doc.selectAll();',
      'if (!doc.selection || doc.selection.length == 0) throw new Error("Nothing in the file could be imported.");',
      job.size
        ? `doc.width = ${job.size.width}; doc.height = ${job.size.height};`
        : 'var r = doc.getSelectionRect(); doc.moveSelectionBy({x: -r.left, y: -r.top}); doc.width = Math.max(1, Math.ceil(r.right - r.left)); doc.height = Math.max(1, Math.ceil(r.bottom - r.top));',
      'doc.selectNone();');
  }
  lines.push('fl.saveDocument(doc, out);', 'fl.trace("PHOENIX_SIZE " + doc.width + " " + doc.height);');
  return `${lines.join('\n')}\n`;
}

export class Converter {
  constructor(config) {
    this.exe = config.phoenix;
    this.workDir = path.join(config.dataDir, 'work');
    this.queue = Promise.resolve();
    this.waiting = 0;
  }

  get available() {
    return Boolean(this.exe);
  }

  /** Runs `job` after the ones before it. */
  enqueue(job) {
    if (!this.exe) {
      return Promise.reject(new HttpError(503, 'no_phoenix', 'This server has no Phoenix to make or convert documents with (set PHOENIX_CLOUD_PHOENIX).'));
    }
    if (this.waiting >= MAX_QUEUE) {
      return Promise.reject(new HttpError(503, 'busy', 'The server is busy converting documents. Try again in a minute.'));
    }
    this.waiting++;
    const run = this.queue.then(() => job());
    this.queue = run.catch(() => {}).finally(() => {
      this.waiting--;
    });
    return run;
  }

  async withFolder(fn) {
    const dir = path.join(this.workDir, newId());
    await fsp.mkdir(path.join(dir, 'settings'), { recursive: true });
    try {
      return await fn(dir);
    } finally {
      await fsp.rm(dir, { recursive: true, force: true });
    }
  }

  run(dir, args) {
    return new Promise((resolve) => {
      const env = {
        ...process.env,
        QT_QPA_PLATFORM: 'offscreen',
        PHOENIX_SETTINGS_DIR: path.join(dir, 'settings'),
        PHOENIX_NATIVE_EXTENSIONS: 'off',
        PHOENIX_GPU_DISABLE: '1',
        TEMP: dir,
        TMP: dir,
        TMPDIR: dir,
      };
      const child = spawn(this.exe, args, { cwd: dir, env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
      let output = '';
      const keep = (d) => {
        output = (output + d.toString()).slice(-20000);
      };
      child.stdout.on('data', keep);
      child.stderr.on('data', keep);
      const timer = setTimeout(() => child.kill('SIGKILL'), TIMEOUT_MS);
      child.on('error', (err) => {
        clearTimeout(timer);
        resolve({ code: -1, output: err.message });
      });
      child.on('close', (code) => {
        clearTimeout(timer);
        resolve({ code, output });
      });
    });
  }

  /** What Phoenix said, without the graphics drivers' chatter. */
  static problem(output, dir) {
    // The job's own file names mean nothing to the person uploading.
    const plain = output.split(dir).join('').split(dir.replaceAll('\\', '/')).join('');
    const lines = plain.split(/\r?\n/)
      .map((l) => l.replace(/^[\\/]*job\.jsfl:\d+:\s*(Error:\s*)?/, '').replace(/^[\\/]*in\.\w+:\s*/, '').trim())
      .filter((l) => l && !l.startsWith('at ') && !/vulkan|dawn|BackendVk|PHOENIX_SIZE/i.test(l));
    return lines.slice(-3).join(' ').slice(0, 300) || 'Phoenix could not read it.';
  }

  /** Runs a script that saves `out`; its stage size. */
  async runScript(dir, job) {
    const file = path.join(dir, 'job.jsfl');
    await fsp.writeFile(file, script(job));
    const { code, output } = await this.run(dir, ['run-script', file, '--timeout', '170', '--no-native-extensions']);
    const size = /PHOENIX_SIZE (\d+) (\d+)/.exec(output);
    if (code !== 0 || !size || !fs.existsSync(job.out) || !isPhx(await fsp.readFile(job.out))) {
      throw new ConvertError(`Phoenix could not convert it: ${Converter.problem(output, dir)}`);
    }
    return { width: Number(size[1]), height: Number(size[2]) };
  }

  /** Draws the first frame of `phxFile` THUMBNAIL_WIDTH wide; null if it can't. */
  async render(dir, phxFile, size) {
    const width = THUMBNAIL_WIDTH;
    const height = Math.max(8, Math.min(1024, Math.round(width * size.height / Math.max(1, size.width))));
    const stem = path.join(dir, 'thumb.png');
    await this.run(dir, ['render', phxFile, '--range', '1', '--out', stem, '--size', `${width}x${height}`]);
    const made = fs.readdirSync(dir).find((f) => /^thumb.*\.png$/i.test(f));
    return made ? fsp.readFile(path.join(dir, made)) : null;
  }

  async finish(dir, out, size) {
    const bytes = await fsp.readFile(out);
    const png = await this.render(dir, out, size);
    return png ? withThumbnail(bytes, png) : bytes;
  }

  /** A new, empty document. */
  newDocument({ width, height, fps, background }) {
    return this.enqueue(() => this.withFolder(async (dir) => {
      const out = path.join(dir, 'out.phx');
      const size = await this.runScript(dir, { kind: 'new', out, width, height, fps, background });
      return this.finish(dir, out, size);
    }));
  }

  /** An upload as .phx bytes: converted, or a .phx given a thumbnail if it has none. */
  fromUpload(bytes, ext) {
    const kind = UPLOAD_KINDS[ext];
    if (!kind) {
      throw new ConvertError(`Phoenix Cloud takes ${Object.keys(UPLOAD_KINDS).map((k) => `.${k}`).join(', ')} files.`);
    }
    if (kind === 'phx') {
      let split;
      try {
        split = splitContainer(bytes);
      } catch (err) {
        if (err instanceof PhxError) {
          throw new HttpError(422, 'not_phx', `That file can't be stored: ${err.message}.`);
        }
        throw err;
      }
      if (!this.available || split.thumbnail) {
        return Promise.resolve(bytes);
      }
      return this.enqueue(() => this.withFolder(async (dir) => {
        const file = path.join(dir, 'in.phx');
        await fsp.writeFile(file, bytes);
        const png = await this.render(dir, file, split.metadata);
        return png ? withThumbnail(bytes, png) : bytes;
      }));
    }
    return this.enqueue(() => this.withFolder(async (dir) => {
      const input = path.join(dir, `in.${ext}`);
      await fsp.writeFile(input, bytes);
      const out = path.join(dir, 'out.phx');
      const size = ext === 'svg' ? svgSize(bytes.toString('utf8', 0, Math.min(bytes.length, 65536))) : null;
      const stage = await this.runScript(dir, { kind, input, out, size });
      return this.finish(dir, out, stage);
    }));
  }
}
