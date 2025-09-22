export interface InitializeOptions {
  resourcesPath?: string;
  verbose?: boolean;
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
  printerProfile?: string;
  filamentProfile?: string;
  processProfile?: string;
  verbose?: boolean;
  dryRun?: boolean;
  // Preferred: options (values coerced to string internally)
  options?: Record<string, string | number | boolean>;
  // Back-compat: custom (string-only)
  custom?: Record<string, string>;
}

export function initialize(opts?: InitializeOptions): void;
export function version(): string;
export function getModelInfo(file: string): Promise<ModelInfo>;
export function slice(params: SliceParams): Promise<{ output: string }>;
