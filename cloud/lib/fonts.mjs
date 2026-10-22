// The font library: fonts the web editor loads (the browser has few of its
// own) and the server's Phoenix renders with, in one folder. It starts with
// open fonts drawn with the same widths as the common ones documents name
// (Liberation for Arial, Times New Roman and Courier New; Carlito for
// Calibri; Caladea for Cambria), which Phoenix uses in their place; admins
// add more from the Admin page.

import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import zlib from 'node:zlib';

export const FONT_TYPES = ['.ttf', '.otf'];
const MAX_FONT_BYTES = 20 * 1024 * 1024;

const STARTER = {
  liberation: 'https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz',
  files: ['Carlito-Regular', 'Carlito-Bold', 'Carlito-Italic', 'Carlito-BoldItalic'].map((n) => `https://raw.githubusercontent.com/google/fonts/main/ofl/carlito/${n}.ttf`)
    .concat(['Caladea-Regular', 'Caladea-Bold', 'Caladea-Italic', 'Caladea-BoldItalic'].map((n) => `https://raw.githubusercontent.com/google/fonts/main/ofl/caladea/${n}.ttf`)),
};

/** A font file's name made safe: letters, digits, '-', '_', '.', and a font extension. */
export function fontName(name) {
  const base = path.basename(String(name ?? '')).replace(/[^A-Za-z0-9._-]+/g, '_').slice(0, 100);
  return FONT_TYPES.includes(path.extname(base).toLowerCase()) && !base.startsWith('.') ? base : null;
}

/** Whether `bytes` start like a TrueType or OpenType font. */
export function looksLikeFont(bytes) {
  const tag = bytes.subarray(0, 4).toString('latin1');
  return tag === 'OTTO' || tag === 'true' || bytes.readUInt32BE(0) === 0x00010000;
}

/** The fonts in the library: [{ name, size }]. */
export function listFonts(dir) {
  if (!fs.existsSync(dir)) {
    return [];
  }
  return fs.readdirSync(dir).filter((n) => fontName(n) === n).sort()
    .map((name) => ({ name, size: fs.statSync(path.join(dir, name)).size }));
}

async function download(url) {
  const res = await fetch(url, { signal: AbortSignal.timeout(60000) });
  if (!res.ok) {
    throw new Error(`${url} answered ${res.status}`);
  }
  return Buffer.from(await res.arrayBuffer());
}

/** The .ttf files in a .tar.gz, by their own names (never a path out of the folder). */
function ttfsInTarGz(archive) {
  const tar = zlib.gunzipSync(archive, { maxOutputLength: 64 * 1024 * 1024 });
  const out = [];
  for (let at = 0; at + 512 <= tar.length;) {
    const name = tar.toString('utf8', at, at + 100).replace(/\0.*$/s, '');
    if (!name) {
      break;
    }
    const size = Number.parseInt(tar.toString('latin1', at + 124, at + 136).replace(/\0.*$/s, '').trim() || '0', 8);
    const type = tar.toString('latin1', at + 156, at + 157);
    const body = tar.subarray(at + 512, at + 512 + size);
    const file = fontName(name);
    if ((type === '0' || type === '\0') && file && file.toLowerCase().endsWith('.ttf') && looksLikeFont(body)) {
      out.push({ name: file, body });
    }
    at += 512 + Math.ceil(size / 512) * 512;
  }
  return out;
}

/** Downloads the starter fonts into `dir`; the names it wrote. */
export async function installStarterFonts(dir) {
  await fsp.mkdir(dir, { recursive: true });
  const written = [];
  for (const { name, body } of ttfsInTarGz(await download(STARTER.liberation))) {
    await fsp.writeFile(path.join(dir, name), body);
    written.push(name);
  }
  for (const url of STARTER.files) {
    const body = await download(url);
    const name = fontName(url);
    if (name && looksLikeFont(body)) {
      await fsp.writeFile(path.join(dir, name), body);
      written.push(name);
    }
  }
  return written;
}

/** Adds an uploaded font to the library; its name. */
export async function addFont(dir, name, file) {
  const safe = fontName(name);
  const { size } = await fsp.stat(file);
  if (!safe || size > MAX_FONT_BYTES) {
    return null;
  }
  const head = Buffer.alloc(4);
  const handle = await fsp.open(file, 'r');
  await handle.read(head, 0, 4, 0);
  await handle.close();
  if (!looksLikeFont(head)) {
    return null;
  }
  await fsp.mkdir(dir, { recursive: true });
  await fsp.copyFile(file, path.join(dir, safe));
  return safe;
}
