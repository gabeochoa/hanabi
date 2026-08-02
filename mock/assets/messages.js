const chev='<svg class="chev" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6"><path d="M9 6l6 6-6 6"/></svg>';
const ic={search:'<svg class="tic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="7"/><path d="M21 21l-4-4"/></svg>',
  edit:'<svg class="tic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 20h9M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/></svg>',
  run:'<svg class="tic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 3l14 9-14 9z"/></svg>'};
const okc='<svg viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M20 6L9 17l-5-5"/></svg>';

/* simple inline-markdown: `code` + **bold** (defect #3) */
function md(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')
  .replace(/`([^`]+)`/g,'<code>$1</code>').replace(/\*\*([^*]+)\*\*/g,'<b>$1</b>');}
/* toy syntax highlight for log/diff */
function hl(line,isDiff){
  let l=line.replace(/&/g,'&amp;').replace(/</g,'&lt;');
  if(isDiff){ if(l.startsWith('+'))return '<span class="diff-add">'+l+'</span>';
    if(l.startsWith('-'))return '<span class="diff-del">'+l+'</span>'; return l; }
  return l.replace(/\b(\d+)(ms|s|%)?\b/g,'<span class="num">$1$2</span>')
          .replace(/(503|502|500|401)/g,'<span class="num">$1</span>');
}

const LOG=Array.from({length:22},(_,i)=>`[${String(i).padStart(2,'0')}] retry ${i+1}: upstream 503, backoff ${Math.min(2**i*100,30000)}ms (jitter +${(i*37)%250}ms)`);
const DIFF=['@@ -40,7 +40,12 @@','-  const u = await validateToken(req.headers.authorization)','+  const claims = verifyBearer(req.headers.authorization)','+  if (!claims) return res.status(401).end()','+  // only hydrate when the route needs it','+  const u = req.needsSession ? await hydrateSession(claims.sub) : claims','   return next()'];

function foldBlock(lang,lines,isDiff){
  const long=lines.length>6;
  const body=lines.map((l,i)=>'<span class="ln">'+(i+1)+'</span>'+hl(l,isDiff)).join('\n');
  return '<div class="block'+(long?'':' open')+'">'+
    '<div class="block-bar"><span class="lang">'+lang+'</span>'+
      (isDiff?'<span style="color:var(--ok)">+5</span> <span style="color:var(--err)">-1</span>':'')+
      '<button class="copy">Copy</button></div>'+
    '<div class="foldable'+(long?' folded':'')+'"><pre>'+body+'</pre></div>'+
    (long?'<button class="showfull" data-fold>'+chev+'<span>Show '+(lines.length-5)+' more lines</span></button>':'')+
  '</div>';
}
function pile(){
  const tools=[
    {ic:'search',cmd:'<b>grep</b> <span>-rn "validateToken" src/</span>',dur:'0.4s',slow:false,out:'src/mw/auth.ts:42\nsrc/mw/auth.ts:88\nsrc/routes/feed.ts:17  (read-only route)'},
    {ic:'edit',cmd:'<b>edit</b> <span>src/mw/auth.ts</span>',dur:'1.2s',slow:false,diff:true},
    {ic:'run',cmd:'<b>run</b> <span>auth suite</span>',dur:'8.1s',slow:true,out:'138 passed, 0 failed (8.1s)'}];
  let rows=tools.map(t=>'<div class="trow">'+ic[t.ic]+'<span class="cmd">'+t.cmd+'</span>'+
    '<span class="dur'+(t.slow?' slow':'')+'">'+t.dur+'</span>'+
    '<span class="st ok">'+okc+' done</span></div>'+
    (t.out?'<div class="trow-out">'+t.out.replace(/</g,'&lt;')+'</div>':'')+
    (t.diff?'<div class="trow-out">'+DIFF.map(l=>hl(l,true)).join('\n')+'</div>':'')).join('');
  return '<div class="pile"><div class="pile-head">'+chev+
    '<span class="title">3 tool calls · grep, edit, run</span>'+
    '<span class="agg">3 tools · 9.7s</span></div>'+
    '<div class="pile-body">'+rows+'</div></div>';
}

const TURNS=[
  {role:'user',ts:'2h',html:'<div class="ubub">'+md("the auth middleware still calls the deprecated `validateToken()` on every request. can you migrate it and make sure read-only routes don't pay for a full session read?")+'</div>'},
  {role:'asst',ts:'2h',html:md("Here's the plan. The legacy `AuthGate` couples token validation with session hydration, so every request pays for a full session read even when it only needs the bearer check.\n\nI'll split it into a stateless `verifyBearer()` (signature + expiry only) and an opt-in `hydrateSession()`, so read-only endpoints skip the session store entirely. Staged behind a flag: ship both, shadow-compare a week, then flip the default.").split('\n\n').map(p=>'<p>'+p+'</p>').join('')+pile()},
  {role:'asst',ts:'1h',html:'<p>'+md("Done. Split `AuthGate` into `verifyBearer()` + opt-in `hydrateSession()` — the feed endpoint dropped from 3 store reads to 0, suite green (**138 passed, 0 failed**). Retry log from the shadow-compare run:")+'</p>'+foldBlock('log',LOG,false)},
];

let LOAD='none';
const col=document.getElementById('col'),scroll=document.getElementById('scroll');
const q=new URLSearchParams(location.search);if(q.get('load'))LOAD=q.get('load');const expand=q.get('expand')==='1';

function avatar(r){return '<span class="av '+(r==='user'?'user':'asst')+'">'+(r==='user'?'Y':'✦')+'</span>';}
function render(){
  if(LOAD==='skeleton'){col.innerHTML=skeleton();syncSeg();return;}
  let h='';
  TURNS.forEach(t=>{
    const nm=t.role==='user'?'You':'hanabi';
    h+='<div class="turn '+t.role+'"><div class="who">'+avatar(t.role)+
       '<span class="name'+(t.role==='asst'?' asst':'')+'">'+nm+'</span>'+
       '<span class="ts" title="Aug 1, 2026 · 4:12 PM">'+t.ts+'</span></div>'+
       '<div class="body">'+t.html+'</div></div>';
  });
  if(LOAD==='stream'){
    h+='<div class="turn asst"><div class="who">'+avatar('asst')+'<span class="name asst">hanabi</span></div>'+
       '<div class="body"><div class="thinking">reading the shadow-compare metrics<span class="dots"><i></i><i></i><i></i></span></div>'+
       '<p>'+md("Flipping the flag now — the read-only path is verified, `verifyBearer()` returns in <1ms")+'<span class="caret"></span></p>'+
       '<button class="stop"><span class="sq"></span>Stop generating</button></div></div>';
  }
  col.innerHTML=h;syncSeg();
  if(expand){col.querySelectorAll('.pile').forEach(p=>p.classList.add('open'));
    col.querySelectorAll('.block').forEach(b=>{b.classList.add('open');const f=b.querySelector('.foldable');if(f)f.classList.remove('folded');});}
}
function skeleton(){
  // mirror the real rhythm: user bubble shape, asst paragraphs, a pile row, a code block (#11)
  const bar=(w,h2)=>'<div class="bar" style="width:'+w+';height:'+(h2||'13px')+';margin:8px 0"></div>';
  let h='<div class="sk-turn"><div class="sk-who"><div class="bar" style="width:24px;height:24px;border-radius:6px"></div><div class="bar" style="width:60px;height:12px"></div></div>'+
    '<div style="display:flex;justify-content:flex-end"><div class="bar" style="width:55%;height:38px;border-radius:13px"></div></div></div>';
  h+='<div class="sk-turn"><div class="sk-who"><div class="bar" style="width:24px;height:24px;border-radius:6px"></div><div class="bar" style="width:70px;height:12px"></div></div>'+
    bar('96%')+bar('88%')+bar('92%')+bar('40%')+
    '<div class="bar" style="width:100%;height:38px;border-radius:10px;margin:12px 0"></div>'+
    '<div class="bar" style="width:100%;height:120px;border-radius:9px;margin:10px 0"></div></div>';
  return h;
}
function syncSeg(){document.querySelectorAll('#loadSeg button').forEach(b=>b.classList.toggle('on',b.dataset.load===LOAD));}
scroll.addEventListener('click',e=>{
  const ph=e.target.closest('.pile-head');if(ph){ph.parentElement.classList.toggle('open');return;}
  const sf=e.target.closest('[data-fold]');if(sf){const bl=sf.closest('.block');bl.classList.toggle('open');
    const f=bl.querySelector('.foldable');f.classList.toggle('folded');
    sf.querySelector('span').textContent=f.classList.contains('folded')?('Show '+(22-5)+' more lines'):'Show less';return;}
  const cp=e.target.closest('.copy');if(cp){cp.textContent='Copied';setTimeout(()=>cp.textContent='Copy',1200);return;}
});
document.getElementById('loadSeg').addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;LOAD=b.dataset.load;render();});
render();
