window.main = async () => { const tags = GH.GH_TAGS, n = tags.length, cols = 12, s = 120, L = PT.mk(cols * s, Math.ceil(n / cols) * s + 10); L.g.fillStyle = '#2b2440'; L.g.fillRect(0, 0, L.c.width, L.c.height);
  for (let i = 0; i < n; i++) { const img = await ghostImg(tags[i]); if (img) L.g.drawImage(img, (i % cols) * s + 6, Math.floor(i / cols) * s + 4, s - 12, s - 12); L.g.fillStyle = '#fff'; L.g.font = '12px sans-serif'; L.g.fillText(tags[i], (i % cols) * s + 6, Math.floor(i / cols) * s + s - 2); }
  console.log('fonts loaded: ' + await loadFonts('ゴーストブロックABC')); out.ghostsheet = L.c; };
