// test.js (corrigido)
// eslint-disable-next-line no-console
process.env.NODE_TLS_REJECT_UNAUTHORIZED = process.env.NODE_TLS_REJECT_UNAUTHORIZED ?? "0";

const fetch = require("node-fetch");
const { URL } = require("url");
const AbortController = global.AbortController || require("abort-controller");

// ===== Parâmetros (alinhados ao site) =====
const DOWNLOAD_DURATION_SEC = 15;
const DOWNLOAD_STREAMS = 10;
const CHUNK_SIZE_MB = 20;         // /download/20
const UPLOAD_DURATION_SEC = 15;
const UPLOAD_STREAMS = 3;
const PING_SAMPLES = 20;

const SINGLE_REQ_TIMEOUT_MS = 15000;  // ↑ aumentamos pra 15s
const GLOBAL_TIMEOUT_MS = 3 * 60 * 1000;

// ===== Descoberta (STINFO) =====
const STINFO_BASE = "https://speedtest.eaqbr.com.br:8443";
const GET_RESUME_PATH = "/stinfo-isp/v1/web/device/get-resume";
const BASIC_AUTH = "Basic d3A6JDJhJDEwJG1iUkJBS1hmd3NtcENDVnJpdDkwY2VDQU5JcDVqWXVaM3pTMTljOE9MSmJtYkpkN0tTMUky";

// ===== Cabeçalhos “de navegador” (usamos em tudo) =====
const COMMON_HEADERS = {
  "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36",
  "Accept": "*/*",
  "Origin": "https://www.brasilbandalarga.com.br",
  "Referer": "https://www.brasilbandalarga.com.br/",
  "Cache-Control": "no-cache",
  "Pragma": "no-cache",
  "Connection": "keep-alive"
};

// ===== Utils =====
function sleep(ms){ return new Promise(r => setTimeout(r, ms)); }
function nowMs(){ return Date.now(); }
function mbps(bytes, sec){ return (bytes * 8) / (sec * 1024 * 1024); }
function fmtMB(b){ return (b/(1024*1024)).toFixed(2); }
function fmtMs(v){ return Number.isFinite(v) ? Math.round(v) : 0; }
function deadlineAfter(ms){
  const id = setTimeout(()=>{ console.error("⏱️  Timeout global (3 min)."); process.exit(2); }, ms);
  return ()=>clearTimeout(id);
}

// fetch com timeout
function withTimeout(executor, ms, label="request"){
  const ctrl = new AbortController();
  const t = setTimeout(()=>ctrl.abort(), ms);
  return executor(ctrl.signal)
    .finally(()=>clearTimeout(t))
    .catch(e=>{
      if (e.name === "AbortError") throw new Error(`${label}: timeout`);
      throw e;
    });
}

async function fetchJSON(url, opts={}, to=SINGLE_REQ_TIMEOUT_MS, label="fetchJSON"){
  return withTimeout(async (signal)=>{
    const res = await fetch(url, { ...opts, headers: { ...COMMON_HEADERS, ...(opts.headers||{}) }, signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return res.json();
  }, to, label);
}

async function fetchHead(url, to=SINGLE_REQ_TIMEOUT_MS, label="HEAD"){
  return withTimeout(async (signal)=>{
    const res = await fetch(url, { method:"HEAD", headers: { ...COMMON_HEADERS }, signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return res;
  }, to, label);
}

// stream: soma bytes conforme chegam
async function fetchAndCountBytes(url, opts={}, to=SINGLE_REQ_TIMEOUT_MS, label="GET"){
  return withTimeout(async (signal)=>{
    const res = await fetch(url, { ...opts, headers: { ...COMMON_HEADERS, ...(opts.headers||{}) }, signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return new Promise((resolve, reject)=>{
      let total = 0;
      res.body.on("data", chunk => { total += chunk.length; });
      res.body.on("end", ()=> resolve(total));
      res.body.on("error", reject);
    });
  }, to, label);
}

// ===== Descobrir servidores via STINFO =====
async function discoverServers(){
  console.log("🌐  1) Descobrindo servidores (STINFO:get-resume) …");
  const url = `${STINFO_BASE}${GET_RESUME_PATH}?t2=${Math.random()}`;
  const body = { srvTp:"WST", appVs:"3.0.0", pltf:"W", loc:"0,0" };

  const data = await fetchJSON(url, {
    method:"POST",
    headers: { "Authorization": BASIC_AUTH, "Content-Type":"application/json", "Accept":"application/json" },
    body: JSON.stringify(body)
  }, SINGLE_REQ_TIMEOUT_MS, "get-resume");

  const ispOrg = data?.isp?.as?.org?.name || data?.isp?.as?.org?.label || "n/a";
  const ip     = data?.isp?.ip?.number || "n/a";
  const reg    = data?.geo?.address || "n/a";
  const onnet  = (data?.ptts?.onnet  || []).map(r => ({ base:r[0], label:r[1], onnet:true  }));
  const offnet = (data?.ptts?.offnet || []).map(r => ({ base:r[0], label:r[1], onnet:false }));

  console.log(`   ISP: ${ispOrg} | IP: ${ip} | Região: ${reg}`);
  console.log(`   Servidores retornados: on/off = ${onnet.length}/${offnet.length}`);
  return { all:[...onnet, ...offnet], ispOrg, ip, reg };
}

function protoCandidates(base){
  // get-resume vem “http://host”; tentamos HTTPS e caímos para HTTP se precisar
  const u = new URL(base);
  const httpsTry = new URL(base); httpsTry.protocol = "https:";
  return [httpsTry.toString().replace(/\/+$/,""), base.replace(/\/+$/,"")];
}

async function rttToBase(base){
  const tries = protoCandidates(base);
  const path = "/?r="+Math.random();
  for (const candidate of tries){
    try {
      const t0 = nowMs();
      await fetchHead(candidate+path, 1500, "rtt");
      return { url:candidate, rtt: nowMs()-t0, ok:true, proto: new URL(candidate).protocol };
    } catch { /* tenta próximo */ }
  }
  return { url:tries[0], rtt: Infinity, ok:false, proto: new URL(tries[0]).protocol };
}

async function chooseBestServer(all){
  console.log("🧭  2) Selecionando melhor servidor por RTT …");
  const sample = all.slice(0, Math.max(8, Math.min(12, all.length)));
  console.log(`🔎  Medindo RTT em ${sample.length} servidores…`);
  let best = null;
  for (const s of sample){
    const res = await rttToBase(s.base);
    const host = new URL(res.url).host;
    if (res.ok){
      const proto = (res.proto || "").replace(":","").toUpperCase();
      console.log(`   • ${host} ${proto} → ${res.rtt} ms`);
      if (!best || res.rtt < best.rtt) best = { ...res, label: s.label };
    } else {
      console.log(`   • ${host} → falha`);
    }
    await sleep(30);
  }
  if (!best) throw new Error("Nenhum servidor respondeu RTT");
  const proto = (best.proto || "").replace(":","").toUpperCase();
  console.log(`✅  Servidor escolhido: ${new URL(best.url).host} (${proto}), RTT ~ ${best.rtt} ms — ${best.label}`);
  return best;
}

// ===== Ping / Jitter / Perda =====
async function measurePing(baseURL){
  console.log("🏓  3) Medindo latência/jitter/perda …");
  const arr = [];
  let lost = 0;
  for (let i=1;i<=PING_SAMPLES;i++){
    const t0 = nowMs();
    try {
      await fetchHead(`${baseURL}/?r=${Math.random()}`, 2000, "ping");
      arr.push(nowMs()-t0);
    } catch { lost++; }
    process.stdout.write(`   [${i}/${PING_SAMPLES}] ${arr[arr.length-1] ?? "timeout"} ms\r`);
    await sleep(25);
  }
  process.stdout.write("\n");
  const avg = arr.length ? arr.reduce((a,b)=>a+b,0)/arr.length : Infinity;
  const jit = arr.length>1 ? arr.slice(1).reduce((a,t,i)=>a+Math.abs(t-arr[i]),0)/(arr.length-1) : 0;
  const lossPct = (lost/PING_SAMPLES)*100;
  console.log(`   → ping médio: ${fmtMs(avg)} ms | jitter: ${fmtMs(jit)} ms | perda: ${lossPct.toFixed(2)} %`);
  return { ping:avg, jitter:jit, lossPct };
}

// ===== Download (stream, com headers e timeout maior) =====
async function measureDownload(baseURL){
  console.log("⬇️  4) Medindo DOWNLOAD …");
  console.log(`⬇️  Download: ${DOWNLOAD_STREAMS} fluxos, ${DOWNLOAD_DURATION_SEC}s, caminho /download/${CHUNK_SIZE_MB} …`);
  const endAt = nowMs() + DOWNLOAD_DURATION_SEC*1000;
  let totalBytes = 0;

  async function worker(id){
    while (nowMs() < endAt){
      const url = `${baseURL}/download/${CHUNK_SIZE_MB}?r=${Math.random()}`;
      const t0 = nowMs();
      try {
        const bytes = await fetchAndCountBytes(url, {}, SINGLE_REQ_TIMEOUT_MS, `download-${id}`);
        totalBytes += bytes;
        const sec = (nowMs()-t0)/1000;
        process.stdout.write(`   [#${id}] +${fmtMB(bytes)} MB em ${sec.toFixed(2)}s\r`);
      } catch {
        // ignora e tenta de novo
      }
    }
  }

  const workers = [];
  for (let i=1;i<=DOWNLOAD_STREAMS;i++) workers.push(worker(i));
  await Promise.all(workers);

  const elapsedSec = DOWNLOAD_DURATION_SEC;
  const speed = mbps(totalBytes, elapsedSec);
  process.stdout.write("\n");
  console.log(`   → total: ${fmtMB(totalBytes)} MB em ${elapsedSec}s | média: ${speed.toFixed(2)} Mbps`);
  return speed;
}

// ===== Upload (headers iguais ao site) =====
async function measureUpload(baseURL){
  console.log("⬆️  5) Medindo UPLOAD …");
  console.log(`⬆️  Upload: ${UPLOAD_STREAMS} fluxos, ${UPLOAD_DURATION_SEC}s, posts pequenos repetidos…`);
  const endAt = nowMs() + UPLOAD_DURATION_SEC*1000;
  let totalBytes = 0;

  const CHUNK_BYTES = 256*1024; // 256 KB
  const payload = Buffer.alloc(CHUNK_BYTES, 0x61); // 'a'
  const formBuf = Buffer.concat([Buffer.from("d="), payload]); // x-www-form-urlencoded (campo d=)
  const headers = {
    ...COMMON_HEADERS,
    "Content-Type": "application/x-www-form-urlencoded",
    "Content-Encoding": "identity"
  };

  async function worker(id){
    while (nowMs() < endAt){
      const url = `${baseURL}/upload?r=${Math.random()}`;
      const t0 = nowMs();
      try {
        await withTimeout(signal => fetch(url, { method:"POST", headers, body: formBuf, signal }), SINGLE_REQ_TIMEOUT_MS, `upload-${id}`);
        totalBytes += CHUNK_BYTES;
        const sec = (nowMs()-t0)/1000;
        process.stdout.write(`   [#${id}] +${(CHUNK_BYTES/1024)|0} KB em ${sec.toFixed(2)}s\r`);
      } catch { /* ignora */ }
    }
  }

  const workers = [];
  for (let i=1;i<=UPLOAD_STREAMS;i++) workers.push(worker(i));
  await Promise.all(workers);

  const elapsedSec = UPLOAD_DURATION_SEC;
  const speed = mbps(totalBytes, elapsedSec);
  process.stdout.write("\n");
  console.log(`   → total: ${fmtMB(totalBytes)} MB em ${elapsedSec}s | média: ${speed.toFixed(2)} Mbps`);
  return speed;
}

// ===== Main =====
(async ()=>{
  const cancel = deadlineAfter(GLOBAL_TIMEOUT_MS);
  try{
    const { all, ispOrg, ip, reg } = await discoverServers();
    const best = await chooseBestServer(all);
    const baseURL = best.url;

    const pingStats = await measurePing(baseURL);
    const dl = await measureDownload(baseURL);
    const ul = await measureUpload(baseURL);

    console.log("\n✅  RESUMO");
    console.log(`   Servidor: ${new URL(baseURL).host} (${new URL(baseURL).protocol.replace(":","").toUpperCase()}) — ${best.label}`);
    console.log(`   Ping médio: ${fmtMs(pingStats.ping)} ms | Jitter: ${fmtMs(pingStats.jitter)} ms | Perda: ${pingStats.lossPct.toFixed(0)}%`);
    console.log(`   Download: ${dl.toFixed(2)} Mbps | Upload: ${ul.toFixed(2)} Mbps\n`);

    const out = {
      timestamp: new Date().toISOString(),
      isp: { ip, org: ispOrg },
      geo: { address: reg },
      server: { host: new URL(baseURL).host, protocol: new URL(baseURL).protocol.replace(":",""), label: best.label, rtt_ms: best.rtt },
      latency_ms: fmtMs(pingStats.ping),
      jitter_ms: fmtMs(pingStats.jitter),
      packet_loss_pct: +pingStats.lossPct.toFixed(2),
      download_mbps: +dl.toFixed(2),
      upload_mbps: +ul.toFixed(2)
    };
    console.log(JSON.stringify(out, null, 2));
  } catch(e){
    console.error("❌  Falha:", e.message || e);
    process.exit(1);
  } finally {
    cancel();
  }
})();
