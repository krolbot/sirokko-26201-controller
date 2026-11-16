import { readFile } from 'node:fs/promises';

const html = await readFile('dist/index.html', 'utf8');
const failures = [];
if (!/<html[\s>]/i.test(html) || !/<\/html>/i.test(html)) failures.push('not a complete HTML document');
if (/<script\b[^>]*\bsrc\s*=/i.test(html)) failures.push('external script remains');
if (/<link\b[^>]*\brel=["']stylesheet["'][^>]*\bhref=/i.test(html)) failures.push('external stylesheet remains');
if (/(?:<script\b[^>]*\bsrc|<link\b[^>]*\bhref|<img\b[^>]*\bsrc)\s*=\s*["'](?:https?:)?\/\//i.test(html)) failures.push('external asset URL remains');
if (!/connect-src 'self'/.test(html) || !/font-src data:/.test(html)) failures.push('required CSP directives are missing');
if (failures.length) throw new Error(`dist verification failed: ${failures.join('; ')}`);
console.log(`PASS: self-contained dist/index.html (${Buffer.byteLength(html)} bytes)`);
