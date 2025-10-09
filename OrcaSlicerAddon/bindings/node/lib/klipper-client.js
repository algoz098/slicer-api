/**
 * Klipper/Moonraker Client for OrcaSlicerAddon
 * 
 * Provides upload and control functionality for Klipper-based printers (K2 Plus, etc.)
 * Compatible with Moonraker API
 * 
 * @module klipper-client
 */

'use strict';

const axios = require('axios');
const fs = require('fs');
const path = require('path');
const FormData = require('form-data');

/**
 * Klipper/Moonraker Client
 */
class KlipperClient {
  /**
   * @param {Object} config - Configuration
   * @param {string} config.host - Printer hostname or IP (e.g., "k2plus.local" or "192.168.1.100")
   * @param {number} [config.port=7125] - Moonraker port
   * @param {string} [config.apiKey] - API Key for authentication
   * @param {boolean} [config.ssl=false] - Use HTTPS
   * @param {number} [config.timeout=30000] - Request timeout in ms
   */
  constructor(config) {
    if (!config || !config.host) {
      throw new Error('KlipperClient: host is required');
    }

    this.host = config.host;
    this.port = config.port || 7125;
    this.apiKey = config.apiKey || '';
    this.ssl = config.ssl || false;
    this.timeout = config.timeout || 30000;

    this.baseURL = `${this.ssl ? 'https' : 'http'}://${this.host}:${this.port}`;
    
    this.axios = axios.create({
      baseURL: this.baseURL,
      timeout: this.timeout,
      headers: this.apiKey ? { 'X-Api-Key': this.apiKey } : {}
    });
  }

  /**
   * Test connection to Moonraker
   * @returns {Promise<Object>} Version info
   */
  async test() {
    try {
      const response = await this.axios.get('/api/version');
      return {
        success: true,
        version: response.data.result.version,
        klippy_connected: response.data.result.klippy_connected,
        klippy_state: response.data.result.klippy_state
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }

  /**
   * Get printer status
   * @returns {Promise<Object>} Printer status
   */
  async getStatus() {
    try {
      const response = await this.axios.get('/api/printer/objects/query', {
        params: {
          heater_bed: '',
          extruder: '',
          print_stats: '',
          toolhead: ''
        }
      });
      
      const status = response.data.result.status;
      
      return {
        success: true,
        bed: {
          temperature: status.heater_bed?.temperature || 0,
          target: status.heater_bed?.target || 0
        },
        extruder: {
          temperature: status.extruder?.temperature || 0,
          target: status.extruder?.target || 0
        },
        print: {
          state: status.print_stats?.state || 'unknown',
          filename: status.print_stats?.filename || '',
          duration: status.print_stats?.print_duration || 0,
          progress: status.print_stats?.progress || 0
        },
        position: {
          x: status.toolhead?.position?.[0] || 0,
          y: status.toolhead?.position?.[1] || 0,
          z: status.toolhead?.position?.[2] || 0
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
   * List files on printer
   * @param {string} [root='gcodes'] - Root directory
   * @returns {Promise<Object>} File list
   */
  async listFiles(root = 'gcodes') {
    try {
      const response = await this.axios.get('/api/server/files/list', {
        params: { root }
      });
      
      return {
        success: true,
        files: response.data.result || []
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }

  /**
   * Upload GCode file to printer
   * @param {string} filePath - Path to local GCode file
   * @param {Object} [options] - Upload options
   * @param {string} [options.filename] - Remote filename (defaults to local filename)
   * @param {boolean} [options.select=false] - Select file after upload
   * @param {boolean} [options.print=false] - Start printing after upload
   * @param {Function} [options.onProgress] - Progress callback (progress) => {}
   * @returns {Promise<Object>} Upload result
   */
  async uploadFile(filePath, options = {}) {
    try {
      // Validate file exists
      if (!fs.existsSync(filePath)) {
        throw new Error(`File not found: ${filePath}`);
      }

      // Validate it's a .gcode file
      if (!filePath.toLowerCase().endsWith('.gcode')) {
        throw new Error('Only .gcode files are supported for Klipper');
      }

      const filename = options.filename || path.basename(filePath);
      const form = new FormData();
      
      form.append('file', fs.createReadStream(filePath), {
        filename: filename,
        contentType: 'application/octet-stream'
      });
      
      if (options.select) {
        form.append('select', 'true');
      }
      
      if (options.print) {
        form.append('print', 'true');
      }

      const response = await this.axios.post('/api/files/local', form, {
        headers: {
          ...form.getHeaders(),
          ...(this.apiKey ? { 'X-Api-Key': this.apiKey } : {})
        },
        maxContentLength: Infinity,
        maxBodyLength: Infinity,
        onUploadProgress: (progressEvent) => {
          if (options.onProgress && progressEvent.total) {
            const progress = {
              loaded: progressEvent.loaded,
              total: progressEvent.total,
              percentage: Math.round((progressEvent.loaded / progressEvent.total) * 100)
            };
            options.onProgress(progress);
          }
        }
      });

      return {
        success: true,
        filename: response.data.result?.item?.path || filename,
        print_started: response.data.result?.print_started || false
      };
    } catch (error) {
      return {
        success: false,
        error: error.message,
        details: error.response?.data
      };
    }
  }

  /**
   * Start printing a file
   * @param {string} filename - Filename to print
   * @returns {Promise<Object>} Result
   */
  async startPrint(filename) {
    try {
      await this.axios.post('/api/printer/print/start', {
        filename: filename
      });
      
      return {
        success: true,
        filename: filename
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }

  /**
   * Pause current print
   * @returns {Promise<Object>} Result
   */
  async pausePrint() {
    try {
      await this.axios.post('/api/printer/print/pause');
      return { success: true };
    } catch (error) {
      return { success: false, error: error.message };
    }
  }

  /**
   * Resume current print
   * @returns {Promise<Object>} Result
   */
  async resumePrint() {
    try {
      await this.axios.post('/api/printer/print/resume');
      return { success: true };
    } catch (error) {
      return { success: false, error: error.message };
    }
  }

  /**
   * Cancel current print
   * @returns {Promise<Object>} Result
   */
  async cancelPrint() {
    try {
      await this.axios.post('/api/printer/print/cancel');
      return { success: true };
    } catch (error) {
      return { success: false, error: error.message };
    }
  }

  /**
   * Delete a file from printer
   * @param {string} filename - Filename to delete
   * @returns {Promise<Object>} Result
   */
  async deleteFile(filename) {
    try {
      await this.axios.delete(`/api/server/files/gcodes/${filename}`);
      return { success: true };
    } catch (error) {
      return { success: false, error: error.message };
    }
  }

  /**
   * Get file metadata
   * @param {string} filename - Filename
   * @returns {Promise<Object>} Metadata
   */
  async getMetadata(filename) {
    try {
      const response = await this.axios.get('/api/server/files/metadata', {
        params: { filename }
      });
      
      return {
        success: true,
        metadata: response.data.result
      };
    } catch (error) {
      return {
        success: false,
        error: error.message
      };
    }
  }
}

module.exports = KlipperClient;

