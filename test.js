/**
 * Teste EAQ/BBL sem navegador — com LOG passo-a-passo no terminal
 * - Descobre servidores via /stinfo-isp/.../get-resume (Authorization Basic igual ao site)
 * - Seleciona melhor servidor por RTT (HTTPS; cai para HTTP se necessário)
 * - Mede ping/jitter/perda com progresso
 * - Mede download/upload com pequenos pacotes em paralelo + progresso por segundo
 * - Timeout geral: 3 minutos
 *
 * Aviso: desabilita verificação TLS (somente para testes locais).
 */

process.env.NODE_TLS_REJECT_UNAUTHORIZED = "0"; // ATENÇÃO: apenas para teste
const https = require("https");
const fetch = require("node-fetch");

// ===== Config base =====
const ORIGIN  = "https://www.brasilbandalarga.com.br";
const REFERER = "https://www.brasilbandalarga.com.br/";
const STINFO_BASE = "https://speedtest.eaqbr.com.br:8443";
const GET_RESUME_PATH = "/stinfo-isp/v1/web/device/get-resume";

// Authorization igual ao que você capturou no curl:
const STINFO_AUTH_B64 = "Basic d3A6JDJhJDEwJG1iUkJBS1hmd3NtcENDVnJpdDkwY2VDQU5JcDVqWXVaM3pTMTljOE9MSmJtYkpkN0tTMUky";

// Ajustes de teste (pode alterar por CLI: --down=15 --up=12 --streamsDown=6 --streamsUp=4)
const DEFAULTS = {
  pingCount: 12,
  downDuration: 15,    // segundos
  upDuration: 12,      // segundos
  streamsDown: 6,
  streamsUp: 4,
  uploadChunkKB: 256,  // tamanho do bloco de upload
  perReqTimeoutMs: 7000,
  globalTimeoutMs: 180000, // 3 minutos
};

const args = parseArgs(process.argv.slice(2));
const agent = new https.Agent({ rejectUnauthorized: false });
const baseHeaders = {
  "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120 Safari/537.36",
  "Origin": ORIGIN,
  "Referer": REFERER,
  "Cache-Control": "no-cache"
};

// ===== Util =====
function parseArgs(argv) {
  const out = {};
  for (const a of argv) {
    if (a.startsWith("--host=")) out.host = a.split("=")[1];
    else if (a === "--http") out.forceProto = "http";
    else if (a === "--https") out.forceProto = "https";
    else if (a.startsWith("--down=")) out.downDuration = +a.split("=")[1] || DEFAULTS.downDuration;
    else if (a.startsWith("--up=")) out.upDuration = +a.split("=")[1] || DEFAULTS.upDuration;
    else if (a.startsWith("--streamsDown=")) out.streamsDown = +a.split("=")[1] || DEFAULTS.streamsDown;
    else if (a.startsWith("--streamsUp=")) out.streamsUp = +a.split("=")[1] || DEFAULTS.streamsUp;
  }
  return out;
}
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
function deadlineAfter(ms) {
  const id = setTimeout(() => {
    console.error("⏱️  Timeout geral atingido (3 minutos). Abortando.");
    process.exit(2);
  }, ms);
  return () => clearTimeout(id);
}
function fmtMbps(v){ return Number.isFinite(v) ? Number(v).toFixed(2) : "0.00"; }
function fmtMB(bytes){ return (bytes/(1024*1024)).toFixed(2); }
function fmtMs(v){ return Number.isFinite(v) ? Math.round(v) : 0; }
function nowISO(){ return new Date().toISOString(); }

async function fetchWithTimeout(url, { timeoutMs = 8000, headers = {}, ...opts } = {}) {
  const controller = new AbortController();
  const to = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(url, { ...opts, headers: { ...baseHeaders, ...headers }, agent, signal: controller.signal });
    return res;
  } finally {
    clearTimeout(to);
  }
}
async function jsonPost(url, body, headers = {}, timeoutMs = 12000) {
  const res = await fetchWithTimeout(url, {
    timeoutMs,
    method: "POST",
    headers: { "Content-Type": "application/json", ...headers },
    body: JSON.stringify(body)
  });
  if (!res.ok) throw new Error(`HTTP ${res.status} em ${url}`);
  return res.json();
}

// ===== STINFO: descoberta =====
async function getResume() {
  const url = `${STINFO_BASE}${GET_RESUME_PATH}?t2=${Math.random()}`;
  const body = { srvTp: "WST", appVs: "3.0.0", pltf: "W", loc: "0,0" };
  return jsonPost(url, body, { Authorization: STINFO_AUTH_B64, Accept: "application/json" }, 15000);
}
function extractHostsFromResume(resume) {
  const entries = [];
  const addGroup = (arr, group) => {
    if (!Array.isArray(arr)) return;
    for (const item of arr) {
      if (!Array.isArray(item) || !item[0]) continue;
      try {
        const u = new URL(item[0]);
        entries.push({ host: u.host, label: item[1] || group });
      } catch {}
    }
  };
  addGroup(resume?.ptts?.onnet, "onnet");
  addGroup(resume?.ptts?.offnet, "offnet");
  return entries;
}

// ===== Seleção de servidor =====
async function timeGet(url, timeoutMs = 2500) {
  const t0 = Date.now();
  try {
    const res = await fetchWithTimeout(url, { timeoutMs, method: "GET" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return Date.now() - t0;
  } catch { return null; }
}
async function probeHostBestProtoVerbose(host, forceProto) {
  if (forceProto === "https") {
    const rtt = await timeGet(`https://${host}/?r=${Math.random()}`, 2500);
    console.log(`   • Testando ${host} via HTTPS → ${rtt === null ? "falha" : rtt+" ms"}`);
    return rtt !== null ? { host, proto: "https", rtt } : null;
  }
  if (forceProto === "http") {
    const rtt = await timeGet(`http://${host}/?r=${Math.random()}`, 2500);
    console.log(`   • Testando ${host} via HTTP → ${rtt === null ? "falha" : rtt+" ms"}`);
    return rtt !== null ? { host, proto: "http", rtt } : null;
  }
  // Tenta HTTPS; se falhar, HTTP
  let rtt = await timeGet(`https://${host}/?r=${Math.random()}`, 2500);
  console.log(`   • ${host} HTTPS → ${rtt === null ? "falha" : rtt+" ms"}`);
  if (rtt !== null) return { host, proto: "https", rtt };

  rtt = await timeGet(`http://${host}/?r=${Math.random()}`, 2500);
  console.log(`     ↳ ${host} HTTP  → ${rtt === null ? "falha" : rtt+" ms"}`);
  if (rtt !== null) return { host, proto: "http", rtt };
  return null;
}
async function selectBestServerDynamic(entries, forceProto) {
  console.log(`🔎  Medindo RTT em ${entries.length} servidores…`);
  const samples = [];
  for (const e of entries) {
    const p = await probeHostBestProtoVerbose(e.host, forceProto);
    if (p) samples.push({ ...p, label: e.label });
    await sleep(40);
  }
  if (!samples.length) throw new Error("Nenhum servidor respondeu.");
  samples.sort((a,b)=>a.rtt-b.rtt);
  const best = samples[0];
  console.log(`✅  Servidor escolhido: ${best.host} (${best.proto.toUpperCase()}), RTT ~ ${best.rtt} ms — ${best.label || ""}`);
  return best;
}

// ===== Ping/Jitter/Perda =====
async function pingStatsVerbose(baseUrl, count = DEFAULTS.pingCount, timeoutMs = 2500) {
  console.log(`🏓  Ping/Jitter/Perda: ${count} amostras…`);
  let ok = 0, fails = 0;
  const list = [];
  for (let i = 0; i < count; i++) {
    const rtt = await timeGet(`${baseUrl}/?r=${Math.random()}`, timeoutMs);
    if (rtt === null) { fails++; console.log(`   [${i+1}/${count}] falha`); }
    else { list.push(rtt); ok++; console.log(`   [${i+1}/${count}] ${rtt} ms`); }
    await sleep(30);
  }
  const lossPct = (fails / (ok + fails)) * 100;
  const avg = list.length ? list.reduce((a,b)=>a+b,0)/list.length : 0;
  let jitter = 0;
  if (list.length > 1) {
    let sum = 0;
    for (let i = 1; i < list.length; i++) sum += Math.abs(list[i]-list[i-1]);
    jitter = sum/(list.length-1);
  }
  console.log(`   → ping médio: ${fmtMs(avg)} ms | jitter: ${fmtMs(jitter)} ms | perda: ${lossPct.toFixed(2)} %`);
  return { avg, jitter, lossPct, samples: list };
}

// ===== Download/Upload com progresso =====
async function downloadTestVerbose(baseUrl, durationSec, streams, perReqTimeoutMs) {
  console.log(`⬇️  Download: ${streams} fluxos, ${durationSec}s, pequenos pacotes…`);
  let totalBytes = 0;
  const endAt = Date.now() + durationSec*1000;

  async function worker(id) {
    while (Date.now() < endAt) {
      try {
        const res = await fetchWithTimeout(`${baseUrl}/download?r=${Math.random()}`, { timeoutMs: perReqTimeoutMs });
        if (!res.ok) continue;
        const buf = await res.arrayBuffer();
        totalBytes += buf.byteLength;
      } catch {}
    }
  }

  const tick = setInterval(() => {
    const elapsed = Math.max(1, (DEFAULTS.downDuration*1000 - Math.max(0, endAt-Date.now()))/1000);
    const mbps = (totalBytes*8)/(elapsed*1e6);
    process.stdout.write(`   [t=${Math.floor(elapsed)}s] ${fmtMB(totalBytes)} MB | ~${fmtMbps(mbps)} Mbps      \r`);
  }, 1000);

  await Promise.all(Array.from({length: streams}, (_,i)=>worker(i+1)));
  clearInterval(tick);

  const elapsed = durationSec;
  const mbps = (totalBytes*8)/(elapsed*1e6);
  process.stdout.write("\n");
  console.log(`   → total: ${fmtMB(totalBytes)} MB em ${elapsed}s | média: ${fmtMbps(mbps)} Mbps`);
  return { totalBytes, elapsedSec: elapsed, mbps };
}

async function uploadTestVerbose(baseUrl, durationSec, streams, chunkKB, perReqTimeoutMs) {
  console.log(`⬆️  Upload: ${streams} fluxos, ${durationSec}s, blocos de ${chunkKB} KB…`);
  let totalBytes = 0;
  const endAt = Date.now() + durationSec*1000;
  const body = Buffer.alloc(chunkKB*1024, 0x78);

  async function worker(id) {
    while (Date.now() < endAt) {
      try {
        const res = await fetchWithTimeout(`${baseUrl}/upload?r=${Math.random()}`, {
          timeoutMs: perReqTimeoutMs,
          method: "POST",
          headers: { "Content-Type": "application/octet-stream", "Content-Encoding": "identity" },
          body
        });
        if (!res.ok) continue;
        totalBytes += body.length;
      } catch {}
    }
  }

  const tick = setInterval(() => {
    const elapsed = Math.max(1, (DEFAULTS.upDuration*1000 - Math.max(0, endAt-Date.now()))/1000);
    const mbps = (totalBytes*8)/(elapsed*1e6);
    process.stdout.write(`   [t=${Math.floor(elapsed)}s] ${fmtMB(totalBytes)} MB | ~${fmtMbps(mbps)} Mbps      \r`);
  }, 1000);

  await Promise.all(Array.from({length: streams}, (_,i)=>worker(i+1)));
  clearInterval(tick);

  const elapsed = durationSec;
  const mbps = (totalBytes*8)/(elapsed*1e6);
  process.stdout.write("\n");
  console.log(`   → total: ${fmtMB(totalBytes)} MB em ${elapsed}s | média: ${fmtMbps(mbps)} Mbps`);
  return { totalBytes, elapsedSec: elapsed, mbps };
}

// ===== Main =====
(async () => {
  const cfg = {
    pingCount: DEFAULTS.pingCount,
    downDuration: args.downDuration || DEFAULTS.downDuration,
    upDuration: args.upDuration || DEFAULTS.upDuration,
    streamsDown: args.streamsDown || DEFAULTS.streamsDown,
    streamsUp: args.streamsUp || DEFAULTS.streamsUp,
    uploadChunkKB: DEFAULTS.uploadChunkKB,
    perReqTimeoutMs: DEFAULTS.perReqTimeoutMs
  };

  const cancelDeadline = deadlineAfter(DEFAULTS.globalTimeoutMs);

  try {
    console.log("🌐  1) Descobrindo servidores (STINFO:get-resume) …");
    const resume = await getResume();
    const entries = extractHostsFromResume(resume);
    if (!entries.length) throw new Error("get-resume não retornou servidores.");

    console.log(`   ISP: ${resume?.isp?.as?.org?.name || "?"} | IP: ${resume?.isp?.ip?.number || "?"} | Região: ${resume?.geo?.address || "?"}`);
    console.log(`   Servidores retornados: on/off = ${resume?.ptts?.onnet?.length || 0}/${resume?.ptts?.offnet?.length || 0}`);

    let baseUrl, best;
    if (args.host) {
      const proto = args.forceProto || "https";
      baseUrl = `${proto}://${args.host}`;
      console.log(`🧭  2) Servidor forçado por CLI: ${baseUrl}`);
    } else {
      console.log("🧭  2) Selecionando melhor servidor por RTT …");
      best = await selectBestServerDynamic(entries, args.forceProto);
      baseUrl = `${best.proto}://${best.host}`;
    }

    console.log("🏓  3) Medindo latência/jitter/perda …");
    const ping = await pingStatsVerbose(baseUrl, cfg.pingCount, 2500);

    console.log("⬇️  4) Medindo DOWNLOAD …");
    const down = await downloadTestVerbose(baseUrl, cfg.downDuration, cfg.streamsDown, cfg.perReqTimeoutMs);

    console.log("⬆️  5) Medindo UPLOAD …");
    const up = await uploadTestVerbose(baseUrl, cfg.upDuration, cfg.streamsUp, cfg.uploadChunkKB, cfg.perReqTimeoutMs);

    const result = {
      timestamp: nowISO(),
      isp: {
        ip: resume?.isp?.ip?.number || null,
        asn: resume?.isp?.as?.asn || null,
        org: resume?.isp?.as?.org?.name || null
      },
      geo: {
        address: resume?.geo?.address || null,
        city: resume?.geo?.city || null,
        state: resume?.geo?.stateIsoA2 || null
      },
      server: best ? { host: best.host, label: best.label || null, protocol: best.proto, rtt_ms: best.rtt } : { host: args.host, protocol: args.forceProto || "https" },
      latency_ms: fmtMs(ping.avg),
      jitter_ms: fmtMs(ping.jitter),
      packet_loss_pct: Number(ping.lossPct.toFixed(2)),
      download_mbps: Number(fmtMbps(down.mbps)),
      upload_mbps: Number(fmtMbps(up.mbps))
    };

    console.log("\n✅  RESUMO");
    console.log(`   Servidor: ${result.server.host} (${result.server.protocol.toUpperCase()})`);
    console.log(`   Ping médio: ${result.latency_ms} ms | Jitter: ${result.jitter_ms} ms | Perda: ${result.packet_loss_pct}%`);
    console.log(`   Download: ${result.download_mbps} Mbps | Upload: ${result.upload_mbps} Mbps\n`);

    // JSON final (caso você queira parsear em outra ferramenta)
    console.log(JSON.stringify(result, null, 2));
  } catch (err) {
    console.error("❌  Falha:", err.message);
    process.exit(1);
  } finally {
    cancelDeadline();
  }
})();
