/* ============================================================
   SAMPLE DATA (all invented)
   ============================================================ */
const THREADS = {
  // attn = DONE or WAITING-ON-YOU  -> dot + bold
  // running = self-running          -> dimmed, calm
  // parked = muted/greyed
  t1: { title:"Multi-tier pricing rollout", state:"attn", tag:"blocked", folder:"stars", star:true,
        sub:"waiting on you · 8m", msgs:[
    {role:"system", ts:"3h", text:"Task: land the multi-tier pricing config once CI is green."},
    {role:"assistant", ts:"2h", text:"Built the config diff <b>D948120</b> adding Tier 1/2/3 price points. Running the shadow comparison against prod now."},
    {role:"tool", ts:"1h", text:"shadow_compare → 4,812 accounts · max delta 0.3% · within tolerance ✓"},
    {role:"assistant", ts:"12m", text:"All CI green, shadow matched. This is the config being added:", code:
      '<span class="cm"># pricing/tiers.cinc</span>\n<span class="kw">tiers</span> = {\n  <span class="st">"tier_1"</span>: { price_usd: 4.99 },\n  <span class="st">"tier_2"</span>: { price_usd: 9.99 },\n  <span class="st">"tier_3"</span>: { price_usd: 19.99 },\n}', after:"Nothing else is blocking. <b>I need your approval to land.</b>"},
    {role:"user", ts:"—", text:"", placeholder:true}
  ]},
  t2: { title:"Stars payout reconciliation", state:"attn", tag:"blocked", folder:"stars", star:false,
        subs:[{title:"Ledger diff scan", state:"running", note:"comparing 4,812 rows against the ledger"},{title:"Duplicate-payout check", state:"done", note:"no duplicates found"}],
        sub:"waiting on you · 22m", msgs:[
    {role:"system", ts:"5h", text:"Task: reconcile this cycle's Stars payouts against the ledger before the batch runs."},
    {role:"assistant", ts:"40m", text:"Reconciled 4,812 accounts. 4,810 match to the cent. Two don't:"},
    {role:"assistant", ts:"38m", text:"", list:["acct 8842 — ledger $128.60, computed $116.20 (Δ $12.40)","acct 1097 — ledger $54.10, computed $51.00 (Δ $3.10)"]},
    {role:"assistant", ts:"22m", text:"Both look like a rounding artifact in a promo credit, but proceeding commits the batch — it's irreversible for this cycle. <b>Hold, or proceed and I'll flag the two for manual follow-up?</b>"},
  ]},
  t3: { title:"Creator welcome QP copy", state:"attn", tag:"done", folder:"stars", star:false,
        sub:"done · 1h", msgs:[
    {role:"system", ts:"2h", text:"Task: draft welcome QP copy for new Stars creators. 3 variants, brand-voice compliant."},
    {role:"assistant", ts:"1h", text:"Three variants, all passing the brand-voice check:"},
    {role:"assistant", ts:"1h", text:"", list:["A (warmest): \u201cWelcome — your fans can now support you directly. Let\u2019s set up Stars.\u201d","B (clearest): \u201cStars are on. Turn on receiving to start earning from your fans.\u201d","C (shortest): \u201cYou\u2019re in. Set up Stars \u2192\u201d"]},
    {role:"assistant", ts:"1h", text:"B scored highest on clarity; A is warmest. <b>Which ships Monday?</b>"},
  ]},
  t4: { title:"Tier upgrade flow", state:"ready", tag:"ready", folder:"experiments", star:false,
        sub:"ready for review · 30m", msgs:[
    {role:"system", ts:"4h", text:"Task: implement in-app upgrade from Tier 1 → Tier 2 with proration."},
    {role:"assistant", ts:"1h", text:"Implemented the upgrade path with a proration line item on the receipt. Wrote 18 new tests."},
    {role:"tool", ts:"35m", text:"make test → 214 passed, 0 failed · lint clean · type check clean ✓"},
    {role:"assistant", ts:"30m", text:"Deployed to staging. <b>Test link is ready</b> — subscribe at Tier 1, upgrade to Tier 2, and confirm the proration line."},
  ]},
  t5: { title:"Payout worker race fix", state:"ready", tag:"ready", folder:"oncall", star:true,
        sub:"ready for review · 2h", msgs:[
    {role:"system", ts:"6h", text:"Task: root-cause and fix intermittent double-writes in the payout worker."},
    {role:"assistant", ts:"3h", text:"Root cause: two workers could claim the same payout row between the read and the lock. Fixed with a conditional update guard."},
    {role:"tool", ts:"2h", text:"stress_run(500) → 0 double writes · CI green ✓"},
    {role:"assistant", ts:"2h", text:"Confident it's fixed. Ready for you to verify on the test tenant."},
  ]},
  t6: { title:"Backfill entitlement table", state:"running", tag:null, folder:"experiments", star:false,
        subs:[{title:"Chunk 1–500k", state:"done", note:"500k rows written"},{title:"Chunk 500k–1M", state:"running", note:"at row 812k"},{title:"Row validator", state:"running", note:"checksums matching so far"}],
        sub:"self-running · 61%", msgs:[
    {role:"system", ts:"2h", text:"Task: backfill the entitlement table for legacy subscribers."},
    {role:"assistant", ts:"5m", text:"Backfill in progress — 61% through 2.1M rows. No action needed; I'll surface it when done or if I hit a snag."},
  ]},
  t7: { title:"Tier schema migration", state:"running", tag:null, folder:"experiments", star:false,
        sub:"self-running · tests", msgs:[
    {role:"assistant", ts:"9m", text:"Running the migration test suite before applying. Quiet until there's something to decide."},
  ]},
  t8: { title:"Nightwatch: D948213", state:"running", tag:null, folder:"oncall", star:false,
        sub:"self-running · landing", msgs:[
    {role:"assistant", ts:"4m", text:"CI green on all signals. Landing the diff now — will report the SHA when it's in."},
  ]},
  t9: { title:"Weekly metrics digest", state:"running", tag:null, folder:"oncall", star:false,
        sub:"self-running", msgs:[
    {role:"assistant", ts:"1m", text:"Assembling the weekly subs metrics digest. Nothing for you yet."},
  ]},
  t10:{ title:"Old A/B: paywall color", state:"parked", tag:null, folder:"experiments", star:false,
        sub:"parked", msgs:[
    {role:"system", ts:"3w", text:"Muted. Experiment concluded — kept for reference."},
    {role:"assistant", ts:"3w", text:"Result was flat. Parked this thread; it won't ask for anything."},
  ]},
  t11:{ title:"Churn query (Q3 cohort)", state:"done", tag:"done", folder:"stars", star:true,
        sub:"done · 12m", msgs:[
    {role:"system", ts:"1h", text:"Task: pull 90-day churn for the Q3 subscriber cohort."},
    {role:"tool", ts:"14m", text:"query returned 41,208 rows · exported to results.csv ✓"},
    {role:"assistant", ts:"12m", text:"Done — 90-day churn came in at 6.2%, down 0.8pt from Q2. Results attached; want a breakdown by tier?"},
  ]},
  t12:{ title:"Docs: onboarding runbook", state:"parked", tag:null, folder:"recent", star:false,
        sub:"parked", msgs:[
    {role:"assistant", ts:"1w", text:"Muted reference thread. No attention needed."},
  ]},
  t13:{ title:"Legacy gifting migration", state:"archived", tag:null, folder:null, star:false,
        sub:"archived · 2mo", msgs:[
    {role:"system", ts:"2mo", text:"Task: migrate legacy gifting rows to the new ledger."},
    {role:"assistant", ts:"2mo", text:"Migration completed and reconciled. Archiving — nothing left to do here."},
  ]},
  t14:{ title:"2024 pricing experiment writeup", state:"archived", tag:null, folder:null, star:false,
        sub:"archived · 5mo", msgs:[
    {role:"assistant", ts:"5mo", text:"Final writeup shipped. Archived for reference."},
  ]},
};

const FOLDER_MEMBERS = {
  stars: ["t1","t2","t3","t11"],
  oncall: ["t5","t8","t9"],
  experiments: ["t4","t6","t7","t10"],
  recent: ["t1","t2","t11","t4","t6","t12"],
  archived: ["t13","t14"],
};
const FOLDER_LABEL = { stars:"Stars — subs", oncall:"Oncall", experiments:"Experiments" };

// How many threads in a set are waiting on the user (attn/done + not parked).
function waitingCount(ids){
  return ids.filter(id=>{ const t=THREADS[id]; return (t.state==="attn"||t.state==="done"); }).length;
}
