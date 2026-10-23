// Outgoing email. Each message is written to the data folder's outbox
// (and, in dev, printed), where Admin > Email lists the latest with how
// each went; with an SMTP service set up (PHOENIX_CLOUD_SMTP_HOST, Amazon
// SES say) it is also sent, in the background, trying again a couple of
// times when the service says to try later. Past an hourly number of emails
// in all, or to one address, mail is held in the outbox and not sent.
// The outbox keeps a copy of each for a while (PHOENIX_CLOUD_MAIL_KEEP_DAYS,
// 30 by default): they hold sign-in and reset links, so not forever.

import fs from 'node:fs';
import path from 'node:path';
import { Budget } from './limits.mjs';
import { buildMessage, sendMail } from './smtp.mjs';

const HOUR = 60 * 60 * 1000;
const DAY = 24 * HOUR;
// An outbox file's name: when it was written, then the address it went to.
const FILE_RE = /^(\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}-\d{3}Z)-[^/\\]+\.txt$/;
// Statuses, as Admin > Email filters them, from the start of the Status line.
export const MAIL_STATUSES = ['sent', 'failed', 'held', 'waiting', 'sending', 'outbox'];

function statusKind(status) {
  return MAIL_STATUSES.find((s) => status.startsWith(s)) ?? 'outbox';
}

/** The time in an outbox file's name. */
function writtenAt(name) {
  const m = FILE_RE.exec(name);
  return m ? Date.parse(m[1].replace(/T(\d{2})-(\d{2})-(\d{2})-(\d{3})Z/, 'T$1:$2:$3.$4Z')) : 0;
}

// Waits before trying again after a failure the service calls temporary.
const RETRY_MS = [60 * 1000, 10 * 60 * 1000];

export class Mailer {
  constructor(config) {
    this.dir = path.join(config.dataDir, 'outbox');
    this.echo = config.mode === 'dev';
    this.smtp = config.smtp;
    this.from = config.mailFrom;
    this.replyTo = config.mailReplyTo;
    this.perHour = config.mailPerHour ?? 100;
    this.allMail = new Budget(this.perHour, HOUR);
    this.perAddress = new Budget(config.mailPerAddressPerHour ?? 5, HOUR);
    this.keepDays = config.mailKeepDays ?? 30;
    this.pruned = 0;
    this.prune();
    // Sent one at a time, in order.
    this.queue = Promise.resolve();
  }

  /** Where mail goes: { sending, host, from, replyTo, perHour }. */
  describe() {
    return { sending: Boolean(this.smtp), host: this.smtp?.host ?? null, from: this.from, replyTo: this.replyTo, perHour: this.perHour };
  }

  /**
   * Writes { to, subject, text } to the outbox and, with SMTP set up, sends
   * it; `replyTo` overrides where replies go (PHOENIX_CLOUD_MAIL_REPLY_TO).
   * Returns a promise of how the sending went ({ status, detail }), which
   * callers may ignore: it never rejects.
   */
  send({ to, subject, text, replyTo = null }) {
    fs.mkdirSync(this.dir, { recursive: true });
    if (Date.now() - this.pruned > HOUR) {
      this.prune();
    }
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const file = path.join(this.dir, `${stamp}-${String(to).replace(/[^a-z0-9@._-]/gi, '_')}.txt`);
    const write = (status) => fs.writeFileSync(file, `To: ${to}\nSubject: ${subject}\nStatus: ${status}\n\n${text}\n`);
    write(this.smtp ? 'sending' : 'outbox only (no mail service set up)');
    if (this.echo) {
      console.log(`[cloud] mail to ${to}: ${subject}\n${text}`);
    }
    if (!this.smtp) {
      return Promise.resolve({ status: 'outbox', detail: 'No mail service is set up, so it is only in the outbox.' });
    }
    // Over a budget: held (in the outbox), not sent. The address is checked
    // first, so a flood to one inbox doesn't spend the site's budget.
    const address = String(to).trim().toLowerCase();
    const held = !this.perAddress.take(address) ? 'too many emails to this address in the last hour'
      : !this.allMail.take('all') ? `the limit of ${this.perHour} emails an hour was reached` : null;
    if (held) {
      write(`held, not sent: ${held}`);
      console.error(`[cloud] mail to ${to} held: ${held}`);
      return Promise.resolve({ status: 'held', detail: `Not sent: ${held}. It is in the outbox.` });
    }
    const message = buildMessage({ from: this.from, to, replyTo: replyTo || this.replyTo, subject, text });
    const attempt = async (tries) => {
      try {
        const { reply } = await sendMail(this.smtp, { from: this.from, to, message });
        write(`sent (${reply})`);
        return { status: 'sent', detail: reply };
      } catch (err) {
        if (err.temporary && tries < RETRY_MS.length) {
          write(`waiting to try again: ${err.message}`);
          await new Promise((resolve) => setTimeout(resolve, RETRY_MS[tries]).unref?.());
          return attempt(tries + 1);
        }
        write(`failed: ${err.message}`);
        console.error(`[cloud] mail to ${to} failed: ${err.message}`);
        return { status: 'failed', detail: err.message };
      }
    };
    const result = this.queue.then(() => attempt(0));
    this.queue = result.catch(() => {});
    return result;
  }

  /** The outbox's file names, newest first. */
  files() {
    return fs.existsSync(this.dir) ? fs.readdirSync(this.dir).filter((n) => FILE_RE.test(n)).sort().reverse() : [];
  }

  read(name) {
    const text = fs.readFileSync(path.join(this.dir, name), 'utf8');
    const [head, ...body] = text.split('\n\n');
    const field = (key) => head.split('\n').find((l) => l.startsWith(`${key}: `))?.slice(key.length + 2) ?? '';
    const status = field('Status');
    return { file: name, to: field('To'), subject: field('Subject'), status, kind: statusKind(status), at: writtenAt(name), text: body.join('\n\n').trim() };
  }

  /**
   * The messages that match, newest first: { q } in the address or subject,
   * { status } one of MAIL_STATUSES. { messages (up to `limit`), total }.
   */
  find({ q = '', status = '', limit = 50 } = {}) {
    const words = String(q).trim().toLowerCase();
    const all = this.files().map((name) => this.read(name))
      .filter((m) => (!status || m.kind === status) && (!words || `${m.to} ${m.subject}`.toLowerCase().includes(words)));
    return { messages: all.slice(0, limit), total: all.length, kept: this.files().length };
  }

  /** The latest messages, newest first. */
  recent(limit = 50) {
    return this.find({ limit }).messages;
  }

  /** Deletes one message from the outbox; whether it was there. */
  remove(name) {
    if (!FILE_RE.test(String(name))) {
      return false;
    }
    const file = path.join(this.dir, name);
    if (!fs.existsSync(file)) {
      return false;
    }
    fs.rmSync(file);
    return true;
  }

  /** Deletes every message that matches (as find); how many. */
  removeAll({ q = '', status = '' } = {}) {
    const { messages } = this.find({ q, status, limit: Infinity });
    let n = 0;
    for (const m of messages) {
      // One still being sent stays until it is done.
      if (m.kind !== 'sending' && m.kind !== 'waiting' && this.remove(m.file)) {
        n++;
      }
    }
    return n;
  }

  /** Deletes messages older than keepDays. */
  prune(now = Date.now()) {
    this.pruned = now;
    if (!this.keepDays) {
      return 0;
    }
    let n = 0;
    for (const name of this.files()) {
      if (writtenAt(name) < now - this.keepDays * DAY && this.remove(name)) {
        n++;
      }
    }
    return n;
  }
}
