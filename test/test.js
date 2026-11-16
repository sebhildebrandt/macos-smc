const assert = require('node:assert');
const smc = require('../lib/index.js');

const inRange = (v, lo, hi) => v === null || (v >= lo && v <= hi);

(async () => {
  console.log('macos-smc', smc.version());

  const c = smc.chip();
  console.log('Chip:', c);
  assert.ok(c.name?.startsWith('Apple M'));
  assert.ok(c.tiers.reduce((a, t) => a + t.cores, 0) === c.cpuCores);

  const t = smc.temperatures();
  console.log('Temperatures:', {
    cpu: t.cpu.max,
    gpu: t.gpu.max,
    battery: t.battery,
    ssd: t.ssd,
    wifi: t.wifi,
  });
  assert.ok(t.cpu.sensors.length > 0, 'no CPU temperature sensors');
  for (const v of [t.cpu.max, t.gpu.max, t.battery, t.ssd, t.wifi]) {
    assert.ok(inRange(v, 15, 120));
  }

  const f = smc.fans();
  console.log('Fans:', f);
  for (const fan of f) assert.ok(inRange(fan.percent, 0, 100));

  for (const bad of [Number.NaN, Infinity, -1, 60001, '500']) {
    await assert.rejects(smc.sample(bad), RangeError);
  }

  const s0 = await smc.sample(0);
  assert.ok(
    s0.gpu.usage !== null || s0.gpu.mhz === null,
    'gpu.mhz without data',
  );

  // macOS 27 updates CPU energy every ~2.1 s; a first call may not see two updates yet
  let s = await smc.sample(2500);
  if (s.cpu.watts === null) s = await smc.sample(2500);
  console.log(
    'Sample:',
    JSON.stringify(
      { ...s, cpu: { ...s.cpu, cores: s.cpu.cores.length } },
      null,
      2,
    ),
  );
  assert.strictEqual(s.cpu.cores.length, c.cpuCores);
  assert.deepStrictEqual(
    s.cpu.tiers.map((x) => x.name),
    c.tiers.map((x) => x.name),
  );
  for (const tier of s.cpu.tiers)
    assert.ok(tier.mhz > 0 && tier.mhz <= tier.maxMhz);
  assert.ok(inRange(s.cpu.usage, 0, 100) && inRange(s.gpu.usage, 0, 100));
  assert.ok(s.gpu.maxMhz > 0);
  assert.ok(s.cpu.watts > 0, 'no CPU power');
  assert.ok(inRange(s.system.watts, 0, 500));

  // first call measures itself, the next ones use the delta since the previous call
  const f1 = await smc.frequencies();
  await new Promise((r) => setTimeout(r, 200));
  const t0 = Date.now();
  const f2 = await smc.frequencies();
  console.log('Frequencies:', {
    interval: f2.interval,
    mhz: f2.cpu.mhz,
    tiers: f2.cpu.tiers,
  });
  assert.ok(f1.interval > 0 && f2.interval >= 200);
  assert.ok(Date.now() - t0 < 50, 'frequencies() should not wait');
  assert.strictEqual(f2.cpu.cores.length, c.cpuCores);
  for (const core of f2.cpu.cores) {
    assert.ok(core.minMhz > 0 && core.maxMhz >= core.minMhz);
    assert.ok(core.mhz === null || core.mhz <= core.maxMhz);
  }

  // F_NOCACHE on a descriptor node opened itself
  const fsp = require('node:fs/promises');
  const tmp = require('node:path').join(
    require('node:os').tmpdir(),
    `macos-smc-${process.pid}.tmp`,
  );
  const handle = await fsp.open(tmp, 'w');
  try {
    assert.strictEqual(smc.noCache(handle.fd), true);
  } finally {
    await handle.close();
    await fsp.rm(tmp, { force: true });
  }
  assert.throws(() => smc.noCache(-1), TypeError);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
