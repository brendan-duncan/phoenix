// A ZIP of several files, written as it is sent: each entry stored as it is
// (a .phx is compressed already), its CRC worked out on the way and written
// after it. No ZIP64, so the whole is kept under 4 GB.

import { once } from 'node:events';
import zlib from 'node:zlib';

export const ZIP_LIMIT = 0xffffffff - 1024 * 1024;

/** The size of the ZIP of `entries` ({ name, size }), known before it is written. */
export function zipSize(entries) {
  let size = 22;
  for (const e of entries) {
    const name = Buffer.byteLength(e.name);
    size += 30 + name + e.size + 16 + 46 + name;
  }
  return size;
}

function dosTime(ms) {
  const d = new Date(ms);
  const time = (d.getHours() << 11) | (d.getMinutes() << 5) | Math.floor(d.getSeconds() / 2);
  const date = ((Math.max(d.getFullYear(), 1980) - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
  return { time, date };
}

/**
 * Writes the ZIP of `entries` to `out`, then ends it.
 * @param {{ name: string, size: number, modifiedAt: number, bytes: () => AsyncIterable<Buffer> }[]} entries
 * @param {import('node:stream').Writable} out
 */
export async function writeZip(entries, out) {
  const write = async (buf) => {
    if (!out.write(buf)) {
      await once(out, 'drain');
    }
  };
  // UTF-8 names; sizes and CRC in a descriptor after the data.
  const FLAGS = 0x0808;
  let offset = 0;
  const central = [];
  for (const e of entries) {
    const name = Buffer.from(e.name);
    const { time, date } = dosTime(e.modifiedAt ?? Date.now());
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(FLAGS, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt16LE(time, 10);
    local.writeUInt16LE(date, 12);
    local.writeUInt16LE(name.length, 26);
    await write(local);
    await write(name);
    let crc = 0;
    let size = 0;
    for await (const piece of e.bytes()) {
      crc = zlib.crc32(piece, crc);
      size += piece.length;
      await write(piece);
    }
    if (size !== e.size) {
      throw new Error(`${e.name} was ${size} bytes, not ${e.size}`);
    }
    const descriptor = Buffer.alloc(16);
    descriptor.writeUInt32LE(0x08074b50, 0);
    descriptor.writeUInt32LE(crc, 4);
    descriptor.writeUInt32LE(size, 8);
    descriptor.writeUInt32LE(size, 12);
    await write(descriptor);
    const entry = Buffer.alloc(46);
    entry.writeUInt32LE(0x02014b50, 0);
    entry.writeUInt16LE(20, 4);
    entry.writeUInt16LE(20, 6);
    entry.writeUInt16LE(FLAGS, 8);
    entry.writeUInt16LE(0, 10);
    entry.writeUInt16LE(time, 12);
    entry.writeUInt16LE(date, 14);
    entry.writeUInt32LE(crc, 16);
    entry.writeUInt32LE(size, 20);
    entry.writeUInt32LE(size, 24);
    entry.writeUInt16LE(name.length, 28);
    entry.writeUInt32LE(offset, 42);
    central.push(entry, name);
    offset += 30 + name.length + size + 16;
  }
  const directory = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12);
  end.writeUInt32LE(offset, 16);
  await write(directory);
  await write(end);
  out.end();
}
