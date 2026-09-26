# macos-smc

Hardware sensor library for [node.js][nodejs-url] on macOS (Apple Silicon): CPU/GPU temperatures, CPU/GPU frequencies and utilization, power draw and fans.

```
                        ___  ____
  _ __ ___   __ _  ___ / _ \/ ___|       ___ _ __ ___   ___
 | '_ ` _ \ / _` |/ __| | | \___ \ _____/ __| '_ ` _ \ / __|
 | | | | | | (_| | (__| |_| |___) |_____\__ \ | | | | | (__
 |_| |_| |_|\__,_|\___|\___/|____/      |___/_| |_| |_|\___|

  macOS (Apple Silicon - ARM) - SMC hardware sensor library

```

[![NPM Version][npm-image]][npm-url]
[![NPM Downloads][downloads-image]][downloads-url]
[![Git Issues][issues-img]][issues-url]
[![MIT license][license-img]][license-url]

## Quick Start

- Zero runtime dependencies (native Node-API addon)
- macOS 12+, Apple Silicon (arm64) only
- No `sudo`, no child processes
- All temperatures in °C, power in W, frequencies in MHz, usage in percent (0–100)

### Installation

```bash
npm install macos-smc
```

Requires Xcode command-line tools (`xcode-select --install`).

### Usage

```js
const smc = require("macos-smc");

smc.chip();
// { name: 'Apple M5 Max', model: 'Mac17,7', generation: 5, variant: 'Max', cpuCores: 18,
//   tiers: [{ name: 'Super', cores: 6 }, { name: 'Performance', cores: 12 }], gpuCores: 40, memoryBytes }

smc.temperatures();
// { cpu: { max, avg, sensors }, gpu: { max, avg, sensors }, battery, ssd, wifi }

smc.fans();
// [{ id, rpm, min, max, target, percent }]

await smc.sample(1000);
// {
//   interval,
//   cpu: { mhz, usage, watts, tiers: [{ name, cores, mhz, maxMhz, usage }], cores: [{ name, tier, mhz, usage }] },
//   gpu: { mhz, maxMhz, usage, watts },
//   ane: { watts }, dram: { watts },
//   system: { watts, adapterWatts, socHeatWatts }
// }
```

## Reference

<table>
  <tr><th>Function</th><th>Result</th><th>Source</th></tr>
  <tr><td><code>version()</code></td><td>library version</td><td></td></tr>
  <tr><td><code>chip()</code></td><td>chip name, model, generation, variant, CPU cores per tier, GPU cores, memory</td><td>sysctl, IORegistry</td></tr>
  <tr><td></td><td colspan="2"><details><pre>
{
  "name": "Apple M5 Max",
  "model": "Mac17,7",
  "generation": 5,
  "variant": "Max",
  "cpuCores": 18,
  "tiers": [
    {
      "name": "Super",
      "cores": 6
    },
    {
      "name": "Performance",
      "cores": 12
    }
  ],
  "gpuCores": 40,
  "memoryBytes": 137438953472
}
</pre></details></td></tr>
  <tr><td><code>temperatures()</code></td><td>CPU / GPU (max, avg, per sensor), battery, SSD, WiFi</td><td>SMC</td></tr>
  <tr><td></td><td colspan="2"><details><pre>
{
  "cpu": {
    "max": 45.734375,
    "avg": 45.62907608695652,
    "sensors": [
      {
        "key": "Tp00",
        "value": 45.734375
      },
      {
        "key": "Tp04",
        "value": 45.671875
      },
      ...
    ]
  },
  "gpu": {
    "max": 45.4375,
    "avg": 44.899925595238095,
    "sensors": [
      {
        "key": "Tg08",
        "value": 45.015625
      },
      {
        "key": "Tg0C",
        "value": 45
      },
      ...
    ]
  },
  "battery": 32.96666463216146,
  "ssd": 35.97265625,
  "wifi": 43.059783935546875
}
</pre></details></td></tr>
  <tr><td><code>fans()</code></td><td>rpm, min, max, target, percent per fan</td><td>SMC</td></tr>
  <tr><td></td><td colspan="2"><details><pre>
[
  {
    "id": 0,
    "rpm": 2314,
    "min": 2317,
    "max": 7826,
    "target": 2317,
    "percent": 0
  },
  {
    "id": 1,
    "rpm": 2504,
    "min": 2317,
    "max": 7826,
    "target": 2502,
    "percent": 3.4
  }
]
</pre></details></td></tr>
  <tr><td><code>sample(intervalMs = 500)</code></td><td>CPU MHz / usage (total, per tier, per core), GPU MHz / usage, CPU / GPU / ANE / DRAM watts, system / adapter / SoC heat watts</td><td>IOReport, IORegistry, SMC</td></tr>
  <tr><td></td><td colspan="2"><details><pre>
{
  "interval": 1007,
  "cpu": {
    "mhz": 4370,
    "usage": 78,
    "watts": null,
    "tiers": [
      {
        "name": "Super",
        "cores": 6,
        "mhz": 4572,
        "maxMhz": 4608,
        "usage": 80.6
      },
      {
        "name": "Performance",
        "cores": 12,
        "mhz": 4264,
        "maxMhz": 4380,
        "usage": 76.7
      }
    ],
    "cores": [
      {
        "name": "MCPU00",
        "tier": "Performance",
        "mhz": 4186,
        "usage": 83.1
      },
      ...
      {
        "name": "PCPU0",
        "tier": "Super",
        "mhz": 4545,
        "usage": 83.2
      },
      ...
    ]
  },
  "gpu": {
    "mhz": 338,
    "maxMhz": 1620,
    "usage": 2.9,
    "watts": 0.03
  },
  "ane": {
    "watts": null
  },
  "dram": {
    "watts": null
  },
  "system": {
    "watts": 7,
    "adapterWatts": 0,
    "socHeatWatts": 36.12
  }
}
</pre></details></td></tr>
</table>

- Unavailable values are `null`. Fanless machines return `[]` from `fans()`.
- `mhz` is the residency-weighted frequency while active. `usage` is the active residency.
- Details on the SMC keys and data sources: [docs/smc-keys.md](docs/smc-keys.md)

## Known Issues

- On macOS 27, CPU, ANE and DRAM energy counters update only every ~2 s. The first `sample()` call may therefore return `null` for these watts; later calls reuse the last counter update.
- Verified on M5 Max. M1–M4 and Ultra are implemented from public reverse-engineering sources (see [docs/smc-keys.md](docs/smc-keys.md)). Reports from other chips are welcome via [issues][issues-url].

## News and Changes

| Version | Date       | Comment            |
| ------- | ---------- | ------------------ |
| 1.0.0   | 2026-09-26 | first major relase |
| 0.1.0   | 2026-09-25 | initial release    |

### Release Info

```
npm run build && npm test
npx biome check .
npm pack --dry-run

npm version patch/minor/major
git push
git push --tags
npm publish
```

## Credits

Written by Sebastian Hildebrandt [sebhildebrandt](https://github.com/sebhildebrandt)

## Copyright Information

Apple and macOS are registered trademarks of Apple Inc., Node.js is a trademark of OpenJS Foundation. ARM is a registered trademarks of Arm Limited.
All other trademarks are the property of their respective owners.

## License [![MIT license][license-img]][license-url]

> The [`MIT`][license-url] License (MIT)
>
> Copyright &copy; 2026 Sebastian Hildebrandt, [+innovations](http://www.plus-innovations.com).
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.
>
> Further details see [LICENSE](LICENSE) file.

[npm-image]: https://img.shields.io/npm/v/macos-smc.svg?style=flat-square
[npm-url]: https://npmjs.org/package/macos-smc
[downloads-image]: https://img.shields.io/npm/dm/macos-smc.svg?style=flat-square
[downloads-url]: https://npmjs.org/package/macos-smc
[license-url]: https://github.com/sebhildebrandt/macos-smc/blob/master/LICENSE
[license-img]: https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square
[issues-img]: https://img.shields.io/github/issues/sebhildebrandt/macos-smc.svg?style=flat-square
[issues-url]: https://github.com/sebhildebrandt/macos-smc/issues
[nodejs-url]: https://nodejs.org/en/
