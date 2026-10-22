// Signing in with OAuth 2.0 / OpenID Connect providers: the authorization
// code flow with PKCE, a state and a nonce, run server side with full-page
// redirects (no pop-ups and no provider scripts on our pages). ID tokens are
// checked against the provider's published keys.

import crypto from 'node:crypto';
import { newToken } from './ids.mjs';

const JWKS_TTL = 60 * 60 * 1000;
const CLOCK_SKEW_S = 120;

export function redirectUri(config, provider) {
  return `${config.publicUrl}/api/auth/callback/${provider.id}`;
}

/** Starts a sign-in: records the state and returns the provider's URL. */
export function startLogin(store, config, provider, { invite = null, linkUserId = null, returnTo = '/' }) {
  const state = newToken();
  const verifier = newToken();
  const nonce = newToken();
  store.run('INSERT INTO login_states (state, provider, verifier, nonce, invite, link_user_id, return_to, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
    state, provider.id, verifier, nonce, invite, linkUserId, returnTo, Date.now());
  const url = new URL(provider.authorizeUrl);
  url.searchParams.set('client_id', provider.clientId);
  url.searchParams.set('redirect_uri', redirectUri(config, provider));
  url.searchParams.set('response_type', 'code');
  url.searchParams.set('scope', provider.scope);
  url.searchParams.set('state', state);
  url.searchParams.set('code_challenge', crypto.createHash('sha256').update(verifier).digest('base64url'));
  url.searchParams.set('code_challenge_method', 'S256');
  if (provider.kind === 'oidc') {
    url.searchParams.set('nonce', nonce);
    url.searchParams.set('prompt', 'select_account');
  }
  return url.toString();
}

/** Takes the state back (once only); null when unknown or too old. */
export function takeLoginState(store, config, providerId, state) {
  if (!state) {
    return null;
  }
  const row = store.get('SELECT * FROM login_states WHERE state = ?', state);
  if (!row) {
    return null;
  }
  store.run('DELETE FROM login_states WHERE state = ?', state);
  if (row.provider !== providerId || Date.now() - row.created_at > config.loginStateMinutes * 60 * 1000) {
    return null;
  }
  return row;
}

async function postForm(url, fields) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json' },
    body: new URLSearchParams(fields),
    signal: AbortSignal.timeout(10000),
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok || body.error) {
    throw new Error(`token exchange failed: ${response.status} ${body.error ?? ''}`);
  }
  return body;
}

async function getJson(url, token) {
  const response = await fetch(url, {
    headers: { Authorization: `Bearer ${token}`, Accept: 'application/json', 'User-Agent': 'phoenix-cloud' },
    signal: AbortSignal.timeout(10000),
  });
  if (!response.ok) {
    throw new Error(`${url} answered ${response.status}`);
  }
  return response.json();
}

/**
 * Exchanges the code and returns the person's profile:
 * { subject, email, emailVerified, name, avatarUrl }.
 */
export async function finishLogin(config, provider, jwks, { code, verifier, nonce }) {
  const tokens = await postForm(provider.tokenUrl, {
    grant_type: 'authorization_code',
    code,
    redirect_uri: redirectUri(config, provider),
    client_id: provider.clientId,
    client_secret: provider.clientSecret,
    code_verifier: verifier,
  });
  if (provider.kind === 'oidc') {
    const claims = await jwks.verify(provider, tokens.id_token, nonce);
    return {
      subject: String(claims.sub),
      email: typeof claims.email === 'string' ? claims.email : null,
      emailVerified: claims.email_verified === true || claims.email_verified === 'true',
      name: String(claims.name ?? claims.given_name ?? ''),
      avatarUrl: typeof claims.picture === 'string' && claims.picture.startsWith('https://') ? claims.picture : '',
    };
  }
  if (provider.kind === 'github') {
    const user = await getJson(provider.userUrl, tokens.access_token);
    const emails = await getJson(provider.emailsUrl, tokens.access_token);
    const primary = Array.isArray(emails) ? emails.find((e) => e.primary && e.verified) : null;
    return {
      subject: String(user.id),
      email: primary?.email ?? null,
      emailVerified: Boolean(primary),
      name: String(user.name || user.login || ''),
      avatarUrl: typeof user.avatar_url === 'string' && user.avatar_url.startsWith('https://') ? user.avatar_url : '',
    };
  }
  throw new Error(`unknown provider kind ${provider.kind}`);
}

/** Providers' signing keys, fetched on demand and cached. */
export class Jwks {
  constructor() {
    this.cache = new Map();
  }

  async keys(url, refresh = false) {
    const cached = this.cache.get(url);
    if (cached && !refresh && Date.now() - cached.at < JWKS_TTL) {
      return cached.keys;
    }
    const response = await fetch(url, { signal: AbortSignal.timeout(10000) });
    if (!response.ok) {
      throw new Error(`keys at ${url} answered ${response.status}`);
    }
    const keys = (await response.json()).keys ?? [];
    this.cache.set(url, { at: Date.now(), keys });
    return keys;
  }

  /** Checks an ID token's signature and claims and returns the claims. */
  async verify(provider, idToken, nonce) {
    const parts = String(idToken ?? '').split('.');
    if (parts.length !== 3) {
      throw new Error('no ID token');
    }
    const header = JSON.parse(Buffer.from(parts[0], 'base64url').toString('utf8'));
    const claims = JSON.parse(Buffer.from(parts[1], 'base64url').toString('utf8'));
    if (header.alg !== 'RS256') {
      throw new Error(`ID token signed with ${header.alg}`);
    }
    let jwk = (await this.keys(provider.jwksUrl)).find((k) => k.kid === header.kid);
    if (!jwk) {
      jwk = (await this.keys(provider.jwksUrl, true)).find((k) => k.kid === header.kid);
    }
    if (!jwk) {
      throw new Error('ID token signed with an unknown key');
    }
    const key = crypto.createPublicKey({ key: jwk, format: 'jwk' });
    const signed = Buffer.from(`${parts[0]}.${parts[1]}`);
    if (!crypto.verify('sha256', signed, key, Buffer.from(parts[2], 'base64url'))) {
      throw new Error('ID token signature does not check out');
    }
    const now = Math.floor(Date.now() / 1000);
    const audience = Array.isArray(claims.aud) ? claims.aud : [claims.aud];
    if (!provider.issuers.includes(claims.iss)) {
      throw new Error(`ID token from issuer ${claims.iss}`);
    }
    if (!audience.includes(provider.clientId)) {
      throw new Error('ID token is for another client');
    }
    if (typeof claims.exp !== 'number' || claims.exp < now - CLOCK_SKEW_S) {
      throw new Error('ID token has expired');
    }
    if (claims.nonce !== nonce) {
      throw new Error('ID token nonce does not match');
    }
    if (!claims.sub) {
      throw new Error('ID token has no subject');
    }
    return claims;
  }
}
