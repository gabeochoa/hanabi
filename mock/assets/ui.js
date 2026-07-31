/* ============================================================
   RENDER SIDEBAR CHAT ROWS
   ============================================================ */
const TAG_LABEL = { blocked:"Blocked", ready:"Review", done:"Done" };
function tagHtml(tag){
  if(!tag) return "";
  return '<span class="tag '+tag+'">'+(TAG_LABEL[tag]||tag)+'</span>';
}
function chatRowHtml(id){
  const t = THREADS[id];
  let cls = "chat-row";
  // glyph: dedicated SHAPE per attention status (not color-only)
  //   blocked -> red up-triangle | review -> green diamond | done -> blue dot
  // The glyph SHAPE acts as the row's indent. Calm rows (no shape) still emit
  // an empty glyph slot of the same width, so titles stay column-aligned.
  let glyph = '<span class="glyph"></span>';  // empty placeholder slot by default
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
/* Sync just the open-tab indicators on sidebar rows (called when tabs change). */
function syncOpenIndicators(){
  document.querySelectorAll('.chat-row[data-tid]').forEach(row=>{
    const id = row.getAttribute('data-tid');
    row.classList.toggle('open-tab', openTabs.includes(id));
  });
}
function renderSidebar(){
  document.querySelectorAll('[data-chats]').forEach(el=>{
    const key = el.getAttribute('data-chats');
    const ids = FOLDER_MEMBERS[key] || [];
    el.innerHTML = ids.map(chatRowHtml).join('');
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
  for(const f of ["stars","oncall","experiments"]){
    const ids = FOLDER_MEMBERS[f], w = waitingCount(ids);
    fo += '<div class="folder-ov">'+
            '<div class="folder-ov-head">'+
              '<span class="fo-name">'+FOLDER_LABEL[f]+'</span>'+
              (w>0 ? '<span class="fo-badge">'+w+' waiting</span>'
                   : '<span class="fo-count">'+ids.length+'</span>')+
            '</div>'+
            ids.map(starCardHtml).join('')+
          '</div>';
  }
  document.getElementById('foldersList').innerHTML = fo;
}
function starCardHtml(id){
  const t = THREADS[id];
  return '<div class="card" style="padding:11px 14px;margin-bottom:8px;cursor:pointer" onclick="openChat(\''+id+'\')">'+
    '<div class="chead" style="margin-bottom:2px"><span class="cname" style="font-size:13px">'+t.title+'</span>'+tagHtml(t.tag)+'</div>'+
    '<div class="cctx" style="margin:0">'+t.sub+'</div></div>';
}

/* ============================================================
   TABS + TRANSCRIPT
   ============================================================ */
let openTabs = [];       // array of tab ids (thread ids or the special "home")
let activeTab = null;
let pinnedTabs = new Set();   // tab ids that are pinned (sort left, distinct look)

// "home" is a real tab whose content is the Home digest screen.
const HOME_TAB = "home";
function tabTitle(id){ return id===HOME_TAB ? "Home" : THREADS[id].title; }
function isThreadTab(id){ return id!==HOME_TAB; }

// pinned tabs render before unpinned, otherwise insertion order is preserved.
function orderedTabs(){
  return openTabs.slice().sort((a,b)=>{
    const pa = pinnedTabs.has(a)?0:1, pb = pinnedTabs.has(b)?0:1;
    if(pa!==pb) return pa-pb;
    return openTabs.indexOf(a)-openTabs.indexOf(b);
  });
}

function renderTabs(){
  const wrap = document.getElementById('tabs');
  wrap.innerHTML = orderedTabs().map(id=>{
    const act = id===activeTab ? ' active':'';
    const pinned = pinnedTabs.has(id);
    const pinIcon = pinned
      ? '<svg viewBox="0 0 24 24" fill="currentColor" stroke="none"><path d="M14 2l8 8-4 1-4 4-1 6-3-3-5 5-1-1 5-5-3-3 6-1 4-4z"/></svg>'
      : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2l8 8-4 1-4 4-1 6-3-3-5 5-1-1 5-5-3-3 6-1 4-4z"/></svg>';
    // pinned tabs show a pin toggle (no close); unpinned show a pin toggle + close
    const pinBtn = '<span class="tpin" title="'+(pinned?'Unpin tab':'Pin tab')+'" onclick="event.stopPropagation();togglePin(\''+id+'\')">'+pinIcon+'</span>';
    const closeBtn = pinned ? '' :
      '<span class="tclose" onclick="event.stopPropagation();closeTab(\''+id+'\')">'+
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"><path d="M6 6l12 12M18 6L6 18"/></svg>'+
      '</span>';
    return '<div class="tab'+(pinned?' pinned':'')+act+'" onclick="focusTab(\''+id+'\')">'+
      pinBtn +
      '<span class="tt">'+tabTitle(id)+'</span>'+
      closeBtn +
      '</div>';
  }).join('');
}

// Show the content for whichever tab is active (Home digest vs a thread transcript).
function showTabContent(id){
  if(id===HOME_TAB){ showScreen('home'); }
  else { showScreen('chat'); renderTranscript(id); }
}

function openChat(id){
  if(!openTabs.includes(id)) openTabs.push(id);   // no duplicate: only push if new
  activeTab = id;
  renderTabs();
  showTabContent(id);
  setActiveSmart(null);
  syncOpenIndicators();
}
// Open (or focus) the Home tab. Home defaults to pinned so it sorts first.
function openHome(){
  if(!openTabs.includes(HOME_TAB)){ openTabs.unshift(HOME_TAB); pinnedTabs.add(HOME_TAB); }
  activeTab = HOME_TAB;
  renderTabs();
  showScreen('home');
  setActiveSmart('home');
  syncOpenIndicators();
}
function focusTab(id){
  activeTab = id;
  renderTabs();
  showTabContent(id);
  setActiveSmart(id===HOME_TAB ? 'home' : null);
  syncOpenIndicators();
}
function togglePin(id){
  if(pinnedTabs.has(id)) pinnedTabs.delete(id); else pinnedTabs.add(id);
  renderTabs();
}
function closeTab(id){
  const i = openTabs.indexOf(id);
  if(i>-1) openTabs.splice(i,1);
  pinnedTabs.delete(id);
  if(activeTab===id){
    if(openTabs.length){
      const next = orderedTabs()[0];
      activeTab = next; showTabContent(next);
      setActiveSmart(next===HOME_TAB ? 'home' : null);
    }
    else { activeTab=null; showScreen('home'); setActiveSmart('home'); }
  }
  renderTabs();
  syncOpenIndicators();
}

/* one sub-agent row inside the transcript sub-agent panel */
const SUB_STATUS = {
  running: "working now",
  done:    "finished",
  blocked: "needs a decision",
};
function subItemHtml(s){
  let g = '<span class="glyph g-working"></span>';         // running sub-agent
  if(s.state==="done") g = '<span class="glyph g-done"></span>';
  else if(s.state==="blocked") g = '<span class="glyph g-blocked"></span>';
  const status = s.note || SUB_STATUS[s.state] || s.state;
  return '<div class="sub-item">'+g+
    '<span class="st-title">'+s.title+'</span>'+
    '<span class="st-sep">·</span>'+
    '<span class="st-status">'+status+'</span></div>';
}
function renderTranscript(id){
  const t = THREADS[id];
  document.getElementById('chatTitle').textContent = t.title;
  document.getElementById('chatSub').textContent = t.sub;
  const roleLabel = {user:"You", assistant:"Assistant", tool:"Tool", system:"System"};
  const avatar = {user:"Y", assistant:"A", tool:"T", system:"S"};
  let html = "";
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

/* ============================================================
   SCREENS / SMART VIEWS
   ============================================================ */
function showScreen(name){
  document.querySelectorAll('.screen').forEach(s=>s.classList.remove('active'));
  const el = document.getElementById('screen-'+name);
  if(el) el.classList.add('active');
}
function setActiveSmart(view){
  document.querySelectorAll('.smart-item').forEach(i=>i.classList.remove('active'));
  if(view){
    const el = document.querySelector('.smart-item[data-view="'+view+'"]');
    if(el) el.classList.add('active');
  }
}
document.getElementById('smartList').addEventListener('click', e=>{
  const item = e.target.closest('.smart-item');
  if(!item) return;
  const view = item.getAttribute('data-view');
  if(view==='home'){ openHome(); return; }   // Home opens as a (pinned) tab
  // other smart views stay as pane-swap screens
  setActiveSmart(view);
  showScreen(view);
});

// All-folders icon (in the Folders section header) opens the All-folders screen.
function openAllFolders(){ setActiveSmart(null); showScreen('folders'); }

// Fold-all toggle: collapse every folder-head, or expand them all (toggles state).
function toggleAllFolders(){
  const heads = document.querySelectorAll('.folders .folder-head');
  const anyOpen = Array.from(heads).some(h=>!h.classList.contains('collapsed'));
  heads.forEach(h=>h.classList.toggle('collapsed', anyOpen));  // if any open -> collapse all, else expand all
  // swap the fold-all icon + tooltip to reflect the next action
  const btn = document.getElementById('foldAllBtn');
  const icon = document.getElementById('foldAllIcon');
  if(anyOpen){ // now all collapsed -> next action expands
    btn.title = "Expand all folders";
    icon.innerHTML = '<path d="M7 13l5 5 5-5M7 6l5 5 5-5"/>';
  } else {
    btn.title = "Collapse all folders";
    icon.innerHTML = '<path d="M17 11l-5-5-5 5M17 18l-5-5-5 5"/>';
  }
}

/* ============================================================
   SIDEBAR COLLAPSE
   ============================================================ */
function toggleSidebar(){ document.getElementById('window').classList.toggle('folded'); }
document.getElementById('collapseBtn').addEventListener('click', toggleSidebar);

function toggleFolder(el){ el.classList.toggle('collapsed'); }

/* ============================================================
   THEME  (System / Light / Dark — chosen in Settings)
   ============================================================ */
let themePref = 'system';   // 'system' | 'light' | 'dark'
function systemPrefersDark(){
  return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
}
function applyTheme(){
  const html = document.documentElement;
  const eff = themePref==='system' ? (systemPrefersDark() ? 'dark':'light') : themePref;
  html.setAttribute('data-theme', eff);
  // reflect selection in the settings segmented control if it's mounted
  document.querySelectorAll('.seg-opt[data-theme-opt]').forEach(b=>{
    b.classList.toggle('active', b.getAttribute('data-theme-opt')===themePref);
  });
}
function setThemePref(p){ themePref = p; applyTheme(); }
if(window.matchMedia){
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', ()=>{ if(themePref==='system') applyTheme(); });
}

/* ============================================================
   SETTINGS (modal — holds theme + prefs)
   ============================================================ */
function openSettings(){ document.getElementById('settingsOverlay').classList.add('open'); applyTheme(); }
function closeSettings(){ document.getElementById('settingsOverlay').classList.remove('open'); }

/* ============================================================
   SPOTLIGHT
   ============================================================ */
function openSpotlight(){
  document.getElementById('spotlightOverlay').classList.add('open');
  setTimeout(()=>document.getElementById('spotlightInput').focus(),40);
}
function closeSpotlight(){ document.getElementById('spotlightOverlay').classList.remove('open'); }
document.getElementById('spotlightOverlay').addEventListener('click', closeSpotlight);

/* ============================================================
   MAC MENU-BAR EXTRA (system status item — lives outside the window)
   ============================================================ */
function toggleMenubar(){ document.getElementById('menubarPop').classList.toggle('open'); }
document.getElementById('menubarBtn').addEventListener('click', (e)=>{ e.stopPropagation(); toggleMenubar(); });
document.addEventListener('click', (e)=>{
  const pop = document.getElementById('menubarPop');
  const btn = document.getElementById('menubarBtn');
  if(pop.classList.contains('open') && !pop.contains(e.target) && !btn.contains(e.target)){
    pop.classList.remove('open');
  }
});
// Menu-bar's primary action = start a new chat/task.
function menubarNewChat(){ toggleMenubar(); openSpotlight(); }
function fromMenubar(id){ toggleMenubar(); openChat(id); }

/* ============================================================
   KEYBOARD
   ============================================================ */
document.addEventListener('keydown', e=>{
  const meta = e.metaKey || e.ctrlKey;
  if(meta && e.key.toLowerCase()==='b'){ e.preventDefault(); toggleSidebar(); }
  else if(meta && e.key.toLowerCase()==='k'){ e.preventDefault(); openSpotlight(); }
  else if(e.key==='Escape'){ closeSpotlight(); closeSettings(); document.getElementById('menubarPop').classList.remove('open'); }
});

/* init */
applyTheme();
renderSidebar();
openHome();      // start with the Home tab open (pinned) and active
