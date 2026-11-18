// usage: node paint/render.js <painter.js> [args as JSON]   -> writes every canvas in window.out to out/<name>.png, window.meta to out/<painter>.json
const fs = require('fs'), path = require('path');
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
(async () => {
  const [painter, argJson = '{}', metaName = ''] = process.argv.slice(2);
  const b = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--disable-dev-shm-usage', '--no-sandbox', '--allow-file-access-from-files', '--disable-web-security'] });
  const p = await b.newPage(); p.on('pageerror', e => console.log('PAGE ERROR:', e.message)); p.on('console', m => console.log('  [page]', m.text().slice(0, 300)));
  await p.goto('file://' + path.resolve(__dirname, 'harness.html'));
  for (const f of painter.split(',')) await p.addScriptTag({ path: path.resolve(f) });
  const t0 = Date.now();
  const names = await p.evaluate(async (a) => { PT.initTex(); await window.main(JSON.parse(a)); return Object.keys(window.out); }, argJson);
  const outDir = path.resolve(__dirname, '..', 'out'); fs.mkdirSync(outDir, { recursive: true });
  for (const n of names) { const url = await p.evaluate(n => window.out[n].toDataURL('image/png'), n); fs.writeFileSync(path.join(outDir, n + '.png'), Buffer.from(url.split(',')[1], 'base64')); }
  const meta = await p.evaluate(() => window.meta || null); if (meta) fs.writeFileSync(path.join(outDir, (metaName || path.basename(painter.split(',').pop(), '.js')) + '.json'), JSON.stringify(meta));
  console.log('rendered', names.join(' '), 'in', ((Date.now() - t0) / 1000).toFixed(1) + 's');
  await b.close();
})().catch(e => { console.error('render crashed:', e.message); process.exit(1); });
