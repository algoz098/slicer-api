package handlers

import (
	"net/http"

	"github.com/gin-gonic/gin"
	"slicer-api/internal/services"
	"slicer-api/pkg/errors"
	"slicer-api/pkg/logger"
)

// SystemHandler manipula requests relacionados ao sistema
type SystemHandler struct {
	systemService services.SystemService
	logger        logger.Logger
}

// NewSystemHandler cria uma nova instância do SystemHandler
func NewSystemHandler(systemService services.SystemService, logger logger.Logger) *SystemHandler {
	return &SystemHandler{
		systemService: systemService,
		logger:        logger,
	}
}

// GetHealth godoc
// @Summary Verificar saúde do sistema
// @Tags system
// @Produce json
// @Success 200 {object} domain.HealthStatus
// @Router /health [get]
func (h *SystemHandler) GetHealth(c *gin.Context) {
	health, err := h.systemService.GetHealth()
	if err != nil {
		h.logger.Error("Erro ao verificar health", "error", err)
		apiErr := errors.InternalServerError("Erro interno do servidor")
		c.JSON(apiErr.StatusCode, apiErr.ToResponse())
		return
	}

	// Se algum componente não estiver saudável, retorna 503
	status := http.StatusOK
	if health.Status != "healthy" {
		status = http.StatusServiceUnavailable
	}

	c.JSON(status, health)
}

// GetSystemInfo godoc
// @Summary Obter informações do sistema
// @Tags system
// @Produce json
// @Success 200 {object} map[string]interface{}
// @Router /info [get]
func (h *SystemHandler) GetSystemInfo(c *gin.Context) {
	info := map[string]interface{}{
		"version":     "1.0.0",
		"build_date":  "2024-01-15T10:30:00Z",
		"go_version":  "1.21.0",
		"commit_hash": "abc123def456",
		"environment": "development",
		"features": map[string]bool{
			"file_upload":     false,
			"cache_enabled":   false,
			"queue_enabled":   false,
			"metrics_enabled": false,
			"auth_enabled":    false,
		},
		"limits": map[string]interface{}{
			"max_file_size":      "100MB",
			"max_concurrent_jobs": 1,
			"job_timeout":        "1h",
			"cache_ttl":          "24h",
		},
	}

	c.JSON(http.StatusOK, info)
}

// GetMetrics godoc
// @Summary Obter métricas do sistema
// @Tags system
// @Produce json
// @Success 200 {object} map[string]interface{}
// @Router /metrics [get]
func (h *SystemHandler) GetMetrics(c *gin.Context) {
	metrics := map[string]interface{}{
		"requests_total":        0,
		"requests_duration_avg": "0ms",
		"memory_usage":          "0MB",
		"cpu_usage":             "0%",
		"goroutines":            0,
		"uptime":                "0s",
		"last_updated":          "2024-01-15T10:30:00Z",
	}

	c.JSON(http.StatusOK, metrics)
}
