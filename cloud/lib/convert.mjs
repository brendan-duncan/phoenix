// Phoenix, run with no window, does the work the server can't do itself:
// making a new document, turning an upload (FLA, SWF, SVG, EPS, AI) into a
// .phx, and drawing a document's first frame as its thumbnail. Each job is a
// JSFL script for `Phoenix run-script` and a `Phoenix render`, in a folder
// of its own with settings of its own (the person's own Phoenix settings
// are never touched), one job at a time, with a time limit and a memory
// limit. Uploads are checked before Phoenix opens them: their size, and how
// large an FLA's archive or a SWF unpacks. Files go in and come out as files
// on disk, never whole in this process's memory. The Converter is the one
// way in, so it can become a worker on another machine later.

import { execFile as execFileCallback, spawn } from 'node:child_process';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { promisify } from 'node:util';
import { HttpError } from './http.mjs';
import { newId } from './ids.mjs';
import { bodyOffset, describe, FileSource, isPhx, isThumbnail, PhxError, readContainer } from './phx.mjs';

const execFile = promisify(execFileCallback);

/** Upload types and how Phoenix takes each: opened as a document, or imported as artwork. */
export const UPLOAD_KINDS = { phx: 'phx', fla: 'document', swf: 'document', svg: 'artwork', eps: 'artwork', ai: 'artwork' };
export const THUMBNAIL_WIDTH = 256;
const MAX_QUEUE = 20;
const TIMEOUT_MS = 3 * 60 * 1000;
// How often a running job's memory is looked at.
const WATCH_MS = 200;

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
    this.maxMemoryMB = config.convertMaxMemoryMB;
    this.fontsDir = config.fontsDir;
    this.maxUploadBytes = config.convertMaxUploadBytes;
    this.maxExpandedBytes = config.convertMaxExpandedBytes;
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

  /**
   * Runs Phoenix with `args`: { code, output, outOfMemory }. A watchdog
   * stops it when it uses more memory than one job may.
   */
  run(dir, args) {
    return new Promise((resolve) => {
      const env = {
        ...process.env,
        QT_QPA_PLATFORM: 'offscreen',
        PHOENIX_SETTINGS_DIR: path.join(dir, 'settings'),
        PHOENIX_NATIVE_EXTENSIONS: 'off',
        PHOENIX_GPU_DISABLE: '1',
        // The server's font library, as the editor has it.
        ...(this.fontsDir && fs.existsSync(this.fontsDir) ? { PHOENIX_FONTS_DIR: this.fontsDir } : {}),
        TEMP: dir,
        TMP: dir,
        TMPDIR: dir,
      };
      const child = spawn(this.exe, args, { cwd: dir, env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
      let output = '';
      let outOfMemory = false;
      const keep = (d) => {
        output = (output + d.toString()).slice(-20000);
      };
      child.stdout.on('data', keep);
      child.stderr.on('data', keep);
      const timer = setTimeout(() => child.kill('SIGKILL'), TIMEOUT_MS);
      // Reading another process's memory is cheap on Linux; on Windows each
      // look takes a moment (tasklist), so a job over in less goes unseen,
      // which is harmless: running out of memory takes time.
      let looking = false;
      const look = async () => {
        if (looking || !child.pid) {
          return;
        }
        looking = true;
        const mb = await memoryMB(child.pid);
        looking = false;
        if (mb > this.maxMemoryMB) {
          outOfMemory = true;
          child.kill('SIGKILL');
        }
      };
      look();
      const watchdog = setInterval(look, WATCH_MS);
      const done = (result) => {
        clearTimeout(timer);
        clearInterval(watchdog);
        resolve({ ...result, outOfMemory });
      };
      child.on('error', (err) => done({ code: -1, output: err.message }));
      child.on('close', (code) => done({ code, output }));
    });
  }

  /** What Phoenix said, without the graphics drivers' chatter. */
  static problem(output, dir) {
    // The job's own file names mean nothing to the person uploading.
    const plain = output.split(dir).join('').split(dir.replaceAll('\\', '/')).join('');
    const lines = plain.split(/\r?\n/)
      .map((l) => l.replace(/^[\\/]*job\.jsfl:\d+:\s*(Error:\s*)?/, '').replace(/^[\\/]*in\.\w+:\s*/, '').replace(/^[^\s]*[\\/][^\s]*\.(fla|swf|svg|eps|ai|phx):\s*/i, '').trim())
      .filter((l) => l && !l.startsWith('at ') && !/vulkan|dawn|BackendVk|PHOENIX_SIZE/i.test(l));
    return lines.slice(-3).join(' ').slice(0, 300) || 'Phoenix could not read it.';
  }

  memoryError() {
    return new HttpError(413, 'too_much_memory', `Phoenix needed more than the ${this.maxMemoryMB} MB of memory the server gives one document, so it was stopped.`);
  }

  /** Runs a script that saves `job.out`; its stage size. */
  async runScript(dir, job) {
    const file = path.join(dir, 'job.jsfl');
    await fsp.writeFile(file, script(job));
    const { code, output, outOfMemory } = await this.run(dir, ['run-script', file, '--timeout', '170', '--no-native-extensions']);
    if (outOfMemory) {
      throw this.memoryError();
    }
    const size = /PHOENIX_SIZE (\d+) (\d+)/.exec(output);
    if (code !== 0 || !size || !fs.existsSync(job.out) || !isPhx(await readStart(job.out))) {
      throw new ConvertError(`Phoenix could not convert it: ${Converter.problem(output, dir)}`);
    }
    return { width: Number(size[1]), height: Number(size[2]) };
  }

  /** Draws the first frame of `phxFile` THUMBNAIL_WIDTH wide; null if it can't. */
  async render(dir, phxFile, size) {
    const width = THUMBNAIL_WIDTH;
    const height = Math.max(8, Math.min(1024, Math.round(width * size.height / Math.max(1, size.width))));
    const stem = path.join(dir, 'thumb.png');
    const { outOfMemory } = await this.run(dir, ['render', phxFile, '--range', '1', '--out', stem, '--size', `${width}x${height}`]);
    const made = !outOfMemory && fs.readdirSync(dir).find((f) => /^thumb.*\.png$/i.test(f));
    return made ? fsp.readFile(path.join(dir, made)) : null;
  }

  /**
   * The job's result, out of its folder (which goes when the job ends):
   * { file, thumbnail }. The caller deletes the file once it is stored.
   */
  async finish(dir, out, size, thumbnail = undefined) {
    const png = thumbnail === undefined ? await this.render(dir, out, size) : thumbnail;
    const file = path.join(this.workDir, `${newId()}.phx`);
    await fsp.rename(out, file);
    return { file, thumbnail: png };
  }

  /**
   * Work nobody waits for (previews, bundles): left out while people's own
   * jobs are waiting, so it never holds them up.
   */
  background(job) {
    if (!this.exe || this.waiting > 1) {
      return Promise.resolve(null);
    }
    return this.enqueue(job).catch(() => null);
  }

  /**
   * An animated preview of the start of a document (`phxFile`, its stage
   * `size`): a small WebP of up to PREVIEW_FRAMES frames; null if it can't.
   */
  preview(phxFile, size) {
    return this.background(() => this.withFolder(async (dir) => {
      const width = PREVIEW_WIDTH;
      const height = Math.max(8, Math.min(800, Math.round(width * size.height / Math.max(1, size.width))));
      const out = path.join(dir, 'preview.webp');
      const { outOfMemory } = await this.run(dir, ['render', phxFile, '--range', `1-${PREVIEW_FRAMES}`, '--out', out,
        '--size', `${width}x${height}`, '--fps', '12', '--quality', '60', '--no-audio']);
      return !outOfMemory && fs.existsSync(out) ? fsp.readFile(out) : null;
    }));
  }

  /**
   * Publishes `phxFile` as HTML5 (a page, its runtime and the movie's
   * content and media) into the folder `target`; true when it did.
   */
  publish(phxFile, target) {
    return this.enqueue(() => this.withFolder(async (dir) => {
      const page = path.join(dir, 'bundle', 'index.html');
      await fsp.mkdir(path.dirname(page), { recursive: true });
      const { output, outOfMemory } = await this.run(dir, ['render', phxFile, '--out', page]);
      if (outOfMemory) {
        throw this.memoryError();
      }
      if (!fs.existsSync(page)) {
        throw new ConvertError(`Phoenix could not publish it: ${Converter.problem(output, dir)}`);
      }
      await fsp.mkdir(path.dirname(target), { recursive: true });
      await fsp.rm(target, { recursive: true, force: true });
      await fsp.rename(path.dirname(page), target);
      return true;
    }));
  }

  /** A new, empty document: { file, thumbnail }. */
  newDocument({ width, height, fps, background }) {
    return this.enqueue(() => this.withFolder(async (dir) => {
      const out = path.join(dir, 'out.phx');
      const size = await this.runScript(dir, { kind: 'new', out, width, height, fps, background });
      return this.finish(dir, out, size);
    }));
  }

  /**
   * A document Phoenix opens where it lies (a starter template's or a
   * lesson's: an XFL DOMDocument.xml, an FLA or a .phx): { file, thumbnail },
   * `thumbnail` (PNG bytes) given or drawn from it.
   */
  fromFile(file, thumbnail = undefined) {
    return this.enqueue(() => this.withFolder(async (dir) => {
      const out = path.join(dir, 'out.phx');
      const size = await this.runScript(dir, { kind: 'document', input: file, out });
      return this.finish(dir, out, size, thumbnail);
    }));
  }

  /**
   * Refuses an upload Phoenix should not open, before it does: too large,
   * or an archive or movie that unpacks to more than one job may hold.
   */
  async check(file, ext) {
    const { size } = await fsp.stat(file);
    if (size > this.maxUploadBytes) {
      throw new HttpError(413, 'too_large', `Phoenix converts files of up to ${mb(this.maxUploadBytes)} MB; that one is ${mb(size)} MB.`);
    }
    let expanded = null;
    if (ext === 'fla') {
      expanded = await zipContents(file);
      if (expanded && expanded.entries > MAX_ARCHIVE_FILES) {
        throw new HttpError(413, 'too_large', `That FLA holds ${expanded.entries} files, more than the ${MAX_ARCHIVE_FILES} Phoenix converts.`);
      }
    } else if (ext === 'swf') {
      expanded = await swfContents(file);
    }
    if (expanded && expanded.bytes > this.maxExpandedBytes) {
      throw new HttpError(413, 'too_large', `That file unpacks to ${mb(expanded.bytes)} MB, more than the ${mb(this.maxExpandedBytes)} MB Phoenix converts.`);
    }
  }

  /**
   * An upload (a file named with its extension) as a .phx: { file, thumbnail }.
   * A .phx comes back as it is (`file` is the upload), with a thumbnail drawn
   * when it has none; the rest are converted.
   */
  async fromUpload(file, ext) {
    const kind = UPLOAD_KINDS[ext];
    if (!kind) {
      throw new ConvertError(`Phoenix Cloud takes ${Object.keys(UPLOAD_KINDS).map((k) => `.${k}`).join(', ')} files.`);
    }
    if (kind === 'phx') {
      let container;
      let stage;
      const source = new FileSource(file);
      try {
        container = readContainer(source);
        stage = describe(container, (chunk) => source.read(bodyOffset(chunk), chunk.storedLength));
      } catch (err) {
        if (err instanceof PhxError) {
          throw new HttpError(422, 'not_phx', `That file can't be stored: ${err.message}.`);
        }
        throw err;
      } finally {
        source.close();
      }
      if (!this.available || container.chunks.some(isThumbnail)) {
        return { file, thumbnail: null };
      }
      const thumbnail = await this.enqueue(() => this.withFolder((dir) => this.render(dir, file, stage)));
      return { file, thumbnail };
    }
    await this.check(file, ext);
    const size = ext === 'svg' ? svgSize(await readStart(file, 65536, 'utf8')) : null;
    return this.enqueue(() => this.withFolder(async (dir) => {
      const out = path.join(dir, 'out.phx');
      const stage = await this.runScript(dir, { kind, input: file, out, size });
      return this.finish(dir, out, stage);
    }));
  }
}

const MAX_ARCHIVE_FILES = 200000;
// Animated previews: how wide, and how many frames of the start.
const PREVIEW_WIDTH = 256;
const PREVIEW_FRAMES = 48;

function mb(bytes) {
  return Math.ceil(bytes / (1024 * 1024));
}

/** The first `length` bytes of a file (as text with `encoding`). */
async function readStart(file, length = 16, encoding = null) {
  const handle = await fsp.open(file, 'r');
  try {
    const buf = Buffer.alloc(length);
    const { bytesRead } = await handle.read(buf, 0, length, 0);
    return encoding ? buf.toString(encoding, 0, bytesRead) : buf.subarray(0, bytesRead);
  } finally {
    await handle.close();
  }
}

async function readAt(handle, offset, length) {
  const buf = Buffer.alloc(length);
  const { bytesRead } = await handle.read(buf, 0, length, offset);
  return buf.subarray(0, bytesRead);
}

/**
 * How much a ZIP archive (an FLA) unpacks to, from its central directory,
 * without unpacking anything: { bytes, entries }; null when it is not a ZIP
 * (an old binary FLA, which Phoenix turns down by itself).
 */
export async function zipContents(file) {
  const handle = await fsp.open(file, 'r');
  try {
    const { size } = await handle.stat();
    if ((await readAt(handle, 0, 2)).toString('latin1') !== 'PK') {
      return null;
    }
    const tailLength = Math.min(size, 22 + 65535);
    const tail = await readAt(handle, size - tailLength, tailLength);
    const end = tail.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
    if (end < 0) {
      throw new HttpError(422, 'convert', 'That FLA is not a whole archive (was the upload cut short?).');
    }
    let entries = tail.readUInt16LE(end + 10);
    let directorySize = tail.readUInt32LE(end + 12);
    let directoryOffset = tail.readUInt32LE(end + 16);
    // ZIP64: the real numbers are in the ZIP64 end record its locator points at.
    if (entries === 0xffff || directorySize === 0xffffffff || directoryOffset === 0xffffffff) {
      const locator = end - 20;
      if (locator >= 0 && tail.readUInt32LE(locator) === 0x07064b50) {
        const record = await readAt(handle, Number(tail.readBigUInt64LE(locator + 8)), 56);
        if (record.length === 56 && record.readUInt32LE(0) === 0x06064b50) {
          entries = Number(record.readBigUInt64LE(32));
          directorySize = Number(record.readBigUInt64LE(40));
          directoryOffset = Number(record.readBigUInt64LE(48));
        }
      }
    }
    // Some FLAs give a wrong directory size (Phoenix reads them all the same):
    // the directory is read from its start up to the end record.
    const endAt = size - tailLength + end;
    const span = endAt - directoryOffset;
    if (directoryOffset >= endAt || span > 64 * 1024 * 1024) {
      throw new HttpError(422, 'convert', 'That FLA\'s archive does not read right.');
    }
    const directory = await readAt(handle, directoryOffset, span);
    let bytes = 0;
    let count = 0;
    for (let at = 0; at + 46 <= directory.length && directory.readUInt32LE(at) === 0x02014b50; count++) {
      let unpacked = directory.readUInt32LE(at + 24);
      const nameLength = directory.readUInt16LE(at + 28);
      const extraLength = directory.readUInt16LE(at + 30);
      const commentLength = directory.readUInt16LE(at + 32);
      if (unpacked === 0xffffffff) {
        // The ZIP64 extra field (0x0001) holds it, first.
        const extra = directory.subarray(at + 46 + nameLength, at + 46 + nameLength + extraLength);
        for (let e = 0; e + 4 <= extra.length;) {
          const id = extra.readUInt16LE(e);
          const length = extra.readUInt16LE(e + 2);
          if (id === 0x0001 && length >= 8) {
            unpacked = Number(extra.readBigUInt64LE(e + 4));
            break;
          }
          e += 4 + length;
        }
      }
      bytes += unpacked;
      at += 46 + nameLength + extraLength + commentLength;
    }
    return { bytes, entries: Math.max(count, entries) };
  } finally {
    await handle.close();
  }
}

/** How large a SWF movie is unpacked, from its header: { bytes }; null when it is not a SWF. */
export async function swfContents(file) {
  const header = await readStart(file, 8);
  const signature = header.toString('latin1', 0, 3);
  if (header.length < 8 || !['FWS', 'CWS', 'ZWS'].includes(signature)) {
    return null;
  }
  return { bytes: header.readUInt32LE(4) };
}

/** A process's memory in MB (its resident set); 0 when it can't be read. */
async function memoryMB(pid) {
  try {
    if (process.platform === 'linux') {
      const status = await fsp.readFile(`/proc/${pid}/status`, 'utf8');
      return Number(/VmRSS:\s+(\d+)/.exec(status)?.[1] ?? 0) / 1024;
    }
    if (process.platform === 'win32') {
      const { stdout } = await execFile('tasklist', ['/FI', `PID eq ${pid}`, '/FO', 'CSV', '/NH'], { windowsHide: true });
      const field = stdout.trim().split('","').pop() ?? '';
      return Number(field.replace(/[^\d]/g, '')) / 1024;
    }
    const { stdout } = await execFile('ps', ['-o', 'rss=', '-p', String(pid)]);
    return Number(stdout.trim()) / 1024;
  } catch {
    return 0;
  }
}
