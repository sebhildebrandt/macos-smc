const os = require('node:os');
const { setTimeout: sleep } = require('node:timers/promises');
const libVersion = require('../package.json').version;

let native = null;

function n() {
  if (os.platform() !== 'darwin' || os.arch() !== 'arm64') {
    throw new Error('macos-smc: only supported on macOS / Apple Silicon');
  }
  native ??= require('../build/Release/macos_smc_native.node');
  return native;
}

const round = (v, d = 1) =>
  v === null || v === undefined ? null : Math.round(v * 10 ** d) / 10 ** d;
const avg = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : null);

function version() {
  return libVersion;
}

// ---- chip ----

let chipCache = null;

function chip() {
  if (chipCache) return chipCache;
  const nat = n();
  const name = nat.sysctlString('machdep.cpu.brand_string');
  const m = /Apple M(\d+)(?:\s+(Pro|Max|Ultra))?/.exec(name ?? '');
  const tiers = [];
  const levels = nat.sysctlNumber('hw.nperflevels') ?? 0;
  for (let i = 0; i < levels; i++) {
    tiers.push({
      name: nat.sysctlString(`hw.perflevel${i}.name`),
      cores: nat.sysctlNumber(`hw.perflevel${i}.physicalcpu`),
    });
  }
  chipCache = {
    name,
    model: nat.sysctlString('hw.model'),
    generation: m ? Number(m[1]) : null,
    variant: m?.[2] ?? null,
    cpuCores: nat.sysctlNumber('hw.physicalcpu'),
    tiers,
    gpuCores: nat.gpuCores(),
    memoryBytes: nat.sysctlNumber('hw.memsize'),
  };
  return chipCache;
}

// ---- temperatures (SMC) ----

function group(sensors) {
  const values = sensors.map((s) => s.value);
  return {
    max: values.length ? Math.max(...values) : null,
    avg: avg(values),
    sensors,
  };
}

// Tf* is GPU fabric incl. static max/threshold keys (Tf?5/Tf?6), never CPU.
// Exactly 40.0 is an idle-gating sentinel (M3/M4).
const plausible = (v) => v >= 15 && v <= 120 && v !== 40;

function temperatures() {
  const all = n().smcRead('T');
  const pick = (re) => all.filter((s) => re.test(s.key) && plausible(s.value));
  const single = (re) => avg(pick(re).map((s) => s.value));
  return {
    cpu: group(pick(/^T[pe]/)),
    gpu: group(pick(/^Tg/)),
    battery: single(/^TB\dT$/),
    ssd: single(/^TH0x$/),
    wifi: single(/^TW0P$/),
  };
}

// ---- fans (SMC) ----

function fans() {
  const raw = Object.fromEntries(
    n()
      .smcRead('F')
      .map((s) => [s.key, s.value]),
  );
  const count = Math.min(raw.FNum ?? 0, 10);
  const val = (k) => (Number.isFinite(raw[k]) ? raw[k] : null);
  const result = [];
  for (let i = 0; i < count; i++) {
    const rpm = val(`F${i}Ac`);
    if (rpm === null) continue;
    const min = val(`F${i}Mn`);
    const max = val(`F${i}Mx`);
    result.push({
      id: i,
      rpm,
      min,
      max,
      target: val(`F${i}Tg`),
      percent:
        min !== null && max !== null && max > min
          ? round(Math.min(100, Math.max(0, ((rpm - min) / (max - min)) * 100)))
          : null,
    });
  }
  return result;
}

// ---- frequency tables (pmgr) ----

// Tables are u32 [freq, voltage] pairs. M1–M3 store Hz, M4+ CPU tables kHz.
const toMhz = (v) => (v > 1e7 ? v / 1e6 : v / 1e3);

function table(props, name) {
  const words = props[name];
  if (!words) return null;
  const f = [];
  for (let i = 0; i < words.length; i += 2)
    if (words[i] > 1) f.push(toMhz(words[i]));
  return f.length ? f : null;
}

let dvfsCache = null;

function dvfs() {
  if (dvfsCache) return dvfsCache;
  const props = n().pmgr();
  // M5+: acc-clusters holds the voltage-states index per CPU cluster (low byte), matched by state count.
  // M1–M4: fixed tables, E = 1, P = 5, matched by channel name.
  const acc = props['acc-clusters'];
  const idx = acc
    ? [...new Set(acc.filter((_, i) => i % 2 === 0).map((w) => w & 0xff))]
    : [1, 5];
  dvfsCache = {
    cpu: idx
      .map((i) => table(props, `voltage-states${i}-sram`))
      .filter(Boolean),
    byName: acc
      ? null
      : {
          ECPU: table(props, 'voltage-states1-sram') ?? [],
          PCPU: table(props, 'voltage-states5-sram') ?? [],
        },
    gpu: table(props, 'voltage-states9') ?? [],
  };
  return dvfsCache;
}

// ---- IOReport sample ----

const INACTIVE = new Set(['IDLE', 'DOWN', 'OFF']);

// Residency-weighted frequency; first active state maps to the lowest table entry.
function residency(states, freqs) {
  const total = states.reduce((a, s) => a + s.residency, 0);
  const active = states.filter((s) => !INACTIVE.has(s.name));
  const busy = active.reduce((a, s) => a + s.residency, 0);
  let mhz = null;
  if (busy > 0 && freqs.length) {
    mhz = 0;
    active.forEach((s, i) => {
      mhz += (s.residency / busy) * freqs[Math.min(i, freqs.length - 1)];
    });
  }
  return { mhz, usage: total > 0 ? busy / total : null };
}

function pickTable(tables, channel, states) {
  if (tables.byName) {
    if (channel.includes('ECPU')) return tables.byName.ECPU;
    if (channel.includes('PCPU')) return tables.byName.PCPU;
  }
  const t = tables.cpu;
  return (
    t.find((x) => x.length === states) ??
    t.find((x) => x.length <= states) ??
    t[0] ??
    []
  );
}

function summarize(cores) {
  const busy = cores.reduce((a, c) => a + (c.mhz !== null ? c.usage : 0), 0);
  const mhz =
    busy > 0
      ? cores.reduce((a, c) => a + (c.mhz !== null ? c.mhz * c.usage : 0), 0) /
        busy
      : null;
  const mins = cores.map((c) => c.minMhz).filter((v) => v !== null);
  const maxs = cores.map((c) => c.maxMhz).filter((v) => v !== null);
  return {
    cores: cores.length,
    mhz: round(mhz ?? (mins.length ? Math.min(...mins) : null), 0),
    maxMhz: maxs.length ? round(Math.max(...maxs), 0) : null,
    usage: round(avg(cores.map((c) => c.usage ?? 0)) * 100),
  };
}

// Clusters sorted by top frequency fill perflevels in order (perflevel0 = fastest).
function cpuTiers(cores, info) {
  const clusters = new Map();
  for (const c of cores) {
    if (!clusters.has(c.cluster)) clusters.set(c.cluster, []);
    clusters.get(c.cluster).push(c);
  }
  const sorted = [...clusters.values()].sort(
    (a, b) => (b[0].maxMhz ?? 0) - (a[0].maxMhz ?? 0),
  );
  const tiers = info.tiers.map((t) => ({
    name: t.name,
    want: t.cores,
    cores: [],
  }));
  if (!tiers.length) tiers.push({ name: null, want: Infinity, cores: [] });
  let ti = 0;
  for (const cl of sorted) {
    while (ti < tiers.length - 1 && tiers[ti].cores.length >= tiers[ti].want)
      ti++;
    for (const c of cl) {
      c.tier = tiers[ti].name;
      tiers[ti].cores.push(c);
    }
  }
  return tiers
    .filter((t) => t.cores.length)
    .map((t) => ({ name: t.name, ...summarize(t.cores) }));
}

const ENERGY_SCALE = { mJ: 1e-3, uJ: 1e-6, nJ: 1e-9 };
const POLL_MS = 250;

function energy(channels) {
  const sum = (test) => {
    const hits = channels.filter(
      (c) =>
        c.group === 'Energy Model' && c.value !== undefined && test(c.channel),
    );
    return hits.length
      ? hits.reduce((a, c) => a + c.value * (ENERGY_SCALE[c.unit] ?? 0), 0)
      : null;
  };
  return {
    cpu: sum((ch) => ch.endsWith('CPU Energy')),
    gpu: sum((ch) => ch.endsWith('GPU Energy')),
    ane: sum((ch) => ch.startsWith('ANE')),
    dram: sum((ch) => ch.startsWith('DRAM')),
  };
}

// macOS 27 publishes CPU/ANE/DRAM energy only every ~2.1 s, often plus a small chunk ~25 ms later
// (GPU every ~50 ms). Watts are measured between counter updates >= 400 ms apart; the start point
// (anchor) is kept across calls, so short intervals still get values.
const MIN_SPAN = 4e8;
const MAX_AGE = 10e9;
let anchor = null;
let lastSoc = null;

function socPower(nat, polls) {
  const points = [];
  for (let i = 1; i < polls.length; i++) {
    const d = nat.irDelta(polls[i - 1].s, polls[i].s, 'CPU Energy');
    if (d.some((c) => c.value > 0)) points.push(polls[i]);
  }
  const now = process.hrtime.bigint();
  if (!anchor || now - anchor.t > MAX_AGE) anchor = points[0] ?? null;
  const to = anchor && points.findLast((p) => p.t - anchor.t >= MIN_SPAN);
  if (to) {
    const e = energy(nat.irDelta(anchor.s, to.s));
    const sec = Number(to.t - anchor.t) / 1e9;
    const w = (j) => (j === null ? null : j / sec);
    lastSoc = { t: to.t, cpu: w(e.cpu), ane: w(e.ane), dram: w(e.dram) };
    anchor = to;
  }
  return lastSoc && now - lastSoc.t < MAX_AGE / 2
    ? lastSoc
    : { cpu: null, ane: null, dram: null };
}

const MAX_INTERVAL = 60000;

async function sample(intervalMs = 500) {
  if (
    !Number.isFinite(intervalMs) ||
    intervalMs < 0 ||
    intervalMs > MAX_INTERVAL
  ) {
    throw new RangeError(
      `macos-smc: intervalMs must be a number between 0 and ${MAX_INTERVAL}`,
    );
  }
  const nat = n();
  const info = chip();
  const tables = dvfs();

  const now = () => process.hrtime.bigint();
  const t0 = now();
  const polls = [];
  const first = nat.irSample();
  if (first) polls.push({ s: first, t: now() });
  const end = t0 + BigInt(Math.round(intervalMs * 1e6));
  while (now() < end) {
    await sleep(Math.max(1, Math.min(POLL_MS, Number(end - now()) / 1e6)));
    const s = nat.irSample();
    if (s) polls.push({ s, t: now() });
  }
  const seconds = Number(now() - t0) / 1e9;
  const a = polls[0];
  const b = polls.at(-1);
  const channels = a && b && a !== b ? nat.irDelta(a.s, b.s) : [];
  const watts = (k) => {
    const v = nat.smcRead(k)[0]?.value;
    return Number.isFinite(v) && v >= 0 ? round(v, 2) : null;
  };

  const cores = [];
  let gpu = { mhz: null, usage: null };
  for (const c of channels) {
    if (!c.states) continue;
    if (c.group === 'CPU Stats') {
      const active = c.states.filter((s) => !INACTIVE.has(s.name)).length;
      const freqs = pickTable(tables, c.channel, active);
      const r = residency(c.states, freqs);
      cores.push({
        name: c.channel,
        cluster: c.channel.slice(0, -1),
        tier: null,
        mhz: r.mhz,
        usage: r.usage,
        minMhz: freqs[0] ?? null,
        maxMhz: freqs.at(-1) ?? null,
      });
    } else if (c.group === 'GPU Stats' && c.channel === 'GPUPH') {
      gpu = residency(c.states, tables.gpu);
    }
  }

  const gpuJ = energy(channels).gpu;
  const soc =
    polls.length > 1
      ? socPower(nat, polls)
      : { cpu: null, ane: null, dram: null };
  const span = a && b ? Number(b.t - a.t) / 1e9 : 0;
  const p = { ...soc, gpu: gpuJ !== null && span > 0 ? gpuJ / span : null };
  const tiers = cores.length ? cpuTiers(cores, info) : [];
  const total = cores.length ? summarize(cores) : null;

  return {
    interval: round(seconds * 1000, 0),
    cpu: {
      mhz: total?.mhz ?? null,
      usage: total?.usage ?? null,
      watts: round(p.cpu, 2),
      tiers,
      cores: cores.map((c) => ({
        name: c.name,
        tier: c.tier,
        mhz: round(c.mhz, 0),
        usage: round((c.usage ?? 0) * 100),
      })),
    },
    gpu: {
      mhz: round(gpu.mhz ?? tables.gpu[0], 0),
      maxMhz: round(tables.gpu.at(-1), 0),
      usage: gpu.usage === null ? null : round(gpu.usage * 100),
      watts: round(p.gpu, 2),
    },
    ane: { watts: round(p.ane, 2) },
    dram: { watts: round(p.dram, 2) },
    system: {
      watts: watts('PSTR'),
      adapterWatts: watts('PDTR'),
      socHeatWatts: watts('PHPC'),
    },
  };
}

module.exports = { version, chip, temperatures, fans, sample };
