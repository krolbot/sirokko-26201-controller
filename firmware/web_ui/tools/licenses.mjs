import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
const sha=b=>createHash('sha256').update(b).digest('hex');
const dependencies=['svelte','clsx','esm-env','tailwindcss','daisyui','@fontsource/ibm-plex-sans'];
const notices=[]; const versions={};
for(const name of dependencies){
 const base=`node_modules/${name}/`;
 const metadata=JSON.parse(await readFile(base+'package.json','utf8'));
 const file=['LICENSE','LICENSE.md','license','license.md'].find(n=>existsSync(base+n));
 if(!file)throw new Error(`Missing license for ${name}`);
 versions[name]=metadata.version;
 notices.push(`${name} ${metadata.version} (${metadata.license})\n${await readFile(base+file,'utf8')}`);
}
const notice=notices.join('\n\n========================================\n\n')+'\n\n========================================\n\n'+`js-sha256 0.11.1 (MIT)\n${await readFile('licenses/js-sha256-MIT.txt','utf8')}`;
if(/<\/script/i.test(notice))throw new Error('Unsafe notice delimiter');
const raw=await readFile('dist/index.html','utf8');
const html=raw.replace('</body>',`<script id="third-party-notices" type="text/plain">${notice}</script></body>`);
await writeFile('dist/index.html',html);
await writeFile('THIRD_PARTY_NOTICES.txt',notice.trimEnd()+'\n');
const sources={};
for(const file of ['package.json','package-lock.json','vite.config.js','index.html','src/main.js','src/App.svelte','src/Icon.svelte','src/style.css','src/lib/api.js','src/lib/controller.js','src/lib/protocol.js','src/lib/sha256.js','src/vendor/js-sha256.js','licenses/js-sha256-MIT.txt','tools/licenses.mjs','tools/verify-dist.mjs'])sources[file]=sha(await readFile(file));
await mkdir('evidence',{recursive:true});
await writeFile('evidence/build.json',JSON.stringify({built_at:new Date().toISOString(),html_sha256:sha(Buffer.from(html)),html_bytes:Buffer.byteLength(html),versions,sources},null,2)+'\n');
console.log(`Local licenses preserved; HTML ${Buffer.byteLength(html)} bytes; SHA256 ${sha(Buffer.from(html))}`);
