package main

import (
"encoding/json"
"net/http"
"net/http/httptest"
"testing"

"slicer-api/internal/config"
"slicer-api/internal/handlers"
"slicer-api/internal/services"
"slicer-api/pkg/logger"

"github.com/gin-gonic/gin"
"github.com/stretchr/testify/assert"
)

func setupTestRouter() *gin.Engine {
gin.SetMode(gin.TestMode)

// Setup minimal configuration for tests
cfg := &config.Config{
Server: config.ServerConfig{Port: "8080"},
Logging: config.LoggingConfig{Level: "info"},
}

// Initialize logger
appLogger := logger.New(&cfg.Logging)

// Initialize services
systemService := services.NewSystemService(appLogger)

// Initialize handlers
systemHandler := handlers.NewSystemHandler(systemService, appLogger)

// Setup router
return setupRouter(cfg, appLogger, systemHandler)
}

func TestHealthEndpoint(t *testing.T) {
router := setupTestRouter()

w := httptest.NewRecorder()
req, _ := http.NewRequest("GET", "/api/v1/health", nil)
router.ServeHTTP(w, req)

assert.Equal(t, http.StatusOK, w.Code)

var response map[string]interface{}
err := json.Unmarshal(w.Body.Bytes(), &response)
assert.NoError(t, err)
assert.Contains(t, response, "status")
}

func TestInfoEndpoint(t *testing.T) {
router := setupTestRouter()

w := httptest.NewRecorder()
req, _ := http.NewRequest("GET", "/api/v1/info", nil)
router.ServeHTTP(w, req)

assert.Equal(t, http.StatusOK, w.Code)

var response map[string]interface{}
err := json.Unmarshal(w.Body.Bytes(), &response)
assert.NoError(t, err)
assert.Contains(t, response, "version")
}

func TestMetricsEndpoint(t *testing.T) {
router := setupTestRouter()

w := httptest.NewRecorder()
req, _ := http.NewRequest("GET", "/api/v1/metrics", nil)
router.ServeHTTP(w, req)

assert.Equal(t, http.StatusOK, w.Code)

var response map[string]interface{}
err := json.Unmarshal(w.Body.Bytes(), &response)
assert.NoError(t, err)
assert.Contains(t, response, "requests_total")
assert.Contains(t, response, "uptime")
}

func TestRootEndpoint(t *testing.T) {
router := setupTestRouter()

w := httptest.NewRecorder()
req, _ := http.NewRequest("GET", "/", nil)
router.ServeHTTP(w, req)

assert.Equal(t, http.StatusOK, w.Code)

var response map[string]interface{}
err := json.Unmarshal(w.Body.Bytes(), &response)
assert.NoError(t, err)
assert.Equal(t, "Slicer API", response["message"])
assert.Equal(t, "1.0.0", response["version"])
}
