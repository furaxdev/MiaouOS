// Miaou Web prelude (runs before page scripts). Modern JavaScript (QuickJS: ES2023).
'use strict';
const window = globalThis;
Object.assign(globalThis, { window, self: globalThis, top: globalThis, parent: globalThis, frames: globalThis });
const NOP = () => {};
globalThis.Node = Element;
{ const f = (...a) => __log(...a.map(x => { try { return typeof x === 'object' && x !== null ? (x instanceof Error ? String(x) : JSON.stringify(x)) : String(x); } catch (e) { return String(x); } }));
  globalThis.console = { log: f, info: f, warn: f, error: f, debug: f, trace: f, table: f, group: f, groupEnd: NOP, groupCollapsed: f, time: NOP, timeEnd: NOP, assert: NOP, clear: NOP, count: NOP, dir: f }; }
// ---- Element helpers built on the native node API ----
const EP = Element.prototype;
EP.append = function (...a) { for (const x of a) this.appendChild(typeof x === 'object' && x !== null ? x : document.createTextNode(String(x))); };
EP.prepend = function (...a) { const f = this.firstChild; for (const x of a) this.insertBefore(typeof x === 'object' && x !== null ? x : document.createTextNode(String(x)), f); };
EP.before = function (...a) { for (const x of a) this.parentNode && this.parentNode.insertBefore(typeof x === 'object' ? x : document.createTextNode(String(x)), this); };
EP.after = function (...a) { for (const x of a) this.parentNode && this.parentNode.insertBefore(typeof x === 'object' ? x : document.createTextNode(String(x)), this.nextSibling); };
EP.replaceWith = function (...a) { this.before(...a); this.remove(); };
EP.replaceChildren = function (...a) { while (this.firstChild) this.removeChild(this.firstChild); this.append(...a); };
EP.insertAdjacentHTML = function (pos, h) { const t = document.createElement('div'); t.innerHTML = h; const k = [...t.childNodes];
  if (pos === 'beforeend') k.forEach(c => this.appendChild(c)); else if (pos === 'afterbegin') k.reverse().forEach(c => this.insertBefore(c, this.firstChild));
  else if (pos === 'beforebegin') k.forEach(c => this.parentNode.insertBefore(c, this)); else if (pos === 'afterend') k.reverse().forEach(c => this.parentNode.insertBefore(c, this.nextSibling)); };
EP.insertAdjacentElement = function (pos, el) { if (pos === 'beforeend') this.appendChild(el); else if (pos === 'afterbegin') this.insertBefore(el, this.firstChild); else if (pos === 'beforebegin') this.parentNode.insertBefore(el, this); else this.parentNode.insertBefore(el, this.nextSibling); return el; };
EP.getBoundingClientRect = function () { return { x: 0, y: 0, width: 0, height: 0, top: 0, left: 0, right: 0, bottom: 0 }; };
EP.getClientRects = () => [];
EP.removeEventListener = function (t, f) { const l = this.__ev && this.__ev[t]; if (l) { const i = l.indexOf(f); if (i >= 0) l.splice(i, 1); } };
EP.toggleAttribute = function (n, force) { const on = force === undefined ? !this.hasAttribute(n) : !!force; if (on) this.setAttribute(n, ''); else this.removeAttribute(n); return on; };
EP.getAttributeNames = function () { return []; };
EP.animate = () => ({ cancel: NOP, finished: Promise.resolve(), onfinish: null });
Object.defineProperty(EP, 'classList', { get() {
  const el = this, get = () => el.className.split(/\s+/).filter(Boolean), set = a => { el.className = a.join(' '); };
  return { contains: c => get().includes(c), add(...c) { const a = get(); c.forEach(x => a.includes(x) || a.push(x)); set(a); }, remove(...c) { set(get().filter(x => !c.includes(x))); },
    toggle(c, force) { const has = get().includes(c), on = force === undefined ? !has : !!force; if (on && !has) this.add(c); else if (!on && has) this.remove(c); return on; },
    replace(a, b) { const l = get(), i = l.indexOf(a); if (i < 0) return false; l[i] = b; set(l); return true; }, item: i => get()[i] ?? null, get length() { return get().length; }, toString: () => el.className, forEach(f) { get().forEach(f); }, [Symbol.iterator]() { return get()[Symbol.iterator](); } }; } });
Object.defineProperty(EP, 'dataset', { get() { const el = this; const key = k => 'data-' + String(k).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
  return new Proxy({}, { get: (t, k) => typeof k === 'symbol' ? undefined : (el.hasAttribute(key(k)) ? el.getAttribute(key(k)) : undefined), set: (t, k, v) => { el.setAttribute(key(k), String(v)); return true; },
    has: (t, k) => el.hasAttribute(key(k)), deleteProperty: (t, k) => { el.removeAttribute(key(k)); return true; } }); } });
Object.defineProperty(EP, 'style', { get() { const el = this, kebab = k => String(k).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
  return new Proxy({}, { get(t, k) { if (typeof k === 'symbol') return undefined; if (k === 'setProperty') return (p, v) => __sset(el, p, String(v)); if (k === 'getPropertyValue') return p => __sget(el, p);
      if (k === 'removeProperty') return p => __sset(el, p, ''); if (k === 'cssText') return el.getAttribute('style') || ''; if (k === 'length') return 0; return __sget(el, kebab(k)); },
    set(t, k, v) { if (k === 'cssText') el.setAttribute('style', String(v)); else __sset(el, kebab(k), String(v)); return true; } }); }, set(v) { this.setAttribute('style', String(v)); } });
// ---- events ----
class Event { constructor(type, o = {}) { this.type = type; this.bubbles = !!o.bubbles; this.cancelable = !!o.cancelable; this.defaultPrevented = false; this.timeStamp = Date.now(); }
  preventDefault() { this.defaultPrevented = true; } stopPropagation() { this.__stop = true; } stopImmediatePropagation() { this.__stop = true; } }
class CustomEvent extends Event { constructor(t, o = {}) { super(t, o); this.detail = o.detail ?? null; } }
class KeyboardEvent extends Event { constructor(t, o = {}) { super(t, o); this.key = o.key || ''; this.code = o.code || ''; } }
class MouseEvent extends Event { constructor(t, o = {}) { super(t, o); this.clientX = o.clientX || 0; this.clientY = o.clientY || 0; this.button = o.button || 0; } }
Object.assign(globalThis, { Event, CustomEvent, KeyboardEvent, MouseEvent, InputEvent: Event, FocusEvent: Event, UIEvent: Event, PointerEvent: MouseEvent });
const __wl = {};
globalThis.addEventListener = (t, f) => { (__wl[t] ||= []).push(f); };
globalThis.removeEventListener = (t, f) => { const l = __wl[t]; if (l) { const i = l.indexOf(f); if (i >= 0) l.splice(i, 1); } };
globalThis.dispatchEvent = e => { __fire(e.type, e); return !e.defaultPrevented; };
globalThis.__fire = (t, e = { type: t, target: window }) => { for (const f of (__wl[t] || []).slice()) { try { f.call(window, e); } catch (x) { console.error(String(x)); } }
  if (typeof window['on' + t] === 'function') { try { window['on' + t](e); } catch (x) { console.error(String(x)); } } };
// ---- timers & misc ----
globalThis.queueMicrotask = f => { Promise.resolve().then(f); };
globalThis.requestAnimationFrame = f => setTimeout(() => f(performance.now()), 16);
globalThis.cancelAnimationFrame = i => clearTimeout(i);
globalThis.requestIdleCallback = f => setTimeout(() => f({ didTimeout: false, timeRemaining: () => 10 }), 1);
globalThis.cancelIdleCallback = i => clearTimeout(i);
const __t0 = Date.now();
globalThis.performance = { now: () => Date.now() - __t0, mark: NOP, measure: NOP, timeOrigin: __t0, getEntriesByName: () => [], getEntriesByType: () => [] };
globalThis.structuredClone = v => v === undefined ? v : JSON.parse(JSON.stringify(v));
globalThis.getComputedStyle = el => el.style;
globalThis.matchMedia = q => ({ matches: /prefers-reduced-motion/.test(q) ? false : /min-width:\s*(\d+)/.test(q) ? 1000 >= +RegExp.$1 : /max-width:\s*(\d+)/.test(q) ? 1000 <= +RegExp.$1 : false, media: q, addEventListener: NOP, removeEventListener: NOP, addListener: NOP, removeListener: NOP });
class Obs { constructor(cb) { this.cb = cb; } observe() {} unobserve() {} disconnect() {} takeRecords() { return []; } }
Object.assign(globalThis, { MutationObserver: Obs, IntersectionObserver: Obs, ResizeObserver: Obs, PerformanceObserver: Obs });
globalThis.btoa = s => { const c = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; s = String(s); let o = '';
  for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), d = s.charCodeAt(i + 2); o += c[a >> 2] + c[((a & 3) << 4) | (b >> 4 || 0)] + (isNaN(b) ? '=' : c[((b & 15) << 2) | (d >> 6 || 0)]) + (isNaN(d) ? '=' : c[d & 63]); } return o; };
globalThis.atob = s => { const c = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; s = String(s).replace(/=+$/, ''); let o = '', b = 0, n = 0;
  for (const ch of s) { const v = c.indexOf(ch); if (v < 0) continue; b = (b << 6) | v; n += 6; if (n >= 8) { n -= 8; o += String.fromCharCode((b >> n) & 255); } } return o; };
globalThis.crypto = { getRandomValues: a => { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 4294967296); return a; }, randomUUID: () => 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }) };
globalThis.TextEncoder = class { encode(s) { const e = unescape(encodeURIComponent(s)), a = new Uint8Array(e.length); for (let i = 0; i < e.length; i++) a[i] = e.charCodeAt(i); return a; } };
globalThis.TextDecoder = class { decode(a) { let s = ''; for (const b of a || []) s += String.fromCharCode(b); try { return decodeURIComponent(escape(s)); } catch { return s; } } };
// ---- storage, navigator, screen, location, history ----
const makeStore = () => { const d = new Map(); return { getItem: k => d.has(String(k)) ? d.get(String(k)) : null, setItem: (k, v) => { d.set(String(k), String(v)); }, removeItem: k => { d.delete(String(k)); }, clear: () => d.clear(), key: i => [...d.keys()][i] ?? null, get length() { return d.size; } }; };
globalThis.localStorage = makeStore(); globalThis.sessionStorage = makeStore();
globalThis.navigator = { userAgent: 'Mozilla/5.0 (X11; MiaouOS) AppleWebKit/537.36 (KHTML, like Gecko) MiaouWeb/2.0', language: 'fr-FR', languages: ['fr-FR', 'fr', 'en'], platform: 'MiaouOS', cookieEnabled: false, onLine: true, hardwareConcurrency: 2, clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') }, sendBeacon: () => true, serviceWorker: undefined };
globalThis.screen = { width: 1280, height: 800, availWidth: 1280, availHeight: 760, colorDepth: 24, orientation: { type: 'landscape-primary' } };
Object.assign(globalThis, { innerWidth: 1000, innerHeight: 600, outerWidth: 1000, outerHeight: 700, devicePixelRatio: 1, scrollX: 0, scrollY: 0, pageXOffset: 0, pageYOffset: 0 });
globalThis.scrollTo = globalThis.scroll = globalThis.scrollBy = NOP; globalThis.focus = globalThis.blur = globalThis.print = globalThis.close = globalThis.stop = NOP;
globalThis.open = u => { __nav(String(u)); return null; };
class URLSearchParams { constructor(i = '') { this.l = []; if (typeof i === 'string') { i.replace(/^\?/, '').split('&').filter(Boolean).forEach(p => { const [k, ...v] = p.split('='); this.l.push([decodeURIComponent(k.replace(/\+/g, ' ')), decodeURIComponent(v.join('=').replace(/\+/g, ' '))]); }); } else if (i && typeof i === 'object') { (Array.isArray(i) ? i : Object.entries(i)).forEach(([k, v]) => this.l.push([String(k), String(v)])); } }
  get(k) { const e = this.l.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this.l.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this.l.some(x => x[0] === k); }
  set(k, v) { const i = this.l.findIndex(x => x[0] === k); if (i < 0) this.l.push([k, String(v)]); else { this.l[i][1] = String(v); this.l = this.l.filter((x, j) => x[0] !== k || j === i); } } append(k, v) { this.l.push([k, String(v)]); }
  delete(k) { this.l = this.l.filter(x => x[0] !== k); } forEach(f) { this.l.forEach(([k, v]) => f(v, k, this)); } keys() { return this.l.map(x => x[0])[Symbol.iterator](); } values() { return this.l.map(x => x[1])[Symbol.iterator](); } entries() { return this.l[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }
  toString() { return this.l.map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&').replace(/%20/g, '+'); } }
class URL { constructor(u, base) { u = String(u); const m = /^([a-z][a-z0-9+.-]*:)?(\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(u);
    if (!m || (!m[1] && base === undefined)) throw new TypeError('Invalid URL: ' + u);
    if (!m[1]) { const b = new URL(base); const path = m[2] ? m[4] : (m[4].startsWith('/') ? m[4] : b.pathname.replace(/[^/]*$/, '') + m[4]); return new URL(b.protocol + '//' + b.host + (path || b.pathname) + (m[2] || m[4] ? (m[5] || '') : (m[5] || b.search)) + (m[6] || '')); }
    this.protocol = m[1]; this.host = m[3] || ''; const hp = this.host.split(':'); this.hostname = hp[0]; this.port = hp[1] || ''; this.pathname = m[4] || '/'; this.search = m[5] || ''; this.hash = m[6] || '';
    this.origin = this.protocol + '//' + this.host; this.searchParams = new URLSearchParams(this.search); this.href = this.origin + this.pathname + this.search + this.hash; }
  toString() { return this.href; } toJSON() { return this.href; } static canParse(u, b) { try { new URL(u, b); return true; } catch { return false; } } }
Object.assign(globalThis, { URL, URLSearchParams });
const __loc = () => { try { return new URL(__href()); } catch { return { protocol: 'about:', host: '', hostname: '', port: '', pathname: '', search: '', hash: '', origin: 'null' }; } };
globalThis.location = { get href() { return __href(); }, set href(v) { __nav(String(v)); }, assign: v => __nav(String(v)), replace: v => __nav(String(v)), reload: () => __navcode(4), toString: () => __href(),
  get protocol() { return __loc().protocol; }, get host() { return __loc().host; }, get hostname() { return __loc().hostname; }, get port() { return __loc().port; }, get pathname() { return __loc().pathname; }, get search() { return __loc().search; }, get hash() { return __loc().hash; }, get origin() { return __loc().origin; } };
globalThis.history = { back: () => __navcode(2), forward: () => __navcode(3), go: n => __navcode(n < 0 ? 2 : 3), length: 1, state: null, pushState: NOP, replaceState: NOP, scrollRestoration: 'auto' };
// ---- network: fetch / XMLHttpRequest on top of the synchronous native loader (answered asynchronously) ----
class Headers { constructor(i) { this.m = new Map(); if (i) Object.entries(i).forEach(([k, v]) => this.m.set(k.toLowerCase(), String(v))); } get(k) { return this.m.get(String(k).toLowerCase()) ?? null; } set(k, v) { this.m.set(String(k).toLowerCase(), String(v)); } has(k) { return this.m.has(String(k).toLowerCase()); } append(k, v) { this.set(k, v); } delete(k) { this.m.delete(String(k).toLowerCase()); } forEach(f) { this.m.forEach((v, k) => f(v, k)); } entries() { return this.m.entries(); } }
class Response { constructor(body = '', o = {}) { this._b = body; this.status = o.status ?? 200; this.ok = this.status >= 200 && this.status < 300; this.statusText = o.statusText || ''; this.headers = new Headers(o.headers); this.url = o.url || ''; this.type = 'basic'; this.redirected = false; this.bodyUsed = false; }
  text() { return Promise.resolve(String(this._b)); } json() { return Promise.resolve().then(() => JSON.parse(this._b)); } blob() { return Promise.resolve({ size: String(this._b).length, type: '' }); } arrayBuffer() { return Promise.resolve(new TextEncoder().encode(this._b).buffer); } clone() { return new Response(this._b, { status: this.status, url: this.url }); } }
class Request { constructor(u, o = {}) { this.url = String(u && u.url || u); this.method = o.method || 'GET'; this.headers = new Headers(o.headers); this.body = o.body; } }
Object.assign(globalThis, { Headers, Response, Request });
globalThis.fetch = (input, o = {}) => new Promise((res, rej) => setTimeout(() => { const r = __fetch(String(input && input.url || input));
  if (r === null) rej(new TypeError('Failed to fetch')); else res(new Response(r, { status: 200, url: String(input && input.url || input) })); }, 0));
class XMLHttpRequest { constructor() { this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = ''; this.responseType = ''; this.timeout = 0; }
  open(m, u) { this.method = m; this.url = String(u); this.readyState = 1; } setRequestHeader() {} getResponseHeader() { return null; } getAllResponseHeaders() { return ''; } abort() {} overrideMimeType() {}
  addEventListener(t, f) { (this['on' + t] ||= f); }
  send() { setTimeout(() => { const r = __fetch(this.url); this.readyState = 4; if (r === null) { this.status = 0; this.onerror && this.onerror({ type: 'error' }); }
      else { this.status = 200; this.statusText = 'OK'; this.responseText = r; this.response = this.responseType === 'json' ? JSON.parse(r) : r; this.onreadystatechange && this.onreadystatechange({}); this.onload && this.onload({ type: 'load' }); } this.onloadend && this.onloadend({}); }, 0); } }
globalThis.XMLHttpRequest = XMLHttpRequest;
globalThis.FormData = class { constructor() { this.l = []; } append(k, v) { this.l.push([k, v]); } get(k) { const e = this.l.find(x => x[0] === k); return e ? e[1] : null; } };
globalThis.AbortController = class { constructor() { this.signal = { aborted: false, addEventListener() {} }; } abort() { this.signal.aborted = true; } };
globalThis.Image = class { set src(v) { this._s = v; setTimeout(() => this.onload && this.onload(), 0); } get src() { return this._s; } };
globalThis.Audio = class { play() { return Promise.resolve(); } pause() {} };
globalThis.WebSocket = class { constructor() { setTimeout(() => this.onerror && this.onerror({}), 0); } send() {} close() {} };
document.addEventListener('DOMContentLoaded', NOP);
