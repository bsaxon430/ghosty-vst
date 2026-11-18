// usage: node shot.js <index.html> <outPrefix> [plugin|web] [w] [h] [dpr] [extraJS]
const path = require('path');
const pw = require(process.env.PLAYWRIGHT || 'playwright');
(async () => {
  const [file, out, mode = 'plugin', w = '1370', h = '835', dpr = '1', extra = '', wait = '900', engine = 'chromium'] = process.argv.slice(2);
  const b = engine === 'webkit' ? await pw.webkit.launch() : await pw.chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--disable-dev-shm-usage', '--no-sandbox', '--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const ctx = await b.newContext({ viewport: { width: +w, height: +h }, deviceScaleFactor: +dpr });
  if (mode === 'plugin') await ctx.addInitScript(() => { const L = {}; window.__sent = []; window.__JUCE__ = { backend: { emitEvent: (id, o) => window.__sent.push([id, o]), addEventListener: (id, fn) => { L[id] = fn; } } }; window.__fire = (id, o) => L[id] && L[id](o); });
  if (engine === 'webkit') await ctx.addInitScript(() => { window.requestAnimationFrame = cb => setTimeout(() => cb(performance.now()), 16); });   // (headless WebKit here hardly runs animation frames: drive them with a timer)
  const p = await ctx.newPage(); const errs = []; p.on('pageerror', e => errs.push(e.message)); p.on('console', m => { if (m.type() === 'error') errs.push('console: ' + m.text().slice(0, 200)); });
  await p.goto('file://' + path.resolve(file));
  if (mode === 'plugin') { await p.waitForFunction(() => window.__sent && window.__sent.some(s => s[0] === 'bs_ready'), null, { timeout: 30000 }); await p.evaluate(() => window.__fire('bs_restore', { json: '' })); }
  await p.waitForTimeout(1800);
  if (extra) { await p.evaluate(extra); await p.waitForTimeout(+wait); }
  await p.screenshot({ path: out });
  console.log('saved', out, 'errors:', errs.length ? errs.slice(0, 5) : 'none');
  await b.close();
})().catch(e => { console.error('crashed:', e.message); process.exit(1); });
