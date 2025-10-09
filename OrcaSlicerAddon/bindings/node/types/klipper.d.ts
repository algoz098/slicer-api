/**
 * Type definitions for Klipper/Moonraker integration
 */

export interface KlipperConfig {
  /** Printer hostname or IP (e.g., "k2plus.local" or "192.168.1.100") */
  host: string;
  /** Moonraker port (default: 7125) */
  port?: number;
  /** API Key for authentication */
  apiKey?: string;
  /** Use HTTPS (default: false) */
  ssl?: boolean;
  /** Request timeout in milliseconds (default: 30000) */
  timeout?: number;
}

export interface TestResult {
  success: boolean;
  version?: string;
  klippy_connected?: boolean;
  klippy_state?: string;
  error?: string;
}

export interface PrinterStatus {
  success: boolean;
  bed?: {
    temperature: number;
    target: number;
  };
  extruder?: {
    temperature: number;
    target: number;
  };
  print?: {
    state: string;
    filename: string;
    duration: number;
    progress: number;
  };
  position?: {
    x: number;
    y: number;
    z: number;
  };
  error?: string;
}

export interface FileInfo {
  path: string;
  modified: number;
  size: number;
  permissions: string;
}

export interface FileListResult {
  success: boolean;
  files?: FileInfo[];
  error?: string;
}

export interface UploadProgress {
  loaded: number;
  total: number;
  percentage: number;
}

export interface UploadOptions {
  /** Remote filename (defaults to local filename) */
  filename?: string;
  /** Select file after upload */
  select?: boolean;
  /** Start printing after upload */
  print?: boolean;
  /** Progress callback */
  onProgress?: (progress: UploadProgress) => void;
}

export interface UploadResult {
  success: boolean;
  filename?: string;
  print_started?: boolean;
  error?: string;
  details?: any;
}

export interface MetadataResult {
  success: boolean;
  metadata?: {
    filename: string;
    size: number;
    modified: number;
    slicer?: string;
    slicer_version?: string;
    filament_total?: number;
    filament_type?: string;
    estimated_time?: number;
    first_layer_height?: number;
    layer_height?: number;
    object_height?: number;
    nozzle_diameter?: number;
  };
  error?: string;
}

export interface SimpleResult {
  success: boolean;
  error?: string;
}

/**
 * Klipper/Moonraker Client
 */
export class KlipperClient {
  constructor(config: KlipperConfig);
  
  /** Test connection to Moonraker */
  test(): Promise<TestResult>;
  
  /** Get printer status */
  getStatus(): Promise<PrinterStatus>;
  
  /** List files on printer */
  listFiles(root?: string): Promise<FileListResult>;
  
  /** Upload GCode file to printer */
  uploadFile(filePath: string, options?: UploadOptions): Promise<UploadResult>;
  
  /** Start printing a file */
  startPrint(filename: string): Promise<SimpleResult>;
  
  /** Pause current print */
  pausePrint(): Promise<SimpleResult>;
  
  /** Resume current print */
  resumePrint(): Promise<SimpleResult>;
  
  /** Cancel current print */
  cancelPrint(): Promise<SimpleResult>;
  
  /** Delete a file from printer */
  deleteFile(filename: string): Promise<SimpleResult>;
  
  /** Get file metadata */
  getMetadata(filename: string): Promise<MetadataResult>;
}

export interface SliceOptions {
  /** Output GCode path (auto-generated if not provided) */
  outputPath?: string;
  /** Slicer configuration overrides */
  config?: Record<string, any>;
  /** Progress callback */
  onProgress?: (progress: any) => void;
}

export interface SliceResult {
  success: boolean;
  outputPath?: string;
  stats?: Record<string, any>;
  error?: string;
}

export interface SliceAndSendOptions {
  /** Slicer configuration */
  sliceConfig?: Record<string, any>;
  /** Remote filename */
  filename?: string;
  /** Select file after upload */
  select?: boolean;
  /** Start printing after upload */
  print?: boolean;
  /** Keep local GCode file after upload */
  keepLocal?: boolean;
  /** Slice progress callback */
  onSliceProgress?: (progress: any) => void;
  /** Upload progress callback */
  onUploadProgress?: (progress: UploadProgress) => void;
}

export interface SliceAndSendResult {
  success: boolean;
  printer?: string;
  localPath?: string | null;
  remotePath?: string;
  printStarted?: boolean;
  stats?: Record<string, any>;
  error?: string;
}

/**
 * Slice and Send Manager
 */
export class SliceAndSend {
  constructor(addon: any);
  
  /** Add a printer configuration */
  addPrinter(name: string, config: KlipperConfig): KlipperClient;
  
  /** Get printer client by name */
  getPrinter(name: string): KlipperClient | undefined;
  
  /** Remove printer */
  removePrinter(name: string): boolean;
  
  /** List all configured printers */
  listPrinters(): string[];
  
  /** Test connection to a printer */
  testPrinter(name: string): Promise<TestResult>;
  
  /** Slice a 3D model */
  slice(inputPath: string, options?: SliceOptions): Promise<SliceResult>;
  
  /** Slice and upload to printer */
  sliceAndSend(inputPath: string, printerName: string, options?: SliceAndSendOptions): Promise<SliceAndSendResult>;
  
  /** Slice, upload, and start printing */
  sliceAndPrint(inputPath: string, printerName: string, options?: SliceAndSendOptions): Promise<SliceAndSendResult>;
  
  /** Get status of all configured printers */
  getAllPrinterStatus(): Promise<Record<string, PrinterStatus>>;
  
  /** Upload existing GCode file to printer */
  sendFile(gcodeFilePath: string, printerName: string, options?: UploadOptions): Promise<UploadResult>;
}

