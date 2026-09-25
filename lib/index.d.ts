export interface SmcValue {
  key: string;
  value: number;
}

export interface CoreTier {
  /** perflevel name, e.g. "Performance", "Efficiency", "Super" */
  name: string | null;
  cores: number;
}

export interface Chip {
  /** e.g. "Apple M5 Max" */
  name: string | null;
  /** e.g. "Mac17,7" */
  model: string | null;
  generation: number | null;
  variant: 'Pro' | 'Max' | 'Ultra' | null;
  cpuCores: number | null;
  /** perflevel0 = fastest */
  tiers: CoreTier[];
  gpuCores: number | null;
  memoryBytes: number | null;
}

export interface SensorGroup {
  max: number | null;
  avg: number | null;
  sensors: SmcValue[];
}

/** °C */
export interface Temperatures {
  cpu: SensorGroup;
  gpu: SensorGroup;
  battery: number | null;
  ssd: number | null;
  wifi: number | null;
}

export interface Fan {
  id: number;
  rpm: number;
  min: number | null;
  max: number | null;
  target: number | null;
  /** rpm position within min..max, 0–100 */
  percent: number | null;
}

export interface CpuTierSample {
  name: string | null;
  cores: number;
  /** residency-weighted active frequency */
  mhz: number | null;
  maxMhz: number | null;
  /** 0–100 */
  usage: number | null;
}

export interface CpuCoreSample {
  /** IOReport channel, e.g. "PCPU0" */
  name: string;
  tier: string | null;
  mhz: number | null;
  usage: number | null;
}

export interface Sample {
  /** actual measured interval in ms */
  interval: number;
  cpu: {
    mhz: number | null;
    usage: number | null;
    watts: number | null;
    tiers: CpuTierSample[];
    cores: CpuCoreSample[];
  };
  gpu: {
    mhz: number | null;
    maxMhz: number | null;
    usage: number | null;
    watts: number | null;
  };
  ane: { watts: number | null };
  dram: { watts: number | null };
  system: {
    /** SMC PSTR, total system power */
    watts: number | null;
    /** SMC PDTR, AC adapter input power */
    adapterWatts: number | null;
    /** SMC PHPC, SoC heat dissipation estimate */
    socHeatWatts: number | null;
  };
}

export function version(): string;
export function chip(): Chip;
export function temperatures(): Temperatures;
export function fans(): Fan[];
export function sample(intervalMs?: number): Promise<Sample>;
