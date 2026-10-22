// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

// Bundles the renderer page into ./dist (copied next to the plugin DLL by the CMake build).
import { build } from 'esbuild';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';

const out = 'dist';
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

await build({
  entryPoints: ['src/main.js'],
  bundle: true,
  minify: true,
  format: 'iife',
  target: 'chrome110',
  legalComments: 'linked',
  outfile: `${out}/preview.js`,
});

const copies = {
  'src/index.html': 'index.html',
  'src/preview.css': 'preview.css',
  'node_modules/mermaid/dist/mermaid.min.js': 'mermaid.min.js',
  'node_modules/github-markdown-css/github-markdown-light.css': 'github-markdown-light.css',
  'node_modules/github-markdown-css/github-markdown-dark.css': 'github-markdown-dark.css',
  'node_modules/highlight.js/styles/github.min.css': 'hljs-github.css',
  'node_modules/highlight.js/styles/github-dark.min.css': 'hljs-github-dark.css',
};
for (const [from, to] of Object.entries(copies)) copyFileSync(from, `${out}/${to}`);
console.log('web assets written to', out);

// THIRD_PARTY_NOTICES.md (shipped next to the DLL) from the bundled packages' licenses.
const bundled = ['markdown-it', 'markdown-it-emoji', 'markdown-it-footnote', 'markdown-it-task-lists',
  'highlight.js', 'dompurify', 'mermaid', 'github-markdown-css'];
let notices = '# Third-party notices\n\nGRIN-Markdown is licensed under GPL-3.0-or-later (see LICENSE). It bundles the following components.\n' +
  'Licenses of Mermaid\'s own dependencies are embedded at the end of `web/mermaid.min.js`.\n';
for (const name of bundled) {
  const dir = `node_modules/${name}`;
  const pkg = JSON.parse(readFileSync(`${dir}/package.json`, 'utf8'));
  const file = readdirSync(dir).find((f) => /^licen[cs]e/i.test(f));
  notices += `\n## ${name} ${pkg.version} (${pkg.license})\n\n`;
  if (file) notices += '```\n' + readFileSync(`${dir}/${file}`, 'utf8').trim() + '\n```\n';
}
notices += '\n## Notepad++ plugin interface headers (GPL-3.0-or-later, compiled in)\n\n' +
  'PluginInterface.h, Notepad_plus_msgs.h, Docking.h, dockingResource.h and menuCmdID.h, ' +
  'Copyright (C) Don HO, from https://github.com/notepad-plus-plus/notepad-plus-plus. ' +
  'Licensed under the GNU General Public License v3 or later; see LICENSE.txt.\n';
const scintilla = '../third_party/npp/Scintilla-License.txt';
if (existsSync(scintilla)) {
  notices += '\n## Scintilla interface headers (compiled in)\n\n```\n' + readFileSync(scintilla, 'utf8').trim() + '\n```\n';
}
const wv2 = '../third_party/webview2/LICENSE.txt';
if (existsSync(wv2)) {
  notices += '\n## Microsoft Edge WebView2 SDK loader (statically linked)\n\n```\n' + readFileSync(wv2, 'utf8').trim() + '\n```\n';
}
writeFileSync('../THIRD_PARTY_NOTICES.md', notices);
console.log('wrote THIRD_PARTY_NOTICES.md');
