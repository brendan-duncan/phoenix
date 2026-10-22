// Outgoing email. There is no mail server yet: each message is written to
// the data folder's outbox (and, in dev, printed), where the admin page
// lists the latest. Sending through SMTP comes with the remote server.

import fs from 'node:fs';
import path from 'node:path';

export class Mailer {
  constructor(config) {
    this.dir = path.join(config.dataDir, 'outbox');
    this.echo = config.mode === 'dev';
  }

  send({ to, subject, text }) {
    fs.mkdirSync(this.dir, { recursive: true });
    const stamp = new Date().toISOString().replace(/[:.]/g, '-');
    const file = path.join(this.dir, `${stamp}-${String(to).replace(/[^a-z0-9@._-]/gi, '_')}.txt`);
    fs.writeFileSync(file, `To: ${to}\nSubject: ${subject}\n\n${text}\n`);
    if (this.echo) {
      console.log(`[cloud] mail to ${to}: ${subject}\n${text}`);
    }
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
      return { file: name, to: field('To'), subject: field('Subject'), text: body.join('\n\n').trim() };
    });
  }
}
