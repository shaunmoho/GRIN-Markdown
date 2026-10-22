// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

// Renderer page for the GRIN-Markdown Notepad++ plugin.
//
// The native host (WebView2) talks to this page with JSON web messages:
//   host -> page  { type: "render", text, line | anchor, docBase, theme }
//                 { type: "placeholder", text, theme }   current file is not Markdown
//                 { type: "scroll", line }
//                 { type: "theme", theme }          theme: "light" | "dark"
//   page -> host  { type: "ready" }
//                 { type: "link", href }            link the page did not handle itself
//                 { type: "gotoLine", line }        double-click in the preview

import MarkdownIt from 'markdown-it';
import { full as emoji } from 'markdown-it-emoji';
import footnote from 'markdown-it-footnote';
import taskLists from 'markdown-it-task-lists';
import hljs from 'highlight.js/lib/common';
import DOMPurify from 'dompurify';

const host = window.chrome && window.chrome.webview;
const post = (msg) => host && host.postMessage(msg);

const content = document.getElementById('content');
// Captured before any <base> is inserted: relative URLs later resolve against the document's folder.
const APP_BASE = new URL('.', document.currentScript?.src || location.href).href;
let currentTheme = 'light';
let docBase = '';

// ---------------------------------------------------------------------------
// Markdown pipeline
// ---------------------------------------------------------------------------

const escapeHtml = (s) => s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const md = new MarkdownIt({
  html: true,
  linkify: true,
  typographer: false,
  highlight(code, lang) {
    const language = (lang || '').trim().split(/\s+/)[0].toLowerCase();
    if (language && hljs.getLanguage(language)) {
      try {
        return hljs.highlight(code, { language, ignoreIllegals: true }).value;
      } catch { /* fall through to plain text */ }
    }
    return escapeHtml(code);
  },
})
  .use(emoji)
  .use(footnote)
  .use(taskLists, { enabled: false, label: true })
  .use(githubAlerts)
  .use(headingAnchors)
  .use(sourceLines);

// GitHub only treats ~~double~~ tildes as strikethrough; markdown-it agrees. Nothing to do.

const defaultFence = md.renderer.rules.fence;
md.renderer.rules.fence = (tokens, idx, options, env, self) => {
  const token = tokens[idx];
  const lang = token.info.trim().split(/\s+/)[0].toLowerCase();
  if (lang === 'mermaid') {
    const line = token.map ? ` data-line="${token.map[0]}"` : '';
    return `<div class="mermaid-block"${line}><pre class="mermaid-src">${escapeHtml(token.content)}</pre></div>\n`;
  }
  return defaultFence(tokens, idx, options, env, self);
};

// Add data-line="<0-based source line>" to every block that has a source map,
// so the preview can follow the editor's scroll position.
function sourceLines(mdi) {
  mdi.core.ruler.push('source_lines', (state) => {
    for (const token of state.tokens) {
      if (token.map && token.nesting !== -1 && token.block && token.type !== 'fence') {
        token.attrSet('data-line', String(token.map[0]));
      }
    }
  });
}

// GitHub-style heading ids ("## Hello World!" -> id="hello-world").
function headingAnchors(mdi) {
  mdi.core.ruler.push('heading_anchors', (state) => {
    const seen = new Map();
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length; i++) {
      if (tokens[i].type !== 'heading_open') continue;
      const text = tokens[i + 1].children
        .filter((t) => t.type === 'text' || t.type === 'code_inline' || t.type === 'emoji')
        .map((t) => t.content)
        .join('');
      let slug = text.trim().toLowerCase()
        .replace(/[^\p{L}\p{N}\s_-]/gu, '')
        .replace(/\s/g, '-');
      const n = seen.get(slug) || 0;
      seen.set(slug, n + 1);
      if (n) slug += `-${n}`;
      tokens[i].attrSet('id', slug);
    }
  });
}

// GitHub alerts: "> [!NOTE]" / TIP / IMPORTANT / WARNING / CAUTION.
const ALERT_ICONS = {
  note: '<path d="M0 8a8 8 0 1 1 16 0A8 8 0 0 1 0 8Zm8-6.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13ZM6.5 7.75A.75.75 0 0 1 7.25 7h1a.75.75 0 0 1 .75.75v2.75h.25a.75.75 0 0 1 0 1.5h-2a.75.75 0 0 1 0-1.5h.25v-2h-.25a.75.75 0 0 1-.75-.75ZM8 6a1 1 0 1 1 0-2 1 1 0 0 1 0 2Z"/>',
  tip: '<path d="M8 1.5c-2.363 0-4 1.69-4 3.75 0 .984.424 1.625.984 2.304l.214.253c.223.264.47.556.673.848.284.411.537.896.621 1.49a.75.75 0 0 1-1.484.211c-.04-.282-.163-.547-.37-.847a8.456 8.456 0 0 0-.542-.68c-.084-.1-.173-.205-.268-.32C3.201 7.75 2.5 6.766 2.5 5.25 2.5 2.31 4.863 0 8 0s5.5 2.31 5.5 5.25c0 1.516-.701 2.5-1.328 3.259-.095.115-.184.22-.268.319-.207.245-.383.453-.541.681-.208.3-.33.565-.37.847a.751.751 0 0 1-1.485-.212c.084-.593.337-1.078.621-1.489.203-.292.45-.584.673-.848.075-.088.147-.173.213-.253.561-.679.985-1.32.985-2.304 0-2.06-1.637-3.75-4-3.75ZM5.75 12h4.5a.75.75 0 0 1 0 1.5h-4.5a.75.75 0 0 1 0-1.5ZM6 15.25a.75.75 0 0 1 .75-.75h2.5a.75.75 0 0 1 0 1.5h-2.5a.75.75 0 0 1-.75-.75Z"/>',
  important: '<path d="M0 1.75C0 .784.784 0 1.75 0h12.5C15.216 0 16 .784 16 1.75v9.5A1.75 1.75 0 0 1 14.25 13H8.06l-2.573 2.573A1.458 1.458 0 0 1 3 14.543V13H1.75A1.75 1.75 0 0 1 0 11.25Zm1.75-.25a.25.25 0 0 0-.25.25v9.5c0 .138.112.25.25.25h2a.75.75 0 0 1 .75.75v2.19l2.72-2.72a.749.749 0 0 1 .53-.22h6.5a.25.25 0 0 0 .25-.25v-9.5a.25.25 0 0 0-.25-.25Zm7 2.25v2.5a.75.75 0 0 1-1.5 0v-2.5a.75.75 0 0 1 1.5 0ZM9 9a1 1 0 1 1-2 0 1 1 0 0 1 2 0Z"/>',
  warning: '<path d="M6.457 1.047c.659-1.234 2.427-1.234 3.086 0l6.082 11.378A1.75 1.75 0 0 1 14.082 15H1.918a1.75 1.75 0 0 1-1.543-2.575Zm1.763.707a.25.25 0 0 0-.44 0L1.698 13.132a.25.25 0 0 0 .22.368h12.164a.25.25 0 0 0 .22-.368Zm.53 3.996v2.5a.75.75 0 0 1-1.5 0v-2.5a.75.75 0 0 1 1.5 0ZM9 11a1 1 0 1 1-2 0 1 1 0 0 1 2 0Z"/>',
  caution: '<path d="M4.47.22A.749.749 0 0 1 5 0h6c.199 0 .389.079.53.22l4.25 4.25c.141.14.22.331.22.53v6a.749.749 0 0 1-.22.53l-4.25 4.25A.749.749 0 0 1 11 16H5a.749.749 0 0 1-.53-.22L.22 11.53A.749.749 0 0 1 0 11V5c0-.199.079-.389.22-.53Zm.84 1.28L1.5 5.31v5.38l3.81 3.81h5.38l3.81-3.81V5.31L10.69 1.5ZM8 4a.75.75 0 0 1 .75.75v3.5a.75.75 0 0 1-1.5 0v-3.5A.75.75 0 0 1 8 4Zm0 8a1 1 0 1 1 0-2 1 1 0 0 1 0 2Z"/>',
};

function githubAlerts(mdi) {
  const marker = /^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\][ \t]*(?:\n|$)/i;
  mdi.core.ruler.after('block', 'github_alerts', (state) => {
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length; i++) {
      if (tokens[i].type !== 'blockquote_open') continue;
      // Only a blockquote whose first child is a paragraph starting with the marker.
      const para = tokens[i + 1];
      const inline = tokens[i + 2];
      if (!para || para.type !== 'paragraph_open' || !inline || inline.type !== 'inline') continue;
      const m = inline.content.match(marker);
      if (!m) continue;
      const kind = m[1].toLowerCase();
      // Find the matching close.
      let depth = 0, j = i;
      for (; j < tokens.length; j++) {
        if (tokens[j].type === 'blockquote_open') depth++;
        else if (tokens[j].type === 'blockquote_close' && --depth === 0) break;
      }
      tokens[i].tag = 'div';
      tokens[i].attrJoin('class', `markdown-alert markdown-alert-${kind}`);
      if (j < tokens.length) tokens[j].tag = 'div';
      inline.content = inline.content.slice(m[0].length);
      const title = new state.Token('html_block', '', 0);
      title.content = `<p class="markdown-alert-title"><svg class="octicon" viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">${ALERT_ICONS[kind]}</svg>${kind[0].toUpperCase()}${kind.slice(1)}</p>\n`;
      tokens.splice(i + 1, 0, title);
      // An alert containing only the marker leaves an empty paragraph; drop it.
      if (!inline.content.trim()) tokens.splice(i + 2, 3);
    }
  });
}

// YAML front matter is shown as a table, like GitHub does.
function splitFrontMatter(text) {
  const m = text.match(/^---[ \t]*\r?\n([\s\S]*?)\r?\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$)/);
  if (!m) return { front: '', body: text, offset: 0 };
  const offset = m[0].split('\n').length - (m[0].endsWith('\n') ? 1 : 0);
  return { front: m[1], body: text.slice(m[0].length), offset };
}

function renderFrontMatter(yaml) {
  const rows = [];
  for (const line of yaml.split(/\r?\n/)) {
    const m = line.match(/^([A-Za-z0-9_.-]+)\s*:\s*(.*)$/);
    if (m) rows.push(`<tr><td>${escapeHtml(m[1])}</td><td>${escapeHtml(m[2])}</td></tr>`);
    else if (rows.length && line.trim()) {
      rows[rows.length - 1] = rows[rows.length - 1].replace(/<\/td><\/tr>$/, `<br>${escapeHtml(line.trim())}</td></tr>`);
    }
  }
  return rows.length ? `<table class="front-matter" data-line="0"><tbody>${rows.join('')}</tbody></table>\n` : '';
}

// ---------------------------------------------------------------------------
// Mermaid (loaded lazily: it is large and most documents have no diagrams)
// ---------------------------------------------------------------------------

let mermaidLoad = null;
const svgCache = new Map(); // `${theme}\n${source}` -> svg markup | { error }
let diagramSeq = 0;

function loadMermaid() {
  if (!mermaidLoad) {
    mermaidLoad = new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = new URL('mermaid.min.js', APP_BASE).href;
      s.onload = () => resolve(window.mermaid);
      s.onerror = () => reject(new Error('Could not load mermaid.min.js'));
      document.head.appendChild(s);
    });
  }
  return mermaidLoad;
}

let mermaidTheme = null;
async function renderDiagrams(generation) {
  const blocks = [...content.querySelectorAll('.mermaid-block')];
  if (!blocks.length) return;

  // Fill in everything we already have so re-renders don't flicker.
  const pending = [];
  for (const block of blocks) {
    const src = block.querySelector('.mermaid-src').textContent;
    const key = `${currentTheme}\n${src}`;
    if (svgCache.has(key)) showDiagram(block, svgCache.get(key));
    else pending.push({ block, src, key });
  }
  if (!pending.length) return;

  let mermaid;
  try {
    mermaid = await loadMermaid();
  } catch (e) {
    pending.forEach(({ block }) => showDiagram(block, { error: e.message }));
    return;
  }
  if (mermaidTheme !== currentTheme) {
    mermaid.initialize({
      startOnLoad: false,
      securityLevel: 'strict',
      theme: currentTheme === 'dark' ? 'dark' : 'default',
      fontFamily: getComputedStyle(content).fontFamily,
    });
    mermaidTheme = currentTheme;
  }

  for (const { block, src, key } of pending) {
    if (generation !== renderGeneration) return; // a newer render superseded us
    const id = `mermaid-diagram-${++diagramSeq}`;
    let result;
    try {
      const { svg } = await mermaid.render(id, src);
      result = svg;
    } catch (e) {
      result = { error: (e && (e.message || e.str)) || String(e) };
      document.getElementById(id)?.remove();
      document.getElementById(`d${id}`)?.remove();
    }
    if (svgCache.size > 200) svgCache.clear();
    svgCache.set(key, result);
    if (block.isConnected) showDiagram(block, result);
    else {
      // The DOM was replaced while we were rendering; update the equivalent block if it still exists.
      for (const b of content.querySelectorAll('.mermaid-block:not(.rendered)')) {
        if (b.querySelector('.mermaid-src').textContent === src) showDiagram(b, result);
      }
    }
  }
}

function showDiagram(block, result) {
  block.classList.add('rendered');
  let out = block.querySelector('.mermaid-out');
  if (!out) {
    out = document.createElement('div');
    out.className = 'mermaid-out';
    block.appendChild(out);
  }
  if (typeof result === 'string') {
    block.classList.remove('error');
    out.innerHTML = result;
  } else {
    block.classList.add('error');
    out.innerHTML = `<div class="mermaid-error"><strong>Mermaid error</strong><pre></pre></div>`;
    out.querySelector('pre').textContent = result.error;
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

let renderGeneration = 0;
let lastLine = 0;

DOMPurify.addHook('uponSanitizeAttribute', (node, data) => {
  // Keep our own data-line attributes and allow the task list checkboxes.
  if (data.attrName === 'data-line') data.forceKeepAttr = true;
});

function render(text) {
  const generation = ++renderGeneration;
  const { front, body, offset } = splitFrontMatter(text);
  const env = {};
  let html = md.render(body, env);
  if (offset) html = html.replace(/data-line="(\d+)"/g, (_, n) => `data-line="${+n + offset}"`);
  html = renderFrontMatter(front) + html;
  html = DOMPurify.sanitize(html, {
    ADD_ATTR: ['target'],
    FORBID_TAGS: ['style', 'form'],
  });
  content.innerHTML = html;
  for (const a of content.querySelectorAll('a[href]')) a.removeAttribute('target');
  renderDiagrams(generation);
}

// ---------------------------------------------------------------------------
// Scroll sync (editor -> preview)
// ---------------------------------------------------------------------------

function lineElements() {
  return [...content.querySelectorAll('[data-line]')]
    .map((el) => ({ el, line: +el.getAttribute('data-line') }))
    .filter((x) => x.el.offsetParent !== null || x.el === content);
}

function scrollToLine(line) {
  lastLine = line;
  const els = lineElements();
  if (!els.length) return;
  let prev = null, next = null;
  for (const x of els) {
    if (x.line <= line) prev = x;
    else { next = x; break; }
  }
  const top = (el) => el.getBoundingClientRect().top + window.scrollY;
  let y;
  if (!prev) y = 0;
  else if (!next) y = top(prev.el) + (line - prev.line) * 20;
  else {
    const ratio = (line - prev.line) / (next.line - prev.line);
    y = top(prev.el) + (top(next.el) - top(prev.el)) * ratio;
  }
  window.scrollTo({ top: Math.max(0, y - 8), behavior: 'instant' });
}

content.addEventListener('dblclick', (e) => {
  const el = e.target.closest('[data-line]');
  if (el) post({ type: 'gotoLine', line: +el.getAttribute('data-line') });
});

// ---------------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------------

function scrollToAnchor(id) {
  const target = document.getElementById(id) || document.getElementsByName(id)[0];
  target?.scrollIntoView();
}

function onLinkClick(e) {
  if (e.type === 'auxclick' && e.button !== 1) return;
  const a = e.target.closest('a[href]');
  if (!a) return;
  const href = a.getAttribute('href');
  e.preventDefault();
  if (href.startsWith('#')) {
    scrollToAnchor(decodeURIComponent(href.slice(1)));
    return;
  }
  post({ type: 'link', href });
}
document.addEventListener('click', onLinkClick);
document.addEventListener('auxclick', onLinkClick);

// ---------------------------------------------------------------------------
// Theme / host messages
// ---------------------------------------------------------------------------

function setTheme(theme) {
  theme = theme === 'dark' ? 'dark' : 'light';
  if (theme === currentTheme && document.documentElement.dataset.theme) return false;
  currentTheme = theme;
  document.documentElement.dataset.theme = theme;
  // Toggle via media rather than link.disabled: Chromium can ignore `disabled`
  // on a stylesheet that is still loading.
  for (const link of document.querySelectorAll('link[data-theme]')) {
    link.media = link.dataset.theme === theme ? 'all' : 'not all';
  }
  return true;
}

function setDocBase(base) {
  if (base === docBase) return;
  docBase = base;
  let el = document.querySelector('base');
  if (!el) {
    el = document.createElement('base');
    document.head.prepend(el);
  }
  // Without a document folder, relative URLs resolve against the app itself (and simply 404).
  el.href = base || location.href;
}

let lastText = null;
host?.addEventListener('message', (e) => {
  const msg = e.data;
  if (!msg || typeof msg !== 'object') return;
  switch (msg.type) {
    case 'render': {
      const themeChanged = msg.theme ? setTheme(msg.theme) : false;
      setDocBase(msg.docBase || '');
      if (msg.text !== lastText || themeChanged) {
        lastText = msg.text;
        render(msg.text);
      }
      if (msg.anchor) scrollToAnchor(msg.anchor);
      else if (typeof msg.line === 'number') scrollToLine(msg.line);
      break;
    }
    case 'placeholder':
      setTheme(msg.theme);
      lastText = null;
      content.innerHTML = '';
      content.appendChild(Object.assign(document.createElement('p'), { className: 'placeholder', textContent: msg.text }));
      window.scrollTo(0, 0);
      break;
    case 'scroll':
      scrollToLine(msg.line);
      break;
    case 'theme':
      if (setTheme(msg.theme) && lastText !== null) {
        render(lastText);
        scrollToLine(lastLine);
      }
      break;
  }
});

// Images and diagrams change the layout after the first scroll; re-apply it.
content.addEventListener('load', () => scrollToLine(lastLine), true);

setTheme(window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
post({ type: 'ready' });

// Standalone use (opened in a normal browser): render the demo text if present.
if (!host) {
  const demo = document.getElementById('demo');
  if (demo) render(demo.textContent);
}
