# SMC keys and data sources on Apple Silicon

Apple does not document SMC keys. Everything below is from community reverse engineering (see [Sources](#sources)) plus our own measurements. "Verified" means measured on hardware or labelled by the Asahi Linux kernel driver.

Measured on: **M5 Max** (Mac17,7, macOS 27.0), idle vs. 15 s full load on 18 threads.

## Temperature keys per chip

All temperature keys are type `flt` (IEEE-754, little-endian), °C.

| Chip | CPU P-cores | CPU E-cores / other tier | GPU | Source |
| --- | --- | --- | --- | --- |
| M1 | Tp01, Tp05, Tp0D, Tp0H, Tp0L, Tp0P, Tp0X, Tp0b | Tp09, Tp0T | Tg05, Tg0D, Tg0L, Tg0T | [stats] |
| M2 | Tp01, Tp05, Tp09, Tp0D, Tp0X, Tp0b, Tp0f, Tp0j | Tp1h, Tp1t, Tp1p, Tp1l | Tg0f, Tg0j | [stats] |
| M3 | Tp04–06, Tp0C–0E, Tp0K–0M, Tp0a–0c, Tp0g–0i, Tp0m–0o, Tp1E–1G, Tp1Q–1S, Tp0y/0z/10 | Te04–06, Te0G–0I, Te0P/0Q, Te0R–0T, Te0U (Max also Te0K–0M) | Tg04/05, Tg0C/0D, Tg0K/0L (+ Pro/Max extras) | [iSMC] |
| M3 (alt.) | Tf04, 09, 0A, 0B, 0D, 0E, 44, 49, 4A, 4B, 4D, 4E | Te05, Te0L, Te0P, Te0S | Tf14, 18, 19, 1A, 24, 28, 29, 2A | [stats] — contradicts iSMC, raw dumps favour iSMC |
| M4 | Tp01, Tp05, Tp09, Tp0D, Tp0V, Tp0Y, Tp0b, Tp0e | Te05, Te0S, Te09, Te0H | base: Tg0G, Tg0H; Pro/Max: Tg1U, Tg1k; all: Tg0K, Tg0L, Tg0d, Tg0e, Tg0j, Tg0k | [stats] |
| M5 | Super: Tp00, 04, 08, 0C, 0G, 0K · Performance: Tp0O, 0R, 0U, 0X, 0a, 0d, 0g, 0j, 0m, 0p, 0u, 0y | none on Pro/Max; Tp1E, 1I, 1Q, 1U, 1g = cluster aggregates | Tg* (42–84 keys) | [stats], [iSMC], measured |
| M6 | unknown | | | |

**Library rule** (works across all generations above): CPU = `Tp*` + `Te*`, GPU = `Tg*`, value 15–120 °C, drop exactly 40.0. Per-chip allowlists are not used because the sources disagree.

## M5 Max: measured key behaviour

| Prefix | Keys | Idle → load | Meaning | Used |
| --- | --- | --- | --- | --- |
| Tp | 23 | 49 → 91 | CPU cores + cluster aggregates | CPU |
| Tg | 84 | 46 → 60 | GPU | GPU |
| Tf | 20 | 45 → 71; **Tf06 = 90.8, Tf16 = 87.4 constant** | GPU fabric; Tf?5/Tf?6 = static min/max | no |
| Tm | 40 | 45 → 81 | memory (per iSMC, unverified) | no |
| Ta | 13 | 48 → 73 | CPU die thermal headroom (iSMC), not a temperature | no |
| Ts | 15 | 45 → 77 | unknown (macmon counts it as CPU) | no |
| TC | 3 | TCMb 55 → 96 | TCMb = CPU die average (iSMC) | no |
| TB | 3 | 33.7, static | battery (Asahi) | `battery` |
| TH | 5 | TH0x 35 | SSD/NAND (Asahi); TH1a/b = 0 | `ssd` |
| TW | 1 | TW0P 43 → 46 | WiFi (Asahi) | `wifi` |
| TD, TN, TV, TR, TU, TP, TS | | partly static or 0 | board / unknown | no |
| Tz | 9 | 0.0 | inactive | no |
| TT | 1 | TTPD = -306783232 | garbage | no |

**Sentinels seen in dumps:**
- M4 Max idle: every `Tp*` reads exactly 40.0, and `Tp1i..Tp2X` read -4 / 0 / 2.5 / 4 / 5.2.
- M3 Max: `Tp1g..Tp3j` read 40.0.
- `TVMX` / `TVmS` read a constant value.
- Inactive slots read 0 or a negative value.

## Power keys

| Key | Meaning | Source | Used |
| --- | --- | --- | --- |
| PSTR | total system power, W | Asahi | `system.watts` |
| PDTR | AC adapter input power, W | Asahi | `system.adapterWatts` |
| PHPC | heatpipe power = SoC heat dissipation estimate, W | Asahi | `system.socHeatWatts` |
| PMVR | 3.8 V rail power | Asahi | no |
| PPBR | battery power | stats, iSMC (community) | no |
| PZC0, PZC1, PHPS, PPSM | react strongly to CPU load (M5 Max: ~4 → ~60 W) | undocumented | no |

No SMC key gives CPU/GPU/ANE/DRAM power; the library uses IOReport for these (see below).

## Fan keys

| Key | Type | Meaning |
| --- | --- | --- |
| FNum | ui8 | fan count (0 on fanless models) |
| F<n>Ac | flt | actual rpm |
| F<n>Mn / F<n>Mx | flt | min / max rpm |
| F<n>Tg | flt | target rpm |
| F<n>Md (M1–M4), F<n>md (M5) | ui8 | mode (write only relevant; not used) |

`fpe2` is the Intel format and does not appear on Apple Silicon.

## Data types

- `flt`: IEEE-754, little-endian.
- `ui8` / `ui16` / `ui32`: decoded big-endian, as stats, iSMC and macmon do. Asahi reads them little-endian and marks some keys as "reverse byte order".
- Also seen: `ioft` (48.16 fixed point), `sp78`, `flag`, `hex_`, `ch8*`, `si*`. The library only reads `flt`/`ui*`.

## IOReport (not SMC)

Loaded from `/usr/lib/libIOReport.dylib` via `dlsym`; no sudo.

| Group / subgroup | Channels | Used for |
| --- | --- | --- |
| Energy Model | `CPU Energy` (Ultra: `DIE_n_CPU Energy`), `GPU Energy`, `ANE*`, `DRAM*`; units mJ/uJ/nJ | watts |
| CPU Stats / CPU Core Performance States | one per core (M1–M4: `ECPU*`/`PCPU*`; M5 Max: `MCPU0x`/`MCPU1x` = Performance, `PCPUx` = Super) | CPU MHz, usage |
| GPU Stats / GPU Performance States | `GPUPH` | GPU MHz, usage |

Inactive states are `IDLE`, `DOWN` and `OFF`; the first active state maps to the lowest frequency table entry.

**macOS 27:** CPU, ANE and DRAM energy update only every ~2.1 s, often followed by a small chunk ~25 ms later. GPU energy updates every ~50 ms. Other tools report this as "CPU power stuck at 0" ([macmon #76], [stats #3608]).

## DVFS frequency tables (IORegistry `pmgr`)

`voltage-states<N>[-sram]` hold u32 `[frequency, voltage]` pairs. Units: Hz up to M3, kHz for the M4+ CPU tables (u32 in Hz cannot exceed 4.29 GHz).

| Chip | CPU tables | GPU table |
| --- | --- | --- |
| M1–M4 | E = `voltage-states1-sram`, P = `voltage-states5-sram` | `voltage-states9` |
| M5+ | `acc-clusters` → table index per cluster (low byte of every 2nd u32) | `voltage-states9` |

M5 Max, measured:
- `acc-clusters` = 22, 23, 5.
- Tables 22 and 23: 1344–4380 MHz, 15 states (Performance).
- Table 5: 1308–4608 MHz, 20 states (Super).
- GPU table 9: 338–1620 MHz, 13 entries, while IOReport reports 15 GPU states.

Caveat: on M4, IOReport shows the *requested* DVFS state, so throttling is not visible ([macmon #78]).

## Sources

- [iSMC] — dkorunic/iSMC `src/temp.txt` and raw dumps in `internal/reports/`: https://github.com/dkorunic/iSMC
- [stats] — exelban/stats `Modules/Sensors/values.swift`: https://github.com/exelban/stats/blob/master/Modules/Sensors/values.swift
- macmon — vladkens/macmon `src_lib/metrics.rs`, `src_lib/sources.rs`: https://github.com/vladkens/macmon
- mactop — context-labs/mactop: https://github.com/context-labs/mactop
- Asahi Linux: `drivers/hwmon/macsmc-hwmon.c`, `arch/arm64/boot/dts/apple/hwmon-*.dtsi`, `include/linux/mfd/macsmc.h`: https://github.com/torvalds/linux, https://asahilinux.org/docs/hw/soc/smc/
- iSMC issues on M5: https://github.com/dkorunic/iSMC/issues/31, https://github.com/dkorunic/iSMC/issues/34

[iSMC]: https://github.com/dkorunic/iSMC
[stats]: https://github.com/exelban/stats
[macmon #76]: https://github.com/vladkens/macmon/issues/76
[macmon #78]: https://github.com/vladkens/macmon/issues/78
[stats #3608]: https://github.com/exelban/stats/issues/3608
