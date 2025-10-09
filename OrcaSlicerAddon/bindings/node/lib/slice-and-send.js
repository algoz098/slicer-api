/**
 * Slice and Send - High-level API for slicing and sending to Klipper printers
 * 
 * Combines OrcaSlicerAddon slicing with KlipperClient upload
 * 
 * @module slice-and-send
 */

'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const KlipperClient = require('./klipper-client');

/**
 * Slice and Send Manager
 */
class SliceAndSend {
  /**
   * @param {Object} addon - OrcaSlicerAddon instance
   */
  constructor(addon) {
    if (!addon || typeof addon.slice !== 'function') {
      throw new Error('SliceAndSend: valid OrcaSlicerAddon instance required');
    }
    this.addon = addon;
    this.printers = new Map();
  }

  /**
   * Add a printer configuration
   * @param {string} name - Printer name/identifier
   * @param {Object} config - Printer configuration
   * @param {string} config.host - Printer hostname or IP
   * @param {number} [config.port=7125] - Moonraker port
   * @param {string} [config.apiKey] - API Key
   * @param {boolean} [config.ssl=false] - Use HTTPS
   * @returns {KlipperClient} Printer client
   */
  addPrinter(name, config) {
    const client = new KlipperClient(config);
    this.printers.set(name, client);
    return client;
  }

  /**
   * Get printer client by name
   * @param {string} name - Printer name
   * @returns {KlipperClient|undefined} Printer client
   */
  getPrinter(name) {
    return this.printers.get(name);
  }

  /**
   * Remove printer
   * @param {string} name - Printer name
   * @returns {boolean} Success
   */
  removePrinter(name) {
    return this.printers.delete(name);
  }

  /**
   * List all configured printers
   * @returns {string[]} Printer names
   */
  listPrinters() {
    return Array.from(this.printers.keys());
  }

  /**
   * Test connection to a printer
   * @param {string} name - Printer name
   * @returns {Promise<Object>} Test result
   */
  async testPrinter(name) {
    const printer = this.printers.get(name);
    if (!printer) {
      return { success: false, error: `Printer '${name}' not found` };
    }
    return await printer.test();
  }

  /**
   * Slice a 3D model
   * @param {string} inputPath - Path to input file (.stl, .3mf, etc.)
   * @param {Object} [options] - Slicing options
   * @param {string} [options.outputPath] - Output GCode path (auto-generated if not provided)
   * @param {Object} [options.config] - Slicer configuration overrides
   * @param {Function} [options.onProgress] - Progress callback
   * @returns {Promise<Object>} Slice result with outputPath
   */
  async slice(inputPath, options = {}) {
    try {
      // Validate input file exists
      if (!fs.existsSync(inputPath)) {
        throw new Error(`Input file not found: ${inputPath}`);
      }

      // Generate output path if not provided
      let outputPath = options.outputPath;
      if (!outputPath) {
        const basename = path.basename(inputPath, path.extname(inputPath));
        const tempDir = os.tmpdir();
        outputPath = path.join(tempDir, `${basename}_${Date.now()}.gcode`);
      }

      // Ensure output directory exists
      const outputDir = path.dirname(outputPath);
      if (!fs.existsSync(outputDir)) {
        fs.mkdirSync(outputDir, { recursive: true });
      }

      // Prepare slice options
      const sliceOptions = {
        input: inputPath,
        output: outputPath,
        ...options.config
      };

      // Call addon slice function
      // Note: addon.slice() returns { output: string, usedOptions?: string[], ignoredOptions?: string[], estimatedTimeSec?: number, filamentUsedGrams?: number }
      // It throws on error, so we wrap in try/catch
      const result = await this.addon.slice(sliceOptions);

      if (!result || !result.output) {
        throw new Error('Slicing failed: no output returned');
      }

      // Verify output file was created
      if (!fs.existsSync(outputPath)) {
        throw new Error(`Output file was not created: ${outputPath}`);
      }

      return {
        success: true,
        outputPath: outputPath,
        stats: {
          estimatedTimeSec: result.estimatedTimeSec,
          filamentUsedGrams: result.filamentUsedGrams,
          usedOptions: result.usedOptions,
          ignoredOptions: result.ignoredOptions
        }
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }

  /**
   * Slice and upload to printer
   * @param {string} inputPath - Path to input file
   * @param {string} printerName - Printer name
   * @param {Object} [options] - Options
   * @param {Object} [options.sliceConfig] - Slicer configuration
   * @param {string} [options.filename] - Remote filename
   * @param {boolean} [options.select=false] - Select file after upload
   * @param {boolean} [options.print=false] - Start printing after upload
   * @param {boolean} [options.keepLocal=false] - Keep local GCode file after upload
   * @param {Function} [options.onSliceProgress] - Slice progress callback
   * @param {Function} [options.onUploadProgress] - Upload progress callback
   * @returns {Promise<Object>} Result
   */
  async sliceAndSend(inputPath, printerName, options = {}) {
    try {
      // Get printer
      const printer = this.printers.get(printerName);
      if (!printer) {
        throw new Error(`Printer '${printerName}' not found`);
      }

      // Test printer connection first
      const testResult = await printer.test();
      if (!testResult.success) {
        throw new Error(`Printer '${printerName}' is not reachable: ${testResult.error}`);
      }

      // Slice the model
      console.log(`Slicing ${inputPath}...`);
      const sliceResult = await this.slice(inputPath, {
        config: options.sliceConfig,
        onProgress: options.onSliceProgress
      });

      if (!sliceResult.success) {
        throw new Error(`Slicing failed: ${sliceResult.error}`);
      }

      console.log(`Slicing complete: ${sliceResult.outputPath}`);

      // Upload to printer
      console.log(`Uploading to printer '${printerName}'...`);
      const uploadResult = await printer.uploadFile(sliceResult.outputPath, {
        filename: options.filename,
        select: options.select,
        print: options.print,
        onProgress: options.onUploadProgress
      });

      if (!uploadResult.success) {
        throw new Error(`Upload failed: ${uploadResult.error}`);
      }

      console.log(`Upload complete: ${uploadResult.filename}`);

      // Clean up local file if requested
      if (!options.keepLocal) {
        try {
          fs.unlinkSync(sliceResult.outputPath);
          console.log(`Cleaned up local file: ${sliceResult.outputPath}`);
        } catch (err) {
          console.warn(`Failed to clean up local file: ${err.message}`);
        }
      }

      return {
        success: true,
        printer: printerName,
        localPath: options.keepLocal ? sliceResult.outputPath : null,
        remotePath: uploadResult.filename,
        printStarted: uploadResult.print_started,
        stats: sliceResult.stats
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }

  /**
   * Convenience method: slice, upload, and start printing
   * @param {string} inputPath - Path to input file
   * @param {string} printerName - Printer name
   * @param {Object} [options] - Options (same as sliceAndSend)
   * @returns {Promise<Object>} Result
   */
  async sliceAndPrint(inputPath, printerName, options = {}) {
    return await this.sliceAndSend(inputPath, printerName, {
      ...options,
      print: true
    });
  }

  /**
   * Get status of all configured printers
   * @returns {Promise<Object>} Status map
   */
  async getAllPrinterStatus() {
    const statuses = {};
    
    for (const [name, printer] of this.printers.entries()) {
      statuses[name] = await printer.getStatus();
    }
    
    return statuses;
  }

  /**
   * Upload existing GCode file to printer
   * @param {string} gcodeFilePath - Path to GCode file
   * @param {string} printerName - Printer name
   * @param {Object} [options] - Upload options
   * @returns {Promise<Object>} Result
   */
  async sendFile(gcodeFilePath, printerName, options = {}) {
    try {
      const printer = this.printers.get(printerName);
      if (!printer) {
        throw new Error(`Printer '${printerName}' not found`);
      }

      // Validate file
      if (!fs.existsSync(gcodeFilePath)) {
        throw new Error(`File not found: ${gcodeFilePath}`);
      }

      if (!gcodeFilePath.toLowerCase().endsWith('.gcode')) {
        throw new Error('Only .gcode files are supported');
      }

      // Upload
      const result = await printer.uploadFile(gcodeFilePath, options);
      
      return result;
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }
}

module.exports = SliceAndSend;

