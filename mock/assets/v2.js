/* ============================================================
   v2.js — blue-sky overlay ON TOP of data.js + ui.js
   Loads AFTER data.js and ui.js. Two jobs:
     1. Data overrides: add the V2-only sample threads/folders to the
        shared globals (t15 empty thread, empty "Drafts" folder).
     2. Function overrides: redefine the 4 shared render functions that
        bluesky customises (empty-state illustrations + entrance motion),
        and add the V2-only empty-state/motion helpers.
   Because classic <script> tags share one global scope and later
   definitions win, these override ui.js. ui.js already ran its init on
   load; we re-render at the bottom so the V2 versions drive the final
   painted state (identical to the original monolithic bluesky.html).
   ============================================================ */

/* ---------- 1. DATA OVERRIDES (V2-only sample data) ---------- */
// V2: an empty thread (no messages yet) to demo the empty-thread state
THREADS.t15 = { title:"New thread", state:"parked", tag:null, folder:"recent", star:false,
      sub:"no messages yet", msgs:[] };
FOLDER_MEMBERS.drafts = [];                 // V2: an empty folder (demo the empty-folder state)
if(!FOLDER_MEMBERS.recent.includes("t15")) FOLDER_MEMBERS.recent.push("t15");
FOLDER_LABEL.drafts = "Drafts";

/* ---------- 2a. OVERRIDDEN RENDER FUNCTIONS ---------- */

function chatRowHtml(id){
  const t = THREADS[id];
  let cls = "chat-row";
  // glyph: dedicated SHAPE per attention status (not color-only)
  //   blocked -> red up-triangle | review -> green diamond | done -> blue dot
  let glyph = "";
  if(t.tag==="blocked"){ cls+=" attn has-glyph"; glyph='<span class="glyph g-blocked"></span>'; }
  else if(t.tag==="ready"){ cls+=" attn has-glyph"; glyph='<span class="glyph g-review"></span>'; }
  else if(t.state==="done"||t.tag==="done"){ cls+=" attn has-glyph"; glyph='<span class="glyph g-done"></span>'; }
  else if(t.state==="attn"){ cls+=" attn has-glyph"; glyph='<span class="glyph g-blocked"></span>'; }
  else if(t.state==="running") cls+=" running";
  else if(t.state==="parked"||t.state==="archived") cls+=" parked";

  const subs = t.subs || [];
  const working = subs.some(s=>s.state==="running");
  // lightweight "a sub-agent is working" signal — hollow ring right of the status shape.
  // (full sub-agent visualization now lives in the thread transcript, not the sidebar.)
  const ring = working ? '<span class="glyph g-working" title="a sub-agent is working"></span>' : "";

  // subtle open-as-tab indicator (accent bar + faint dot) when this thread is an open tab
  if(openTabs.includes(id)) cls+=" open-tab";

  return '<div class="'+cls+'" data-tid="'+id+'" onclick="openChat(\''+id+'\')">'+
      glyph + ring +
      '<span class="ctitle">'+t.title+'</span>'+
      '<span class="opendot" title="open in a tab"></span>'+
  '</div>';
}

function renderSidebar(){
  document.querySelectorAll('[data-chats]').forEach(el=>{
    const key = el.getAttribute('data-chats');
    const ids = FOLDER_MEMBERS[key] || [];
    // V2: empty-folder state inside a folder body when it has no threads
    if(!ids.length && el.classList.contains('folder-body')){
      el.innerHTML = emptyFolderHtml(true);
    } else {
      el.innerHTML = ids.map(chatRowHtml).join('');
    }
  });
  // folder-header attention rollups: show "N waiting" only when >0, else plain count
  document.querySelectorAll('.folder-group').forEach(g=>{
    const body = g.querySelector('.folder-body'); if(!body) return;
    const key = body.getAttribute('data-chats');
    const ids = FOLDER_MEMBERS[key] || [];
    const w = waitingCount(ids);
    const cnt = g.querySelector('.fcount'); if(!cnt) return;
    if(w>0){ cnt.textContent = w+" waiting"; cnt.classList.add('attn'); }
    else   { cnt.textContent = ids.length; cnt.classList.remove('attn'); }
  });
  // archived count (low-signal, plain count only)
  const ac = document.querySelector('[data-archived-count]');
  if(ac) ac.textContent = (FOLDER_MEMBERS.archived||[]).length;
  // starred screen
  const starred = Object.keys(THREADS).filter(id=>THREADS[id].star);
  document.getElementById('starredList').innerHTML = starred.map(starCardHtml).join('');
  // All Folders overview: one card per folder (count + waiting rollup), then its chats
  let fo = "";
  for(const f of ["stars","oncall","experiments","drafts"]){
    const ids = FOLDER_MEMBERS[f], w = waitingCount(ids);
    fo += '<div class="folder-ov">'+
            '<div class="folder-ov-head">'+
              '<span class="fo-name">'+FOLDER_LABEL[f]+'</span>'+
              (w>0 ? '<span class="fo-badge">'+w+' waiting</span>'
                   : '<span class="fo-count">'+ids.length+'</span>')+
            '</div>'+
            (ids.length ? ids.map(starCardHtml).join('') : emptyFolderHtml(false))+
          '</div>';
  }
  document.getElementById('foldersList').innerHTML = fo;
}

function renderTranscript(id){
  const t = THREADS[id];
  document.getElementById('chatTitle').textContent = t.title;
  document.getElementById('chatSub').textContent = t.sub;
  const roleLabel = {user:"You", assistant:"Assistant", tool:"Tool", system:"System"};
  const avatar = {user:"Y", assistant:"A", tool:"T", system:"S"};
  let html = "";

  // V2: empty-thread state (STATIC illustration) — a thread with no messages yet
  const realMsgs = (t.msgs || []).filter(m=>!m.placeholder);
  if(!realMsgs.length && !(t.subs && t.subs.length)){
    document.getElementById('transcript').innerHTML =
      '<div class="empty">'+
        '<div class="art" aria-hidden="true">'+
          '<svg viewBox="0 0 132 108">'+
            /* an empty speech bubble with a soft spark inside; static */
            '<path class="ln" d="M24 30 h84 a8 8 0 0 1 8 8 v34 a8 8 0 0 1 -8 8 h-46 l-16 14 v-14 h-22 a8 8 0 0 1 -8 -8 v-34 a8 8 0 0 1 8 -8 z"/>'+
            '<path class="fill-soft" d="M32 40 h68 v26 h-38 l-10 9 v-9 h-20 z"/>'+
            '<path class="ln" d="M66 46 v14 M59 53 h14"/>'+
            '<circle class="dot" cx="66" cy="53" r="2"/>'+
          '</svg>'+
        '</div>'+
        '<div class="e-title">Nothing here yet</div>'+
        '<div class="e-msg">This thread is a blank slate. Say what you need and I\u2019ll get to work \u2014 the first message starts the story.</div>'+
      '</div>';
    return;
  }

  // sub-agent visualization panel (only when this thread has sub-agents)
  const subs = t.subs || [];
  if(subs.length){
    html += '<div class="subpanel">'+
      '<div class="subpanel-head">'+
        '<span class="sp-ic"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></svg></span>'+
        'Sub-agents ('+subs.length+')'+
      '</div>'+
      '<div class="subpanel-body">'+subs.map(subItemHtml).join('')+'</div>'+
    '</div>';
  }
  for(const m of t.msgs){
    if(m.placeholder) continue;
    let body = "";
    if(m.text) body += '<p>'+m.text+'</p>';
    if(m.code) body += '<div class="codeblock">'+m.code+'</div>';
    if(m.after) body += '<p>'+m.after+'</p>';
    if(m.list) body += '<ul>'+m.list.map(x=>'<li>'+x+'</li>').join('')+'</ul>';
    html += '<div class="msg '+m.role+'">'+
      '<div class="avatar">'+avatar[m.role]+'</div>'+
      '<div class="bubble">'+
        '<div class="meta"><span class="role">'+roleLabel[m.role]+'</span><span class="ts">'+m.ts+'</span></div>'+
        '<div class="text">'+body+'</div>'+
      '</div></div>';
  }
  document.getElementById('transcript').innerHTML = html;
  document.getElementById('transcript').scrollTop = 0;
}

function showScreen(name){
  document.querySelectorAll('.screen').forEach(s=>s.classList.remove('active','anim-rows'));
  const el = document.getElementById('screen-'+name);
  if(el){ el.classList.add('active'); void el.offsetWidth; el.classList.add('anim-rows'); }
}


/* ---------- 2b. V2-ONLY HELPERS ---------- */

function emptyFolderHtml(compact){
  return '<div class="empty'+(compact?' compact sb-empty':'')+'">'+
    '<div class="art" aria-hidden="true">'+
      '<svg viewBox="0 0 132 108">'+
        /* an open, empty folder; static */
        '<path class="ln-soft" d="M22 82 h88"/>'+
        '<path class="ln" d="M28 40 h26 l8 8 h34 a6 6 0 0 1 6 6 v22 a6 6 0 0 1 -6 6 h-62 a6 6 0 0 1 -6 -6 v-30 a6 6 0 0 1 6 -6 z"/>'+
        '<path class="fill-soft" d="M40 62 h48 l-6 16 h-48 z"/>'+
        '<path class="ln" d="M34 62 h60 l-8 20 h-60 z"/>'+
        '<circle class="dot" cx="66" cy="30" r="2"/>'+
      '</svg>'+
    '</div>'+
    '<div class="e-title">Empty folder</div>'+
    '<div class="e-msg">No threads in here yet. Drag one in, or start a new task and file it here.</div>'+
  '</div>';
}

function toggleHomeEmpty(){
  const wrap = document.getElementById('homeWrap');
  const on = wrap.classList.toggle('show-empty');
  document.getElementById('homeEmptyLabel').textContent = on ? 'show digest' : 'preview empty';
}

function toggleBlockedEmpty(){
  const cards = document.getElementById('blockedCards');
  const empty = document.getElementById('blockedEmpty');
  const hidden = empty.style.display === 'none';
  empty.style.display = hidden ? '' : 'none';
  cards.style.display = hidden ? 'none' : '';
  document.getElementById('blockedEmptyLabel').textContent = hidden ? 'show blocked' : 'preview empty';
}

function runSearch(q){
  const query = (q||"").trim().toLowerCase();
  const content = document.getElementById('sbContent');
  const empty = document.getElementById('searchEmpty');
  if(!query){
    // reset: show everything, hide empty, un-dim all rows
    content.style.display = '';
    empty.style.display = 'none';
    document.querySelectorAll('.chat-row[data-tid]').forEach(r=>r.style.display='');
    document.querySelectorAll('.folder-group, .folders, .recent-chats').forEach(g=>g.style.display='');
    return;
  }
  let anyMatch = false;
  document.querySelectorAll('.chat-row[data-tid]').forEach(r=>{
    const id = r.getAttribute('data-tid');
    const title = (THREADS[id] ? THREADS[id].title : '').toLowerCase();
    const hit = title.includes(query);
    r.style.display = hit ? '' : 'none';
    if(hit) anyMatch = true;
  });
  if(anyMatch){
    content.style.display = '';
    empty.style.display = 'none';
  } else {
    content.style.display = 'none';
    empty.style.display = '';
    document.getElementById('searchEmptyMsg').textContent =
      '\u201C'+q.trim()+'\u201D didn\u2019t match any thread. Try fewer letters?';
  }
}

(function(){
  const inp = document.querySelector('.sb-header .search input');
  if(inp) inp.addEventListener('input', e=>runSearch(e.target.value));
})();

function replayMotion(){
  // spin the replay glyph once
  const btn = document.getElementById('replayMotion');
  btn.classList.remove('spin-once'); void btn.offsetWidth; btn.classList.add('spin-once');
  // re-trigger row-in on the active screen's rows
  const active = document.querySelector('.screen.active');
  if(active){
    active.classList.remove('anim-rows'); void active.offsetWidth; active.classList.add('anim-rows');
  }
  // tick the blocked count badges (count/badge tick demo)
  document.querySelectorAll('.smart-item .cnt, .rail-badge, .si-badge, .fcount.attn, .fo-badge').forEach(b=>{
    b.classList.remove('tick'); void b.offsetWidth; b.classList.add('tick');
  });
}

function animateActiveScreen(){
  const active = document.querySelector('.screen.active');
  if(active){ active.classList.remove('anim-rows'); void active.offsetWidth; active.classList.add('anim-rows'); }
}

function demoHomeSkeleton(){
  const wrap = document.getElementById('homeWrap');
  if(!wrap) return;
  wrap.classList.add('show-skel');
  setTimeout(()=>{ wrap.classList.remove('show-skel'); animateActiveScreen(); }, 900);
}


/* ---------- 3. RE-INIT with the V2 function versions ---------- */
/* ui.js already called applyTheme/renderSidebar/openHome on load; re-run the
   render so the overridden (V2) definitions above drive the final paint. */
renderSidebar();
openHome();
demoHomeSkeleton();  // V2: show the loading shimmer briefly on first paint
