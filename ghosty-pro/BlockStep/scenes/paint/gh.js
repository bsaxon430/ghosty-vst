const GH=(()=>{
// Ghost drawings, ported from the GhostBlock dashboard (owner's ghost sheets, 2026-10-09).
// ES module. ghostSvg(tag, rank, size) returns an inline <svg> string; GH_TAGS lists every look.
// rank: 1 crown, 2 silver medal, 3 bronze medal, 'L' the biggest loser (sad face, tear, rain cloud), anything else plain.

function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}

const GH_CROWN='<g transform="translate(16,0)"><path d="M2 16 L8 4 L14 12 L16 1 L18 12 L24 4 L30 16 Z" fill="#F5C542"/><path d="M8 6 l2 5 h-4 z" fill="#E0A820"/><path d="M16 3 l3 7 h-6 z" fill="#FFE58A"/><path d="M24 6 l2 5 h-4 z" fill="#E0A820"/><rect x="2" y="15" width="28" height="3.5" rx="0.6" fill="#F5C542"/><rect x="2" y="18" width="28" height="1.4" fill="#C9922A"/></g>';
// One look per tag. f body colour; hem: g = green slime trim, d = drippy body;
// e: eye colour (or 'none'); face: angry / sleepy / smile / sad / x; a: accessories drawn on top.
const GW='#fff',GG='#5FB24A',GP='#6D45B8',GL='#9B74D2',GD='#5534A6',GK='#141414',GS='#C9C9CE';
const GH_LOOK={
  AxF:{f:GW,hem:'g',e:'#4C1D95',a:['band']}, F6X:{f:GW,hem:'g',e:'#4C1D95',a:['band','phones']}, '8Hu':{f:GW,hem:'g',e:'#4C1D95',a:['band','pixel']},
  BZZ:{f:GW,hem:'g',e:'#4C1D95',a:['band','usd']}, Gme:{f:GW,hem:'g',e:'#4C1D95',a:['hat']}, CSv:{f:GW,hem:'g',e:'#4C1D95',a:['check']},
  '8SE':{f:GW,hem:'g',e:'#4C1D95',a:['band','arrows']}, AoC:{f:GW,hem:'g',e:'#4C1D95',a:['pack']}, QfT:{f:GW,hem:'g',e:'#4C1D95',a:['cap']},
  BuB:{f:GW,e:GK,a:['gum']}, '4hi':{f:GW,e:GK,a:['slime']}, CKQ:{f:GW,e:'#4C1D95',face:'angry'}, '6t6':{f:GW,e:GK,shape:'comet'}, FPN:{f:GW,e:GK,a:['wband']},
  Ghs:{f:GW,e:GK,a:['shades']}, BDs:{f:GW,e:GK,a:['bow']}, '4gF':{f:GW,e:GK,a:['solshirt','pixelk']}, '8DE':{f:GW,e:GK,a:['patch']},
  BRn:{f:GW,e:'none',a:['prop']}, Gyq:{f:GG,hem:'d',e:GK}, EQd:{f:GP,e:GK,face:'sad'}, '3nK':{f:GW,e:'none',shape:'flame'}, '6Sj':{f:GK,e:GW,face:'angry',a:['ninja']},
  '4zJ':{f:GG,e:GK,face:'sleepy'}, frH:{f:GP,e:'none'}, DZw:{f:GW,e:GK,face:'x'}, Amg:{f:GS,e:GK,face:'angry'},
  FRk:{f:GW,e:GK,a:['blush']}, GUP:{f:GG,e:GK,a:['dots']}, '6Fe':{f:GL,e:GK,face:'angry'}, '9L4':{f:GW,e:GK,a:['blur']},
  '6G1':{f:GK,e:GW,face:'angry',a:['hood']}, '7ij':{f:GG,e:GK,face:'smile'}, J3E:{f:GW,e:GK,a:['halo']}, DiN:{f:GD,e:GK},
  '5CX':{f:'#5BD7F5',hem:'d',e:'none',a:['vlines','visor','chain']},
};
const GH_BODY='M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z';
const GH_DRIP='M16 31 C16 13 48 13 48 31 V47 Q47 53 45 47 Q44 55 41 48 Q39 53 37 48 Q35 56 32 48 Q30 53 28 48 Q26 55 23 48 Q21 53 19 48 Q17 52 16 47 Z';
const GH_HAT='<rect x="-4.5" y="-8.5" width="9" height="8.8" rx="0.8" fill="#16A34A" stroke="#14532D" stroke-width="0.6"/><rect x="-7.5" y="-0.6" width="15" height="2.4" rx="1.2" fill="#15803D" stroke="#14532D" stroke-width="0.5"/><rect x="-4.5" y="-3" width="9" height="2" fill="#141414"/><rect x="-1.4" y="-3.4" width="2.8" height="2.8" fill="none" stroke="#F5C542" stroke-width="0.9"/>';
const GH_ACC={
  solshirt:'<defs><linearGradient id="ghsol" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#9945FF"/><stop offset="1" stop-color="#14F195"/></linearGradient></defs><path d="M16 37.5 L22 35 Q32 38.5 42 35 L48 37.5 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z" fill="#17142a"/><g fill="url(#ghsol)"><path d="M28 39.6 h9 l-2 2 h-9 z"/><path d="M26 42.6 h9 l2 2 h-9 z"/><path d="M28 45.6 h9 l-2 2 h-9 z"/></g>',
  cap:'<path d="M17 25 Q32 6 47 25 Z" fill="#16A34A"/><path d="M45 24 Q53 22 58 26 Q51 28 45 26.5 Z" fill="#15803D"/><circle cx="32" cy="12.5" r="1.6" fill="#15803D"/>',
  gum:'<circle cx="40" cy="41" r="6.5" fill="#F9A8D4" stroke="#DB2777" stroke-width="1"/><ellipse cx="38" cy="38.5" rx="1.8" ry="1.2" fill="#fff" opacity=".8"/>',
  bow:'<g transform="rotate(-12 40 16)"><path d="M40 16 l-8 -5.5 v11 z M40 16 l8 -5.5 v11 z" fill="#EC4899" stroke="#9D174D" stroke-width="0.8"/><circle cx="40" cy="16" r="2.4" fill="#F472B6" stroke="#9D174D" stroke-width="0.8"/></g>',
  patch:'<path d="M16.5 22 L47.5 35" stroke="#141414" stroke-width="1.8"/><ellipse cx="27" cy="30.5" rx="5" ry="5.6" fill="#141414"/>',
  blush:'<g fill="#F472B6" opacity=".75"><ellipse cx="21.5" cy="37" rx="3" ry="2"/><ellipse cx="42.5" cy="37" rx="3" ry="2"/></g><g fill="#B45309"><circle cx="20" cy="36" r=".7"/><circle cx="23" cy="37.5" r=".7"/><circle cx="41" cy="37.5" r=".7"/><circle cx="44" cy="36" r=".7"/></g><path d="M28.5 38 q3.5 3 7 0" fill="none" stroke="#141414" stroke-width="1.6" stroke-linecap="round"/>',
  halo:'<ellipse cx="32" cy="9" rx="11" ry="3.2" fill="none" stroke="#FDE047" stroke-width="2.6"/>',
  prop:'<path d="M20 21 Q32 10 44 21 Z" fill="#3B82F6"/><path d="M26 15.5 Q32 13.5 38 15.5" stroke="#FDE047" stroke-width="2" fill="none"/><path d="M32 13 V7" stroke="#141414" stroke-width="1.6"/><ellipse cx="26.5" cy="6.5" rx="5.5" ry="1.8" fill="#EF4444"/><ellipse cx="37.5" cy="6.5" rx="5.5" ry="1.8" fill="#22C55E"/><circle cx="32" cy="6.5" r="1.5" fill="#141414"/>',
  medal2:'<path d="M27 34 L32 41 L37 34" fill="none" stroke="#3B82F6" stroke-width="2.6"/><circle cx="32" cy="44.5" r="6" fill="#D1D5DB" stroke="#6B7280" stroke-width="1.2"/><text x="32" y="48" text-anchor="middle" font-size="9" font-weight="900" font-family="Arial,sans-serif" fill="#374151">2</text>',
  medal3:'<path d="M27 34 L32 41 L37 34" fill="none" stroke="#DC2626" stroke-width="2.6"/><circle cx="32" cy="44.5" r="6" fill="#D97706" stroke="#78350F" stroke-width="1.2"/><text x="32" y="48" text-anchor="middle" font-size="9" font-weight="900" font-family="Arial,sans-serif" fill="#451A03">3</text>',
  cloud:'<g><path d="M20 12 a5 5 0 0 1 6 -6 a7 7 0 0 1 12 1 a5 5 0 0 1 6 5 z" fill="#6B7280"/><path d="M24 14 l-1.5 4 M31 14 l-1.5 4 M38 14 l-1.5 4" stroke="#60A5FA" stroke-width="1.6" stroke-linecap="round"/></g>',
  tear:'<path d="M24 36 q-2 3 0 4.5 q2 -1.5 0 -4.5 z" fill="#60A5FA"/><path d="M28 40.5 q4 -3 8 0" fill="none" stroke="#141414" stroke-width="1.6" stroke-linecap="round"/>',
  band:'<path d="M16.6 24 Q32 15 47.4 24 L47.9 28.5 Q32 20 16.1 28.5 Z" fill="#141414"/>',
  wband:'<path d="M16.6 22 Q32 13 47.4 22 L47.9 27 Q32 18.5 16.1 27 Z" fill="#fafafa" stroke="#9ca3af" stroke-width="0.8"/><path d="M47 23 l6 -5 l-0.5 5 l3.5 2.5 l-6 0.5 z" fill="#fafafa" stroke="#9ca3af" stroke-width="0.8"/>',
  phones:'<path d="M14 30 C13 6 51 6 50 30" fill="none" stroke="#141414" stroke-width="3"/><rect x="10" y="25" width="7" height="11" rx="3" fill="#141414" stroke="#4ADE80" stroke-width="1.4"/><rect x="47" y="25" width="7" height="11" rx="3" fill="#141414" stroke="#4ADE80" stroke-width="1.4"/>',
  pixel:'<rect x="17" y="27" width="30" height="2.2" fill="#141414"/><rect x="19" y="28" width="11" height="6" fill="#141414"/><rect x="34" y="28" width="11" height="6" fill="#141414"/><g fill="#7C3AED"><rect x="21" y="29.5" width="2" height="2"/><rect x="25" y="29.5" width="2" height="2"/><rect x="23" y="31.5" width="2" height="2"/><rect x="36" y="29.5" width="2" height="2"/><rect x="40" y="29.5" width="2" height="2"/><rect x="38" y="31.5" width="2" height="2"/></g>',
  pixelk:'<rect x="17" y="27" width="30" height="2.2" fill="#141414"/><rect x="19" y="28" width="11" height="6" fill="#141414"/><rect x="34" y="28" width="11" height="6" fill="#141414"/><g fill="#fff"><rect x="21" y="29.5" width="2" height="2"/><rect x="23" y="31.5" width="2" height="2"/><rect x="36" y="29.5" width="2" height="2"/><rect x="38" y="31.5" width="2" height="2"/></g>',
  shades:'<path d="M18 27.5 H46" stroke="#141414" stroke-width="1.6"/><path d="M19 27.5 h11 q0 7 -6 7 q-5 0 -5 -7 z M34 27.5 h11 q0 7 -6 7 q-5 0 -5 -7 z" fill="#141414"/><path d="M21 29.5 l3 -1.5 M36 29.5 l3 -1.5" stroke="#555" stroke-width="1.2"/>',
  usd:'<circle cx="15" cy="42" r="7.5" fill="#F5C542" stroke="#B7791F" stroke-width="1.2"/><text x="15" y="46" text-anchor="middle" font-size="11" font-weight="800" font-family="Arial,sans-serif" fill="#15803D">$</text>',
  btc:'<circle cx="46" cy="44" r="7.5" fill="#F5C542" stroke="#B7791F" stroke-width="1.2"/><text x="46" y="48" text-anchor="middle" font-size="10" font-weight="800" font-family="Arial,sans-serif" fill="#7C2D12">\u20BF</text>',
  hat:'<g transform="translate(36,17.6) rotate(12)">'+GH_HAT+'</g>',
  hatc:'<g transform="translate(33,2.2) rotate(8)">'+GH_HAT+'</g>',
  check:'<defs><pattern id="ghck" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="4" fill="#15803D"/><rect width="2" height="2" fill="#141414"/><rect x="2" y="2" width="2" height="2" fill="#141414"/></pattern></defs><path d="M14 34 L50 34 L33 49 Z" fill="url(#ghck)" stroke="#141414" stroke-width="0.8"/>',
  arrows:'<path d="M17 25.5 Q32 16.5 47 25.5" fill="none" stroke="#141414" stroke-width="5"/><g fill="#4ADE80" stroke="#141414" stroke-width="0.7"><path d="M21 22 l3.5 -6 l3.5 6 h-2 v3 h-3 v-3 z"/><path d="M28.5 18.5 l3.5 -6 l3.5 6 h-2 v3 h-3 v-3 z"/><path d="M36 22 l3.5 -6 l3.5 6 h-2 v3 h-3 v-3 z"/></g>',
  pack:'<path d="M45 33 q-3 -7 -9 -7" fill="none" stroke="#14532D" stroke-width="2"/><rect x="43" y="32" width="12" height="15" rx="4" fill="#16A34A" stroke="#14532D" stroke-width="1.2"/><rect x="44.5" y="34.5" width="9" height="5" rx="2" fill="#22C55E" stroke="#14532D" stroke-width="0.8"/><rect x="46" y="41" width="6" height="4" rx="1" fill="#15803D"/>',
  slime:'<path d="M18.5 22 C21 11 43 11 45.5 22 Q44.5 26 43 22.5 Q42 30 40 23.5 Q37.5 27 35.5 23.5 Q34 31 32 24 Q29.5 27 27.5 23.5 Q26 29 24 23.5 Q21.5 26.5 20.5 23 Q19 25 18.5 22 Z" fill="#84CC16" stroke="#4D7C0F" stroke-width="0.6"/>',
  ninja:'<path d="M16.4 24 Q32 16 47.6 24 L48 34 Q32 29 16 34 Z" fill="#2b2b2b"/><path d="M47 25 l6 -4 l-1 5 l4 2 l-6 1 z" fill="#2b2b2b"/>',
  hood:'<path d="M16 33 Q32 29 48 33 V44 H16 Z" fill="#262626"/><path d="M17 24 Q32 15 47 24" fill="none" stroke="#2f2f2f" stroke-width="2"/>',
  dots:'<g fill="#3F8A31"><circle cx="22" cy="22" r="1.4"/><circle cx="40" cy="20" r="1.2"/><circle cx="19" cy="34" r="1.5"/><circle cx="45" cy="37" r="1.4"/><circle cx="32" cy="40" r="1.3"/><circle cx="25" cy="44" r="1.1"/><circle cx="40" cy="45" r="1.5"/><circle cx="31" cy="19" r="1"/><circle cx="44" cy="27" r="1"/></g>',
  vlines:'<g stroke-linecap="round" stroke-width="2.4"><path d="M4 22 H14" stroke="#8B5CF6"/><path d="M2 27 H14" stroke="#8B5CF6"/><path d="M5 32 H14" stroke="#8B5CF6" opacity=".8"/><path d="M3 37 H14" stroke="#7BD23C"/><path d="M6 42 H15" stroke="#7BD23C"/><path d="M4 47 H15" stroke="#7BD23C" opacity=".8"/></g>',
  visor:'<rect x="19" y="25" width="26" height="11" rx="5.5" fill="#141414"/><circle cx="27" cy="30.5" r="3.2" fill="#8B5CF6"/><circle cx="37" cy="30.5" r="3.2" fill="#8B5CF6"/>',
  chain:'<path d="M18 36 Q32 50 46 36" fill="none" stroke="#B7791F" stroke-width="4" stroke-linecap="round"/><path d="M18 36 Q32 50 46 36" fill="none" stroke="#F5C542" stroke-width="2.6" stroke-dasharray="2.6 1.6" stroke-linecap="round"/>',
  blur:'<g stroke="#fff" stroke-linecap="round"><path d="M3 24 H17" stroke-width="2" opacity=".35"/><path d="M1 29 H16" stroke-width="2.5" opacity=".5"/><path d="M4 34 H16" stroke-width="2" opacity=".4"/><path d="M2 39 H17" stroke-width="2.5" opacity=".3"/><path d="M6 44 H18" stroke-width="2" opacity=".25"/></g>',
};
const GH_HEAD=['hat','halo','prop','bow','cap'];

function ghostSvg(tag,rank,size){
  const L=GH_LOOK[tag]||{f:GW,e:GK},e=L.e,lose=rank==='L';let a=(L.a||[]).slice(),face=L.face;
  if(rank===1||lose)a=a.filter(x=>!GH_HEAD.includes(x)||(x==='hat'&&rank===1));
  if(rank===1)a=a.map(x=>x==='hat'?'hatc':x);
  if(lose&&L.shape!=='flame')face='sad';
  let b='';
  if(L.shape==='comet')b+='<path d="M30 17 C44 15 52 24 51 32 C50 42 41 48 31 46 L12 43 L28 41 L4 37 L27 35 L2 31 L27 28 L6 24 L28 22 L10 18 Z" fill="#fff"/>';
  else if(L.shape==='flame')b+='<path d="M50 31 C50 20 42 15 35 16 C27 16 22 20 13 19 C20 22 22 24 17 26 C11 28 8 30 5 34 C13 32 18 33 16 36 C12 39 10 42 8 46 C15 42 22 42 25 44 C31 48 46 46 50 38 Z" fill="#fff"/>';
  else b+=`<path d="${L.hem==='d'?GH_DRIP:GH_BODY}" fill="${L.f}"${L.f===GK?' stroke="#4b4b55" stroke-width="1"':''}/>`;
  if(L.hem==='d'&&L.f!==GG)b+='<path d="M16 42 Q20 39 24 42.5 Q28 39.5 32 42 Q36 39.5 40 42.5 Q44 39 48 42 V47 Q47 53 45 47 Q44 55 41 48 Q39 53 37 48 Q35 56 32 48 Q30 53 28 48 Q26 55 23 48 Q21 53 19 48 Q17 52 16 47 Z" fill="#7BD23C"/>';
  if(L.hem==='g')b+='<path d="M16 45 Q19 43 22 45.5 Q25 42.5 28 45 Q31 47 33 44.5 Q36 42.5 39 45.5 Q42 43 45 45 Q47 43.5 48 44.5 V49 Q46 56 43 50 Q41 57 38 50 Q35 55 32 51 Q29 57 26 50 Q23 55 21 50 Q18 56 16 49 Z" fill="#7BD23C"/>';
  const ex=L.shape==='comet'?[38,45]:[27,37],ec=e==='none'?(L.f===GK?GW:GK):e;
  const pre=a.filter(x=>x==='ninja'||x==='hood');a=a.filter(x=>!pre.includes(x));
  b+=pre.map(x=>GH_ACC[x]).join('');
  if(e!=='none'||lose){
    if(face==='angry'){const[l,r]=ex;b+=`<path d="M${l-5} 26.5 L${l+3.5} 30 Q${l+3} 35 ${l-1} 34 Q${l-5} 32.5 ${l-5} 26.5 Z M${r+5} 26.5 L${r-3.5} 30 Q${r-3} 35 ${r+1} 34 Q${r+5} 32.5 ${r+5} 26.5 Z" fill="${ec}"/>`}
    else if(face==='sleepy'||face==='smile')b+=`<path d="M23 31 q4 3 8 0 M33 31 q4 3 8 0" fill="none" stroke="${ec}" stroke-width="1.8" stroke-linecap="round"/>${face==='smile'?`<path d="M29 37 q3 2.5 6 0" fill="none" stroke="${ec}" stroke-width="1.6" stroke-linecap="round"/>`:''}`;
    else if(face==='x')b+=`<path d="M24 27.5 l6 6 M30 27.5 l-6 6 M34 27.5 l6 6 M40 27.5 l-6 6" stroke="${ec}" stroke-width="2.2" stroke-linecap="round"/><path d="M29 38.5 q1.5 -1.5 3 0 q1.5 1.5 3 0" fill="none" stroke="${ec}" stroke-width="1.4"/>`;
    else if(face==='sad'){const[l,r]=ex;b+=`<path d="M${l-3.5} 27 l6 -2.5 M${r+3.5} 27 l-6 -2.5" stroke="${ec}" stroke-width="1.6" stroke-linecap="round"/><ellipse cx="${l}" cy="31.5" rx="2.6" ry="3.4" fill="${ec}"/><ellipse cx="${r}" cy="31.5" rx="2.6" ry="3.4" fill="${ec}"/>`}
    else b+=`<ellipse cx="${ex[0]}" cy="30.5" rx="3" ry="4.2" fill="${ec}"/><ellipse cx="${ex[1]}" cy="30.5" rx="3" ry="4.2" fill="${ec}"/>`;
  }
  if(lose)a=a.filter(x=>x!=='blush'&&x!=='patch');
  b+=a.map(x=>GH_ACC[x]).join('');
  if(rank===2)b+=GH_ACC.medal2;
  if(rank===3)b+=GH_ACC.medal3;
  if(lose)b+=(L.shape==='flame'?'':GH_ACC.tear.replace(/M24 36/,'M'+(ex[0]-3)+' 36'))+GH_ACC.cloud;
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 64 64" aria-label="${esc(tag)}">${rank===1?GH_CROWN:''}${b}</svg>`}

// Glow colour that suits a body colour (used by the background sprites and the app icon).
const GH_GLOW=f=>f===GG?'#4ADE80':f===GP||f===GD||f===GL?'#c084fc':f===GK?'#a855f7':f===GS?'#e5e7eb':'#a78bfa';
const GH_TAGS=Object.keys(GH_LOOK);
// Glow colour for a tag (looks up its body colour).
function ghostGlow(tag){return GH_GLOW((GH_LOOK[tag]||{f:GW}).f);}


return {ghostSvg,GH_TAGS,GH_LOOK,GH_GLOW,ghostGlow,esc};})();
