// Block Step page tests (local, headless Chromium via Playwright; the page runs as it does inside the plugin, with a
// stand-in for the plugin's bridge). Usage: node page_tests.js [path/to/index.html]  (exit code = number of failures)
//  1. SIZE: at every page size the plugin window can give (the page is laid out at 1370 x 835 and zoomed, so these are
//     the default window, the smallest and the biggest), every control is on screen and no two controls overlap.
//  2. FRESH START: two instances share the Mac's browser storage. Change the first (engine, GLITCH, PROG mood, MAKE ME A
//     TRACK, LINES); the second still starts at the factory defaults. Close both, open a new one: factory defaults.
//     A project's own saved state (what the plugin hands back) brings the changes back.
const path = require('path');
const { chromium } = require('playwright');
const FILE = 'file://' + path.resolve(process.argv[2] || path.join(__dirname, '..', 'data', 'index.html'));
let fails = 0;
const check = (ok, msg) => { console.log((ok ? '  ok   ' : '  FAIL ') + msg); if (!ok) fails++; };

const bridge = () => {   // window.__JUCE__ as the plugin gives it (Shared WebBrowserComponent native integration)
  const L = {}; window.__sent = [];
  window.__JUCE__ = { backend: { emitEvent: (id, o) => window.__sent.push([id, o]), addEventListener: (id, fn) => { L[id] = fn; } } };
  window.__fire = (id, o) => L[id] && L[id](o);
};
async function open(ctx, w = 1370, h = 835, restore = '') {
  let p;
  for (let t = 0; ; t++) {   // (a small test machine can run out of memory loading the 2 MB page: try again)
    p = await ctx.newPage(); await p.setViewportSize({ width: w, height: h });
    const errs = []; p.on('pageerror', e => errs.push(e.message)); p.errs = errs;
    try { await p.goto(FILE); break; } catch (e) { await p.close().catch(() => {}); if (t >= 3) throw e; await new Promise(r => setTimeout(r, 3000)); }
  } await p.waitForFunction(() => window.__sent && window.__sent.some(s => s[0] === 'bs_ready'), null, { timeout: 30000 });
  await p.evaluate(j => window.__fire('bs_restore', { json: j }), restore); await p.waitForTimeout(500);
  return p;
}
// (keys starting with _ are random internal ids, e.g. a pattern's _sid: not settings)
const norm = o => JSON.parse(JSON.stringify(o, (k, v) => k.startsWith('_') ? undefined : v));
const state = async p => { await p.waitForTimeout(700); return norm(await p.evaluate(() => stateNow())); };
const installStateNow = () => { window.stateNow = () => { const s = window.__sent.filter(x => x[0] === 'bs_state'); return s.length ? JSON.parse(s[s.length - 1][1].json) : null; }; };

async function layoutCheck(p, label, only) {
  const r = await p.evaluate((only) => {
    const vis = e => { const r = e.getBoundingClientRect(); if (r.width < 4 || r.height < 4) return false;
      for (let a = e; a; a = a.parentElement) { const st = getComputedStyle(a); if (st.display === 'none' || st.visibility === 'hidden' || +st.opacity === 0 || a.hidden) return false; if (a.classList && a.classList.contains('off')) return false; } return true; };
    const scroller = e => { for (let a = e.parentElement; a && a !== document.body; a = a.parentElement) { const s = getComputedStyle(a); if (/(auto|scroll)/.test(s.overflowY + s.overflowX) && (a.scrollHeight > a.clientHeight + 1 || a.scrollWidth > a.clientWidth + 1)) return a; } return null; };
    // the gear badges sit on the corner of their button on purpose (PIANO ⚙, SYNTH ⚙, CLASSIC ⚙, FRESH ⚙)
    const badge = e => e.classList.contains('gb') || /Set$/.test(e.id || '') || (e.classList.contains('icon') && !!e.closest('.engpair'));
    const els = [...document.querySelectorAll('button, input, select, .seg, .englab, #tmulBpm, .tmul .lab')].filter(vis);
    const textRect = e => { const r = document.createRange(); r.selectNodeContents(e); const q = r.getBoundingClientRect(); return q.width > 0 ? q : e.getBoundingClientRect(); };   // a label: where its text is
    const R = els.map(e => [e, e.matches('.lab, #tmulBpm') ? textRect(e) : e.getBoundingClientRect()]);
    const nm = e => (e.id ? '#' + e.id : e.tagName.toLowerCase() + '.' + [...e.classList].join('.')) + ' "' + (e.textContent || e.value || '').trim().slice(0, 18) + '"';
    const out = [], over = [], mine = e => !only || !!e.closest(only);
    const pageH = Math.max(innerHeight, document.documentElement.scrollHeight);   // (a phone: the page itself scrolls since the painted scenes, build 113)
    R.forEach(([e, r]) => { if (scroller(e) || !mine(e)) return; if (r.right > innerWidth + 1 || r.bottom + scrollY > pageH + 1 || r.left < -1 || r.top + scrollY < -1) out.push(nm(e)); });
    for (let i = 0; i < R.length; i++) for (let j = i + 1; j < R.length; j++) { const [a, ra] = R[i], [c, rc] = R[j];
      if (a.contains(c) || c.contains(a) || badge(a) || badge(c) || (!mine(a) && !mine(c))) continue;
      if (!!a.closest('.sheet') !== !!c.closest('.sheet')) continue;   // a pop-up window floats over the page on purpose
      const ix = Math.min(ra.right, rc.right) - Math.max(ra.left, rc.left), iy = Math.min(ra.bottom, rc.bottom) - Math.max(ra.top, rc.top);
      if (ix > 2 && iy > 2) { const sp = scroller(a) || scroller(c); if (sp) { const pr = sp.getBoundingClientRect(); if ([ra, rc].some(q => q.bottom < pr.top || q.top > pr.bottom)) continue; } over.push(nm(a) + ' X ' + nm(c)); } }
    return { n: R.length, out, over };
  }, only);
  check(r.out.length === 0, `${label}: all ${r.n} controls on screen` + (r.out.length ? ' -- off: ' + r.out.slice(0, 6).join(', ') : ''));
  check(r.over.length === 0, `${label}: no two controls overlap` + (r.over.length ? ' -- ' + r.over.slice(0, 6).join(', ') : ''));
}

(async () => {
  const b = await chromium.launch({ args: ['--disable-dev-shm-usage', '--disable-gpu'] });
  const shots = process.env.GH_SHOTS;
  console.log('SIZE');
  // window 1200 x 760 -> page 1188 x 724 at zoom 0.867 = 1370 x 835; smallest window 720 x 456 -> 1408 x 835; biggest 2560 x 1621 -> 1370 x 851
  // (owner 2026-11-13) the page now always sits at its own shape in the window (letterboxed), so it is always ~1390 x 847: the wide layout
  for (const [w, h, label] of [[1390, 847, 'every window size (locked wide layout)'], [1392, 847, 'rounding, a hair wider'], [1388, 847, 'rounding, a hair narrower']]) {
    const ctx = await b.newContext({ viewport: { width: w, height: h } }); await ctx.addInitScript(bridge); await ctx.addInitScript(installStateNow);
    const p = await open(ctx, w, h);
    await layoutCheck(p, label);
    for (const pg of ['pnoBtn', 'synBtn']) { const ok = await p.evaluate(id => { const e = document.getElementById(id); if (!e) return false; e.click(); return true; }, pg);
      if (ok) { await p.waitForTimeout(400); await layoutCheck(p, `${label}, ${pg === 'pnoBtn' ? 'PIANO' : 'SYNTH'} page`); } }
    if (shots && w === 1390 && h === 847) { await p.evaluate(() => document.getElementById('pnoBtn').click()); await p.waitForTimeout(300); await p.screenshot({ path: path.join(shots, 'blockstep-page-1390x847.png') }); }
    check(p.errs.length === 0, `${label}: no script errors` + (p.errs.length ? ' -- ' + p.errs[0] : ''));
    await ctx.close();
  }

  for (const [w, h, label] of [[1600, 900, 'web, desktop browser 1600 x 900'], [1100, 760, 'web, 1100 x 760'], [390, 844, 'web, phone 390 x 844']]) {
    const c2 = await b.newContext({ viewport: { width: w, height: h } }); const p = await c2.newPage(); const errs = []; p.on('pageerror', e => errs.push(e.message));
    for (let t = 0; ; t++) { try { await p.goto(FILE); break; } catch (e) { if (t >= 3) throw e; await new Promise(r => setTimeout(r, 3000)); } }
    await p.waitForTimeout(1500);
    // (a phone: from build 113 the page scrolls, every block at its own height, so the whole page is checked there too)
    await layoutCheck(p, label);
    if (shots) await p.screenshot({ path: path.join(shots, `blockstep-web-${w}x${h}.png`) });
    check(errs.length === 0, `${label}: no script errors` + (errs.length ? ' -- ' + errs[0] : '')); await c2.close(); }
  for (const w of [900, 960, 1024]) {   // a narrow desktop browser: the name row has little room, so SCENE shows a short label there (build 113)
    const c2 = await b.newContext({ viewport: { width: w, height: 760 } }); const p = await c2.newPage(); await p.goto(FILE); await p.waitForTimeout(1200);
    const r = await p.evaluate(() => { const q = s => document.querySelector(s).getBoundingClientRect(), br = document.querySelector('header.top .brand');
      return { cut: br.scrollWidth - br.clientWidth, gap: Math.round(q('.layers').left - q('#scnBtn').right), btn: document.getElementById('scnBtn').innerText.trim() }; });
    check(r.cut <= 0 && r.gap >= 0, `web, ${w} px wide: the name is not cut and SCENE does not run into DRUMS / PIANO / ACID (button "${r.btn}", ${r.gap} px clear)`); await c2.close(); }

  console.log('FRESH START');
  const ctx = await b.newContext({ viewport: { width: 1370, height: 835 } }); await ctx.addInitScript(bridge); await ctx.addInitScript(installStateNow);
  const A = await open(ctx); const F = await state(A);
  check(F && F.eng === 'classic' && F.apx === 0 && F.bpm === 150 && F.tmul === 1, 'a new instance hands the plugin the factory state (CLASSIC, GLITCH 0, 150 BPM, TEMPO 1x)');
  // change the first instance the way Brandon would
  await A.evaluate(() => { document.querySelector('#engSeg [data-v="fresh"]').click();
    const r = document.querySelector('.apxrow input'); r.value = 60; r.dispatchEvent(new Event('input'));
    document.getElementById('linesBtn').click(); });
  const SA = await state(A); console.log('  (first instance now: eng ' + SA.eng + ', apx ' + SA.apx + ')');
  check(SA.eng === 'fresh' && SA.apx === 60, 'the first instance changed (FRESH, GLITCH 60) and tells the plugin, so the project keeps it');
  const B = await open(ctx); const FB = await state(B);
  if (JSON.stringify(FB) !== JSON.stringify(F)) { const d = Object.keys(F).filter(k => JSON.stringify(F[k]) !== JSON.stringify(FB[k])); const walk = (a, b, pth) => { if (JSON.stringify(a) === JSON.stringify(b)) return; if (a && b && typeof a === 'object' && typeof b === 'object') { for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) walk(a[k], b[k], pth + '.' + k); } else console.log('  differs at', pth, JSON.stringify(a), '->', JSON.stringify(b)); }; walk(F, FB, ''); }
  check(JSON.stringify(FB) === JSON.stringify(F), 'a second instance at the same time starts at the factory state');
  check(await B.evaluate(() => document.querySelector('.apxrow input').value === '0' && document.querySelector('#engSeg [data-v="classic"]').classList.contains('on')),
    'its screen shows the factory settings (CLASSIC on, GLITCH 0)');
  await A.close(); await B.close();
  const C = await open(ctx); const FC = await state(C);
  check(JSON.stringify(FC) === JSON.stringify(F), 'close both and open a new one: the factory state again');
  await C.close();
  const D = await open(ctx, 1370, 835, JSON.stringify(SA));
  check(await D.evaluate(() => document.querySelector('#engSeg [data-v="fresh"]').classList.contains('on') && document.querySelector('.apxrow input').value === '60'), 'a project reopened with its saved state gets its own settings back (FRESH, GLITCH 60)');
  await D.close(); await ctx.close();
  console.log('TEMPO');
  { const ctx = await b.newContext({ viewport: { width: 1370, height: 835 } }); await ctx.addInitScript(bridge); await ctx.addInitScript(installStateNow);
    const T = await open(ctx);
    await T.evaluate(() => window.__fire('bs_pos', { playing: false, step: -1, frac: 0, host: false, bpm: 130, daw: 130 })); await T.waitForTimeout(200);
    const ui = () => T.evaluate(() => ({ on: [...document.querySelectorAll('#tmulSeg [data-m]')].filter(b => b.classList.contains('on')).map(b => b.textContent).join(), bpm: document.getElementById('tmulBpm').textContent, locked: document.getElementById('bpm').disabled, box: document.getElementById('bpm').value }));
    let u = await ui(); check(u.on === '1x' && u.bpm === '130 BPM', 'a new instance: TEMPO 1x, 130 BPM with the DAW at 130 (' + u.on + ', ' + u.bpm + ')');
    check(u.locked && u.box === '130', 'in the plugin the BPM box shows the DAW\'s tempo and is locked (no second tempo to get out of step)');
    for (const [m, want] of [['0.5', '65 BPM'], ['2', '260 BPM'], ['1', '130 BPM']]) {
      await T.evaluate(mm => document.querySelector(`#tmulSeg [data-m="${mm}"]`).click(), m); const st = await state(T); u = await ui();
      check(st.tmul === +m && u.bpm === want, `TEMPO ${m === '0.5' ? '½x' : m + 'x'}: shows ${want} and tells the plugin (tmul ${st.tmul})`); }
    await T.evaluate(() => document.querySelector('#tmulSeg [data-m="2"]').click()); const S2 = await state(T); await T.close();
    const N = await open(ctx); check((await state(N)).tmul === 1, 'a second instance still starts at 1x'); await N.close();
    const R = await open(ctx, 1370, 835, JSON.stringify(S2)); await R.evaluate(() => window.__fire('bs_pos', { daw: 130, bpm: 130 })); await R.waitForTimeout(150);
    check(await R.evaluate(() => document.querySelector('#tmulSeg [data-m="2"]').classList.contains('on') && document.getElementById('tmulBpm').textContent === '260 BPM'), 'a project saved at 2x reopens at 2x (260 BPM)');
    await R.close(); await ctx.close(); }
  // the web page (not the plugin) keeps its own browser memory as before
  const wctx = await b.newContext(); const W = await wctx.newPage(); await W.goto(FILE); await W.waitForTimeout(1500);
  await W.evaluate(() => document.querySelector('#engSeg [data-v="fresh"]').click()); await W.reload(); await W.waitForTimeout(1500);
  check(await W.evaluate(() => document.querySelector('#engSeg [data-v="fresh"]').classList.contains('on')), 'the web version (not the plugin) still remembers its engine in the browser, as before');
  await W.evaluate(() => document.querySelector('#tmulSeg [data-m="2"]').click());
  check(await W.evaluate(() => { const b = +document.getElementById('bpm').value; return !document.getElementById('bpm').disabled && document.getElementById('tmulBpm').textContent === (b * 2) + ' BPM'; }), 'the web version: TEMPO 2x = its own BPM x 2, and its BPM box still works');
  await wctx.close();
  console.log('SCENES');
  { // the two painted looks (build 113): SCENE switches them, the choice is a look (shared by every window, never in the project)
    const ctx = await b.newContext({ viewport: { width: 1370, height: 835 } }); await ctx.addInitScript(bridge); await ctx.addInitScript(installStateNow);
    const A = await open(ctx); const F = await state(A);
    const look = p => p.evaluate(() => ({ city: document.body.classList.contains('sc-city'), anime: document.body.classList.contains('sc-anime'), btn: (document.getElementById('scnBtn') || {}).textContent, cur: window.__scn && window.__scn.cur }));
    let L = await look(A); check(L.city && !L.anime && L.btn === 'SCENE: CITY', 'a new window opens in GHOST CITY, with the SCENE button (' + L.btn + ')');
    const drawn = p => p.evaluate(() => { const c = document.getElementById('scn'), g = c.getContext('2d'), d = g.getImageData(0, 0, c.width, c.height).data; let s = 0, n = 0, mn = 255, mx = 0; for (let i = 0; i < d.length; i += 4 * 997) { const v = (d[i] + d[i + 1] + d[i + 2]) / 3; s += v; n++; if (v < mn) mn = v; if (v > mx) mx = v; } return { avg: s / n, range: mx - mn }; });
    await A.waitForTimeout(1200); let D = await drawn(A); check(D.range > 120, 'the city painting is on the screen (brightness range ' + Math.round(D.range) + ')');
    await A.evaluate(() => { for (let i = 0; i < 12; i++) window.__scn.hit(i, 100); }); await A.waitForTimeout(200);
    await A.evaluate(() => document.getElementById('scnBtn').click()); await A.waitForTimeout(1500);
    L = await look(A); check(L.anime && !L.city && L.btn === 'SCENE: ANIME', 'SCENE switches to SPIRIT HILLS (' + L.btn + ')');
    D = await drawn(A); check(D.avg > 110, 'the painted world is on the screen (average brightness ' + Math.round(D.avg) + ')');
    const SA = await state(A); check(JSON.stringify(SA) === JSON.stringify(F), 'changing the scene changes nothing in the project (the beat, the sounds, every setting stay as they were)');
    // the third look: ORIGINAL (the Ghost Money look) is still there, drawn by the page's own untouched code
    await A.evaluate(() => document.getElementById('scnBtn').click()); await A.waitForTimeout(700);
    const O = await A.evaluate(() => ({ btn: document.getElementById('scnBtn').textContent, plain: !document.body.classList.contains('sc-city') && !document.body.classList.contains('sc-anime'),
      bg: getComputedStyle(document.getElementById('bg')).display !== 'none', scn: getComputedStyle(document.getElementById('scn')).display === 'none' }));
    check(O.btn === 'SCENE: ORIGINAL' && O.plain && O.bg && O.scn, 'SCENE then switches to ORIGINAL: the Ghost Money look, with its own background (' + O.btn + ')');
    check(JSON.stringify(await state(A)) === JSON.stringify(F), '...and the project is still untouched');
    await A.evaluate(() => document.getElementById('scnBtn').click()); await A.waitForTimeout(700); L = await look(A); check(L.city && L.btn === 'SCENE: CITY', '...and once more comes back round to GHOST CITY');
    await A.evaluate(() => document.getElementById('scnBtn').click()); await A.waitForTimeout(900);
    // the controls still do their jobs in the new look: GENERATE, LINEAR / WILD on, next, off
    const r = await A.evaluate(async () => { const $ = id => document.getElementById(id), w = ms => new Promise(r => setTimeout(r, ms)); const o = {};
      for (let i = 0; i < 12; i++) window.__scn.hit(i, 100);
      $('genBtn').click(); await w(300); $('linBtn').click(); await w(450); o.lin1 = $('linV').textContent; $('linNext').click(); await w(450); o.lin2 = $('linV').textContent; $('linPow').click(); await w(200); o.linOff = $('linPow').classList.contains('on');
      $('wildBtn').click(); await w(200); o.wild1 = $('wildV').textContent; $('wildPow').click(); await w(200); o.wildOff = $('wildPow').classList.contains('on'); return o; });
    check(!!r.lin1 && r.lin2 !== r.lin1 && !r.linOff, 'LINEAR turns on, steps to the next version and turns off (' + [r.lin1, r.lin2].join(' -> ').trim() + ')');
    check(!!r.wild1 && !r.wildOff, 'WILD turns on and off (' + String(r.wild1).trim() + ')');
    check(A.errs.length === 0, 'no script errors in either scene' + (A.errs.length ? ' -- ' + A.errs[0] : ''));
    const B = await open(ctx); L = await look(B); check(L.city && L.btn === 'SCENE: CITY', 'a second window opens on GHOST CITY again, whatever was picked last (build 117)');
    check(JSON.stringify(await state(B)) === JSON.stringify(F), '...and it is still at the factory state');
    await B.evaluate(() => document.getElementById('hackBtn').click()); await B.waitForTimeout(300);
    check(await B.evaluate(() => getComputedStyle(document.getElementById('scn')).display === 'none' && document.body.classList.contains('hack')), 'HACKER MODE still takes over the whole screen (the scene is hidden)');
    check(await B.evaluate(() => document.getElementById('scnBtn').disabled), '...and the SCENE button is greyed out there (it would do nothing)');
    await B.evaluate(() => document.getElementById('hackBtn').click()); await B.waitForTimeout(200);
    check(await B.evaluate(() => !document.getElementById('scnBtn').disabled && getComputedStyle(document.getElementById('scn')).display !== 'none'), 'EXIT HACKER brings the scene and its button back');
    await B.evaluate(() => window.__scn.set('city', true)); await B.waitForTimeout(200);
    await A.close(); await B.close(); await ctx.close(); }
  await b.close();
  console.log(fails ? `${fails} FAILED` : 'all page tests passed');
  process.exit(fails);
})().catch(e => { console.error('page tests crashed:', e.message); process.exit(99); });
