// Quanta Cloud remote adapter (used by remote.html on the Quanta website; never on the controller).
// The controller's own page (ui/index.html) asks for /api/state, /api/config and posts /api/control
// and /api/config. Here those go through the Firebase Realtime Database instead of the local network:
//   devices/<id>/live            {json, t}            the controller's /api/state (every 3 s while watched)
//   devices/<id>/config          {json, gen}          its saved settings
//   devices/<id>/commands/watch  server time           written here every 20 s: "someone is looking"
//   devices/<id>/commands/remote {seq, kind, body, t}  a request for the controller
//   devices/<id>/commands/ack    {seq, code, body}     its answer
window.QuantaRemote = (() => {
  "use strict";
  const params = new URLSearchParams(location.search);
  const id = (params.get("device") || "").trim().toLowerCase();
  const auth = firebase.auth(), db = firebase.database(), TS = firebase.database.ServerValue.TIMESTAMP;
  const STALE_MS = 20000;                              // live state older than this = controller not answering
  let live = null, liveAt = 0, config = null, ack = null, offset = 0, ownerErr = "";
  const serverNow = () => Date.now() + offset;
  let readyResolve; const ready = new Promise(r => readyResolve = r);
  const ackWaiters = [];
  const sleep = ms => new Promise(r => setTimeout(r, ms));

  // ---- banner: who/what this page is controlling, and whether it's answering
  function banner(){
    let b = document.getElementById("qrBanner");
    if(!b){
      b = document.createElement("div"); b.id = "qrBanner";
      b.style.cssText = "position:sticky;top:0;z-index:20;display:flex;gap:10px;align-items:center;justify-content:space-between;padding:8px 14px;font:600 13px system-ui,sans-serif;background:#0e1b24;color:#e6eef2";
      document.body.prepend(b);
    }
    const age = liveAt ? serverNow() - liveAt : Infinity;
    const ok = age < STALE_MS;
    const ago = !liveAt ? "no answer yet" : age < 90000 ? Math.round(age/1000) + " s ago" : Math.round(age/60000) + " min ago";
    b.innerHTML = `<a href="/?list=1" style="color:#9ec0ff;text-decoration:none">‹ My controllers</a>
      <span>${ownerErr ? ownerErr : `Remote · <span style="font-family:monospace">${id}</span> · ${ok ? '<span style="color:#7fe0cf">connected</span>' : '<span style="color:#ffc39a">not answering (' + ago + ')</span>'}`}</span>`;
  }

  if(!/^(?:quanta\d+|quantacontroller[1-9]\d*)$/.test(id)){ ownerErr = "No controller chosen."; document.addEventListener("DOMContentLoaded", banner); }

  auth.onAuthStateChanged(async user => {
    if(!user){ location.replace("/?return=" + encodeURIComponent(location.pathname + location.search)); return; }
    if(ownerErr) return;
    try {
      const owned = await db.ref(`users/${user.uid}/devices/${id}`).once("value");
      if(!owned.exists()){ ownerErr = "This controller isn't registered to your account."; banner(); return; }
    } catch(e){ ownerErr = "Can't check your controllers: " + (e.code || e.message); banner(); return; }
    db.ref(".info/serverTimeOffset").on("value", s => offset = s.val() || 0);
    const parse = v => { try { return v && typeof v.json === "string" ? JSON.parse(v.json) : null; } catch(e){ return null; } };
    db.ref(`devices/${id}/live`).on("value", s => { const v = s.val(); const j = parse(v); if(j){ live = j; liveAt = typeof v.t === "number" ? v.t : serverNow(); } banner(); },
      e => { ownerErr = "Can't read the controller (" + (e.code || e.message) + "). Check the database rules."; banner(); });
    // __gen: which save this is (the page compares it with the live cfgGen before using it)
    db.ref(`devices/${id}/config`).on("value", s => { const v = s.val(), j = parse(v); if(j){ if(typeof v.gen === "number") j.__gen = v.gen; config = j; } });
    db.ref(`devices/${id}/commands/ack`).on("value", s => {
      ack = s.val();
      for(let i = ackWaiters.length - 1; i >= 0; i--) if(ack && ack.seq === ackWaiters[i].seq){ ackWaiters[i].done(ack); ackWaiters.splice(i, 1); }
    });
    const ping = () => db.ref(`devices/${id}/commands/watch`).set(TS).catch(e => { ownerErr = "Can't reach the controller (" + (e.code || e.message) + "). Check the database rules."; banner(); });
    ping(); setInterval(ping, 20000);
    setInterval(banner, 5000);
    readyResolve();
  });

  async function waitFor(test, ms, what){
    await Promise.race([ready, sleep(ms)]);
    const end = Date.now() + ms;
    while(Date.now() < end){ if(ownerErr) throw new Error(ownerErr); if(test()) return; await sleep(300); }
    throw new Error(what);
  }
  const fresh = () => live && serverNow() - liveAt < STALE_MS;

  async function get(path){
    if(path === "/api/state"){
      if(!fresh()) await waitFor(fresh, 25000, "The controller isn't answering. Check that it has power and Wi-Fi.");
      return JSON.parse(JSON.stringify(live));
    }
    if(path === "/api/config"){
      if(!config) await waitFor(() => !!config, 25000, "The controller hasn't sent its settings yet.");
      return JSON.parse(JSON.stringify(config));
    }
    if(path === "/api/events") return [];
    throw new Error("Only on the controller's own page");
  }

  // One request at a time. A newer request of the same kind waiting in line replaces the older one
  // (slider moves), and both callers get the newer answer.
  let busy = Promise.resolve(), queued = {};
  function post(path, body){
    const kind = path === "/api/control" ? "control" : path === "/api/config" ? "config" : "";
    if(!kind) return Promise.reject(new Error("That can only be done on the controller's own page."));
    if(queued[kind]){ queued[kind].body = body; return queued[kind].promise; }
    const job = {body}; queued[kind] = job;
    job.promise = (busy = busy.catch(() => {}).then(async () => {
      delete queued[kind];
      await ready; if(ownerErr) throw new Error(ownerErr);
      // Kept under 10 million: the controller's JSON library writes larger numbers in a shortened form,
      // so its answer would never match. (Wraps every ~2.5 h; only "same as the last one" matters.)
      const seq = 1000000 + Date.now() % 9000000;
      const answer = new Promise((resolve, reject) => {
        const t = setTimeout(() => { const i = ackWaiters.findIndex(w => w.seq === seq); if(i >= 0) ackWaiters.splice(i, 1);
          reject(new Error("The controller didn't answer. Check that it has power and Wi-Fi.")); }, 20000);
        ackWaiters.push({seq, done: a => { clearTimeout(t); resolve(a); }});
      });
      await db.ref(`devices/${id}/commands/remote`).set({seq, kind, body: JSON.stringify(job.body), t: TS});
      const a = await answer;
      let j = {}; try { j = JSON.parse(a.body || "{}"); } catch(e){}
      if((a.code|0) >= 400) throw new Error(j.error || ("Error " + a.code));
      if(kind === "control" && j && j.time){ live = j; liveAt = serverNow(); }
      if(kind === "config" && j && j.groups){ config = j; }
      return j;
    }));
    return job.promise;
  }
  return {get, post, id};
})();
