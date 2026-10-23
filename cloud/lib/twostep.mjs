// Two-step sign-in, stored: an account's authenticator secret, the last
// code's step (each code works once), and its recovery codes (kept hashed).
// Signing in with a password (or a provider) then asks for a code before the
// session is made: the password step hands back a short-lived ticket.

import { hashToken, newToken } from './ids.mjs';
import { checkCode, newRecoveryCodes, normalizeRecoveryCode } from './totp.mjs';

const TICKET_MS = 5 * 60 * 1000;
const TICKET_TRIES = 5;

/** Whether `user` has two-step sign-in on. */
export function hasTwoStep(user) {
  return Boolean(user?.totp_secret);
}

/** New recovery codes for `userId`, replacing any before; the codes (shown once). */
export function replaceRecoveryCodes(store, userId) {
  const codes = newRecoveryCodes();
  store.transaction(() => {
    store.run('DELETE FROM recovery_codes WHERE user_id = ?', userId);
    for (const code of codes) {
      store.run('INSERT INTO recovery_codes (user_id, code_hash) VALUES (?, ?)', userId, hashToken(normalizeRecoveryCode(code)));
    }
  });
  return codes;
}

export function recoveryCodesLeft(store, userId) {
  return store.get('SELECT COUNT(*) AS n FROM recovery_codes WHERE user_id = ? AND used_at IS NULL', userId).n;
}

/**
 * Checks `code` (from the app, or a recovery code) for `user`: 'app',
 * 'recovery' or null. A right one is used up.
 */
export function checkSecondStep(store, user, code) {
  const step = checkCode(user.totp_secret, code, user.totp_last_step ?? 0);
  if (step !== null) {
    store.run('UPDATE users SET totp_last_step = ? WHERE id = ?', step, user.id);
    return 'app';
  }
  const typed = normalizeRecoveryCode(code);
  if (typed.length === 12) {
    const { changes } = store.run('UPDATE recovery_codes SET used_at = ? WHERE user_id = ? AND code_hash = ? AND used_at IS NULL',
      Date.now(), user.id, hashToken(typed));
    if (changes) {
      return 'recovery';
    }
  }
  return null;
}

export function turnOff(store, userId) {
  store.transaction(() => {
    store.run('UPDATE users SET totp_secret = NULL, totp_pending = NULL, totp_last_step = 0 WHERE id = ?', userId);
    store.run('DELETE FROM recovery_codes WHERE user_id = ?', userId);
  });
}

/** Tickets between the password (or provider) step and the code, held in memory. */
export class TwoStepTickets {
  constructor() {
    this.tickets = new Map();
  }

  /** A ticket for `userId`, good for five minutes. */
  make(userId, returnTo = '/') {
    const now = Date.now();
    for (const [key, t] of this.tickets) {
      if (t.expires < now) {
        this.tickets.delete(key);
      }
    }
    const ticket = newToken();
    this.tickets.set(hashToken(ticket), { userId, returnTo, expires: now + TICKET_MS, tries: 0 });
    return ticket;
  }

  /** The ticket's { userId, returnTo }, or null when it is wrong or has run out. */
  peek(ticket) {
    const t = this.tickets.get(hashToken(String(ticket ?? '')));
    return t && t.expires > Date.now() ? t : null;
  }

  /** A wrong code: after a few, the ticket is gone and the password is asked for again. */
  miss(ticket) {
    const key = hashToken(String(ticket ?? ''));
    const t = this.tickets.get(key);
    if (t && ++t.tries >= TICKET_TRIES) {
      this.tickets.delete(key);
    }
  }

  use(ticket) {
    this.tickets.delete(hashToken(String(ticket ?? '')));
  }
}
