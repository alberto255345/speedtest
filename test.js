// test.js
/* eslint-disable no-console */
process.env.NODE_TLS_REJECT_UNAUTHORIZED = process.env.NODE_TLS_REJECT_UNAUTHORIZED ?? "0";

const fetch = require("node-fetch");
const { URL } = require("url");
const AbortController = global.AbortController || require("abort-controller");

// ==== Parâmetros alinhados ao site ====
const DOWNLOAD_DURATION_SEC = 15;
const DOWNLOAD_STREAMS = 10;
const CHUNK_SIZE_MB = 20;
const UPLOAD_DURATION_SEC = 15;
const UPLOAD_STREAMS = 3;
const PING_SAMPLES = 20;            // menor que 100 pra ser mais ágil
const SINGLE_REQ_TIMEOUT_MS = 5000; // timeout por request
const GLOBAL_TIMEOUT_MS = 3 * 60 * 1000; // 3 minutos

// ==== Endpoints do back de descoberta ====
const STINFO_BASE = "https://speedtest.eaqbr.com.br:8443";
const GET_RESUME_PATH = "/stinfo-isp/v1/web/device/get-resume";

// Credencial Basic que o site usa (você mesma coletou)
const BASIC_AUTH =
  "Basic d3A6JDJhJDEwJG1iUkJBS1hmd3NtcENDVnJpdDkwY2VDQU5JcDVqWXVaM3pTMTljOE9MSmJtYkpkN0tTMUky";

// helper: timeout
function withTimeout(promise, ms, label) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), ms);
  const wrapped = promise(ctrl.signal)
    .finally(() => clearTimeout(t))
    .catch((e) => {
      if (e.name === "AbortError") throw new Error(`${label || "request"}: timeout`);
      throw e;
    });
  return wrapped;
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function fetchJSON(url, opts = {}, singleTimeout = SINGLE_REQ_TIMEOUT_MS, label = "fetch") {
  return withTimeout(async (signal) => {
    const res = await fetch(url, { ...opts, signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return res.json();
  }, singleTimeout, label);
}

async function fetchBuffer(url, opts = {}, singleTimeout = SINGLE_REQ_TIMEOUT_MS, label = "fetch") {
  return withTimeout(async (signal) => {
    const res = await fetch(url, { ...opts, signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return res.arrayBuffer();
  }, singleTimeout, label);
}

async function head(url, singleTimeout = SINGLE_REQ_TIMEOUT_MS, label = "head") {
  return withTimeout(async (signal) => {
    const res = await fetch(url, { method: "HEAD", signal });
    if (!res.ok) throw new Error(`${label} HTTP ${res.status}`);
    return res;
  }, singleTimeout, label);
}

function nowMs() {
  return Date.now();
}

function mbps(bytes, elapsedSec) {
  return (bytes * 8) / (elapsedSec * 1024 * 1024);
}

function pickProto(hostBaseUrl) {
  // O get-resume vem com "http://..." — o site sobe pra https em runtime quando está em https.
  // Aqui tentamos HTTPS primeiro, depois caímos pra HTTP se falhar.
  const u = new URL(hostBaseUrl);
  if (u.protocol === "http:") {
    const httpsTry = new URL(hostBaseUrl);
    httpsTry.protocol = "https:";
    return [httpsTry.toString().replace(/\/+$/, ""), hostBaseUrl.replace(/\/+$/, "")];
  }
  return [hostBaseUrl.replace(/\/+$/, "")];
}

async function discoverServers() {
  const url = `${STINFO_BASE}${GET_RESUME_PATH}?t2=${Math.random()}`;
  const body = { srvTp: "WST", appVs: "3.0.0", pltf: "W", loc: "0,0" };

  console.log("🌐  1) Descobrindo servidores (STINFO:get-resume) …");
  const data = await fetchJSON(
    url,
    {
      method: "POST",
      headers: {
        "Authorization": BASIC_AUTH,
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Origin": "https://www.brasilbandalarga.com.br",
        "Referer": "https://www.brasilbandalarga.com.br/",
      },
      body: JSON.stringify(body),
    },
    SINGLE_REQ_TIMEOUT_MS,
    "get-resume"
  );

  const ispOrg = data?.isp?.as?.org?.name || data?.isp?.as?.org?.label || "n/a";
  const ip = data?.isp?.ip?.number || "n/a";
  const reg = data?.geo?.address || "n/a";

  const onnet = (data?.ptts?.onnet || []).map((row) => ({ base: row[0], label: row[1], onnet: true }));
  const offnet = (data?.ptts?.offnet || []).map((row) => ({ base: row[0], label: row[1], onnet: false }));
  console.log(`   ISP: ${ispOrg} | IP: ${ip} | Região: ${reg}`);
  console.log(`   Servidores retornados: on/off = ${onnet.length}/${offnet.length}`);

  return { onnet, offnet, ispOrg, ip, reg };
}

async function rttToBase(base) {
  const tries = pickProto(base);
  const path = "/?r=" + Math.random();
  const start = nowMs();
  for (const candidate of tries) {
    try {
      await head(candidate + path, 1500, "rtt");
      return { url: candidate, rtt: nowMs() - start, ok: true, proto: new URL(candidate).protocol };
    } catch (_) {
      // tenta próximo proto
    }
  }
  return { url: tries[0], rtt: Infinity, ok: false, proto: new URL(tries[0]).protocol };
}

async function chooseBestServer(all) {
  console.log("🧭  2) Selecionando melhor servidor por RTT …");
  const sample = [...all].slice(0, Math.max(8, Math.min(12, all.length))); // 8–12 primeiros
  console.log(`🔎  Medindo RTT em ${sample.length} servidores…`);

  let best = null;
  for (const s of sample) {
    const res = await rttToBase(s.base);
    if (res.ok) {
      console.log(`   • ${new URL(res.url).host} ${res.proto.toUpperCase()} → ${res.rtt} ms`);
      if (!best || res.rtt < best.rtt) best = { ...res, label: s.label };
    } else {
      // tenta HTTP fallback explicitamente
      const httpURL = new URL(s.base); httpURL.protocol = "http:";
      try {
        const t0 = nowMs();
        await head(httpURL.toString() + "/?r=" + Math.random(), 1500, "rtt-fallback");
        const rtt = nowMs() - t0;
        console.log(`   • ${httpURL.host} HTTP  → ${rtt} ms`);
        if (!best || rtt < best.rtt) best = { url: httpURL.toString(), rtt, ok: true, label: s.label, proto: "http:" };
      } catch {
        console.log(`   • ${new URL(s.base).host} HTTPS → falha`);
        console.log(`     ↳ ${httpURL.host} HTTP  → falha`);
      }
    }
  }

  if (!best) throw new Error("Nenhum servidor respondeu RTT");
  console.log(`✅  Servidor escolhido: ${new URL(best.url).host} (${best.proto.toUpperCase()}), RTT ~ ${best.rtt} ms — ${best.label}`);
  return best;
}

async function measurePing(baseURL) {
  console.log("🏓  3) Medindo latência/jitter/perda …");
  const times = [];
  let lost = 0;
  for (let i = 1; i <= PING_SAMPLES; i++) {
    const u = baseURL + "/?r=" + Math.random();
    const t0 = nowMs();
    try {
      await head(u, 1500, "ping");
      times.push(nowMs() - t0);
    } catch {
      lost++;
    }
    process.stdout.write(`   [${i}/${PING_SAMPLES}] ${times[times.length - 1] ?? "timeout"} ms\r`);
    await sleep(30);
  }
  process.stdout.write("\n");
  const avg = times.length ? times.reduce((a, b) => a + b, 0) / times.length : Infinity;
  const jit = times.length > 1
    ? times.slice(1).reduce((a, t, i) => a + Math.abs(t - times[i]), 0) / (times.length - 1)
    : 0;
  const lossPct = (lost / PING_SAMPLES) * 100;
  console.log(`   → ping médio: ${Math.round(avg)} ms | jitter: ${Math.round(jit)} ms | perda: ${lossPct.toFixed(2)} %`);
  return { ping: avg, jitter: jit, lossPct };
}

async function measureDownload(baseURL) {
  console.log("⬇️  4) Medindo DOWNLOAD …");
  console.log(`⬇️  Download: ${DOWNLOAD_STREAMS} fluxos, ${DOWNLOAD_DURATION_SEC}s, caminho /download/${CHUNK_SIZE_MB} …`);

  const endAt = nowMs() + DOWNLOAD_DURATION_SEC * 1000;
  let totalBytes = 0;

  async function worker(id) {
    while (nowMs() < endAt) {
      const url = `${baseURL}/download/${CHUNK_SIZE_MB}?r=${Math.random()}`;
      const t0 = nowMs();
      try {
        const buf = await fetchBuffer(url, {}, SINGLE_REQ_TIMEOUT_MS, `download-${id}`);
        const bt = Buffer.from(buf).length; // bytes
        totalBytes += bt;
        const sec = (nowMs() - t0) / 1000;
        process.stdout.write(`   [#${id}] +${(bt / (1024 * 1024)).toFixed(2)} MB em ${sec.toFixed(2)}s\r`);
      } catch {
        // ignora erro e tenta de novo
      }
    }
  }

  const workers = [];
  for (let i = 1; i <= DOWNLOAD_STREAMS; i++) workers.push(worker(i));
  await Promise.all(workers);

  const elapsedSec = DOWNLOAD_DURATION_SEC;
  const speed = mbps(totalBytes, elapsedSec);
  process.stdout.write("\n");
  console.log(`   → total: ${(totalBytes / (1024 * 1024)).toFixed(2)} MB em ${elapsedSec}s | média: ${speed.toFixed(2)} Mbps`);
  return speed;
}

async function measureUpload(baseURL) {
  console.log("⬆️  5) Medindo UPLOAD …");
  console.log(`⬆️  Upload: ${UPLOAD_STREAMS} fluxos, ${UPLOAD_DURATION_SEC}s, posts pequenos repetidos…`);

  const endAt = nowMs() + UPLOAD_DURATION_SEC * 1000;
  let totalBytes = 0;

  // “Pacote pequeno” em linha com a ideia do uploadDataSmall do site.
  const CHUNK_BYTES = 256 * 1024; // 256 KB
  const payload = Buffer.alloc(CHUNK_BYTES, 0x61); // 'a'
  // application/x-www-form-urlencoded — envia num campo "d="
  const formPrefix = "d=";
  const formBuf = Buffer.concat([Buffer.from(formPrefix), payload]);
  const headers = {
    "Content-Type": "application/x-www-form-urlencoded",
    "Content-Encoding": "identity",
  };

  async function worker(id) {
    while (nowMs() < endAt) {
      const url = `${baseURL}/upload?r=${Math.random()}`;
      const t0 = nowMs();
      try {
        await withTimeout(
          (signal) =>
            fetch(url, {
              method: "POST",
              headers,
              body: formBuf,
              signal,
            }),
          SINGLE_REQ_TIMEOUT_MS,
          `upload-${id}`
        );
        totalBytes += CHUNK_BYTES;
        const sec = (nowMs() - t0) / 1000;
        process.stdout.write(`   [#${id}] +${(CHUNK_BYTES / 1024).toFixed(0)} KB em ${sec.toFixed(2)}s\r`);
      } catch {
        // ignora e continua
      }
    }
  }

  const workers = [];
  for (let i = 1; i <= UPLOAD_STREAMS; i++) workers.push(worker(i));
  await Promise.all(workers);

  const elapsedSec = UPLOAD_DURATION_SEC;
  const speed = mbps(totalBytes, elapsedSec);
  process.stdout.write("\n");
  console.log(`   → total: ${(totalBytes / (1024 * 1024)).toFixed(2)} MB em ${elapsedSec}s | média: ${speed.toFixed(2)} Mbps`);
  return speed;
}

async function main() {
  const globalCtrl = new AbortController();
  const globalTimer = setTimeout(() => {
    console.error("⏱️  Timeout global de 3 minutos atingido. Encerrando…");
    process.exit(1);
  }, GLOBAL_TIMEOUT_MS);

  try {
    const { onnet, offnet, ispOrg, ip, reg } = await discoverServers();
    const all = [...onnet, ...offnet];

    const best = await chooseBestServer(all);

    const pingStats = await measurePing(best.url);

    const dl = await measureDownload(best.url);
    const ul = await measureUpload(best.url);

    console.log("\n✅  RESUMO");
    console.log(`   Servidor: ${new URL(best.url).host} (${new URL(best.url).protocol.replace(":", "").toUpperCase()}) — ${best.label}`);
    console.log(`   Ping médio: ${Math.round(pingStats.ping)} ms | Jitter: ${Math.round(pingStats.jitter)} ms | Perda: ${pingStats.lossPct.toFixed(0)}%`);
    console.log(`   Download: ${dl.toFixed(2)} Mbps | Upload: ${ul.toFixed(2)} Mbps\n`);

    // JSON final
    const out = {
      timestamp: new Date().toISOString(),
      isp: { ip, org: ispOrg },
      geo: { address: reg },
      server: { host: new URL(best.url).host, protocol: new URL(best.url).protocol.replace(":", ""), label: best.label, rtt_ms: best.rtt },
      latency_ms: Math.round(pingStats.ping),
      jitter_ms: Math.round(pingStats.jitter),
      packet_loss_pct: +pingStats.lossPct.toFixed(2),
      download_mbps: +dl.toFixed(2),
      upload_mbps: +ul.toFixed(2),
    };
    console.log(JSON.stringify(out, null, 2));
  } finally {
    clearTimeout(globalTimer);
    globalCtrl.abort();
  }
}

main().catch((e) => {
  console.error("ERRO:", e.message || e);
  process.exit(1);
});
