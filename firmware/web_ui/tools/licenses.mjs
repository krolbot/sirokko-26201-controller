import { readFile, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';

const dependencies = ['svelte', 'clsx', 'esm-env'];
const notices = [];

for (const name of dependencies) {
  const base = `node_modules/${name}/`;
  const metadata = JSON.parse(await readFile(base + 'package.json', 'utf8'));
  const file = ['LICENSE', 'LICENSE.md', 'license', 'license.md'].find(
    (candidate) => existsSync(base + candidate),
  );
  if (!file) throw new Error(`Missing license for ${name}`);
  notices.push(`${name} ${metadata.version} (${metadata.license})\n${await readFile(base + file, 'utf8')}`);
}

notices.push(`js-sha256 0.11.1 (MIT)\n${await readFile('licenses/js-sha256-MIT.txt', 'utf8')}`);
const notice = notices.join('\n\n========================================\n\n').replace(/\r\n?/g, '\n');
if (/<\/script/i.test(notice)) throw new Error('Unsafe notice delimiter');

const raw = (await readFile('dist/index.html', 'utf8')).replace(/\r\n?/g, '\n');
const html = raw.replace('</body>', `<script id="third-party-notices" type="text/plain">${notice}</script></body>`);
await writeFile('dist/index.html', html);
await writeFile('THIRD_PARTY_NOTICES.txt', notice.trimEnd() + '\n');
console.log(`Third-party notices included in dist/index.html (${Buffer.byteLength(html)} bytes)`);
