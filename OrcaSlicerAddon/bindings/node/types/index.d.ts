export interface InitializeOptions {
  resourcesPath?: string;
  verbose?: boolean;
  vendors?: string[];
}

export interface ModelInfo {
  filename: string;
  objectCount: number;
  triangleCount: number;
  volume: number;
  boundingBox: string;
  isValid: boolean;
}

export interface SliceParams {
  input: string;
  output?: string;
  plate?: number; // 1-based
  verbose?: boolean;
  dryRun?: boolean;
  // Behavior flags
  center?: boolean; // center object(s) on bed before slicing (default false)
  autoRealignIfNeeded?: boolean; // automatically realign on bed if elements are out-of-bounds
  // Base profile (applied before 3MF load; 3MF settings override these)
  profile?: Record<string, string | number | boolean | (string | number | boolean)[]>;
  // Explicit user overrides (applied after 3MF load; override 3MF settings)
  options?: Record<string, string | number | boolean | (string | number | boolean)[]>;
  // Back-compat: custom (string-only)
  custom?: Record<string, string>;
}

export interface VendorBundle {
  vendor: string; // e.g., 'BBL'
  vendorJson: string; // content of <vendor>.json
  files: Record<string, string>; // map of relative paths to file contents (e.g., 'BBL/machine/..' or 'machine/...')
}

export function initialize(opts?: InitializeOptions): void;
export function version(): string;
export function getModelInfo(file: string): Promise<ModelInfo>;
export function slice(params: SliceParams): Promise<{ output: string; usedOptions?: string[]; ignoredOptions?: string[]; estimatedTimeSec?: number; filamentUsedGrams?: number }>;

// Lazy vendor loading controls (synchronous, must call after initialize)
export function loadVendor(vendorId: string): void;

// Global logging control
export function setLoggingSilenced(silent: boolean): void;

// Klipper/Moonraker integration
export { KlipperClient, SliceAndSend } from './klipper';
