```
                        ___  ____
  _ __ ___   __ _  ___ / _ \/ ___|       ___ _ __ ___   ___
 | '_ ` _ \ / _` |/ __| | | \___ \ _____/ __| '_ ` _ \ / __|
 | | | | | | (_| | (__| |_| |___) |_____\__ \ | | | | | (__
 |_| |_| |_|\__,_|\___|\___/|____/      |___/_| |_| |_|\___|

```

# macos-smc

Hardware sensor library for [node.js][nodejs-url] on macOS (Apple Silicon): CPU/GPU temperatures, CPU/GPU frequencies and utilization, power draw and fans.

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

## Reference

| Function                   | Result                                                                                                                        | Source                    |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| `version()`                | library version                                                                                                               |                           |
| `chip()`                   | chip name, model, generation, variant, CPU cores per tier, GPU cores, memory                                                  | sysctl, IORegistry        |
| `temperatures()`           | CPU / GPU (max, avg, per sensor), battery, SSD, WiFi                                                                          | SMC                       |
| `fans()`                   | rpm, min, max, target, percent per fan                                                                                        | SMC                       |
| `sample(intervalMs = 500)` | CPU MHz / usage (total, per tier, per core), GPU MHz / usage, CPU / GPU / ANE / DRAM watts, system / adapter / SoC heat watts | IOReport, IORegistry, SMC |

- Unavailable values are `null`. Fanless machines return `[]` from `fans()`.
- `mhz` is the residency-weighted frequency while active. `usage` is the active residency.
- Details on the SMC keys and data sources: [docs/smc-keys.md](docs/smc-keys.md)

## Known Issues

- On macOS 27, CPU, ANE and DRAM energy counters update only every ~2 s. The first `sample()` call may therefore return `null` for these watts; later calls reuse the last counter update.
- Verified on M5 Max. M1–M4 and Ultra are implemented from public reverse-engineering sources (see [docs/smc-keys.md](docs/smc-keys.md)). Reports from other chips are welcome via [issues][issues-url].

## News and Changes

| Version | Date       | Comment         |
| ------- | ---------- | --------------- |
| 0.1.0   | 2026-09-25 | initial release |

## Credits

Written by Sebastian Hildebrandt [sebhildebrandt](https://github.com/sebhildebrandt)

## Copyright Information

Apple and macOS are registered trademarks of Apple Inc., Node.js is a trademark of OpenJS Foundation.
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
