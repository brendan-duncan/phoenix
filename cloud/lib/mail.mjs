// Outgoing email. Each message is written to the data folder's outbox
// (and, in dev, printed), where Admin > Email lists the latest with how
// each went; with an SMTP service set up (PHOENIX_CLOUD_SMTP_HOST, Amazon
// SES say) it is also sent, in the background, trying again a couple of
// times when the service says to try later. Past an hourly number of emails
// in all, or to one address, mail is held in the outbox and not sent.

import fs from 'node:fs';
import path from 'node:path';
import { Budget } from './limits.mjs';
import { buildMessage, sendMail } from './smtp.mjs';

const HOUR = 60 * 60 * 1000;

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

  /** The latest messages, newest first. */
  recent(limit = 50) {
    if (!fs.existsSync(this.dir)) {
      return [];
    }
    return fs.readdirSync(this.dir).sort().reverse().slice(0, limit).map((name) => {
      const text = fs.readFileSync(path.join(this.dir, name), 'utf8');
      const [head, ...body] = text.split('\n\n');
      const field = (key) => head.split('\n').find((l) => l.startsWith(`${key}: `))?.slice(key.length + 2) ?? '';
      return { file: name, to: field('To'), subject: field('Subject'), status: field('Status'), text: body.join('\n\n').trim() };
    });
  }
}
