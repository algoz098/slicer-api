package middleware

import (
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

// Logger configura logging estruturado
func Logger() gin.HandlerFunc {
	return gin.LoggerWithFormatter(func(param gin.LogFormatterParams) string {
		return fmt.Sprintf("[%s] %s %s %d %s\n",
			param.TimeStamp.Format("2006/01/02 - 15:04:05"),
			param.Method,
			param.Path,
			param.StatusCode,
			param.Latency,
		)
	})
}

// Recovery captura e formata erros de forma padronizada
func Recovery() gin.HandlerFunc {
	return gin.CustomRecovery(func(c *gin.Context, recovered interface{}) {
		if err, ok := recovered.(string); ok {
			log.Printf("Erro capturado: %s", err)
			c.JSON(http.StatusInternalServerError, gin.H{
				"error":   "internal_server_error",
				"message": "Erro interno do servidor",
				"code":    http.StatusInternalServerError,
			})
		}
		c.AbortWithStatus(http.StatusInternalServerError)
	})
}

// CORS configura CORS para a API
func CORS() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Header("Access-Control-Allow-Origin", "*")
		c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, PATCH")
		c.Header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With")
		c.Header("Access-Control-Expose-Headers", "Content-Length, Content-Range, Content-Disposition")
		c.Header("Access-Control-Allow-Credentials", "true")
		c.Header("Access-Control-Max-Age", "3600")

		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(http.StatusNoContent)
			return
		}

		c.Next()
	}
}

// RequestID adiciona ID único para cada request
func RequestID() gin.HandlerFunc {
	return func(c *gin.Context) {
		requestID := c.GetHeader("X-Request-ID")
		if requestID == "" {
			requestID = generateRequestID()
		}
		
		c.Header("X-Request-ID", requestID)
		c.Set("request_id", requestID)
		c.Next()
	}
}

// Metrics coleta métricas de requests
func Metrics() gin.HandlerFunc {
	return func(c *gin.Context) {
		start := time.Now()
		
		c.Next()
		
		duration := time.Since(start)
		
		// TODO: Enviar métricas para sistema de observabilidade
		log.Printf("Request: %s %s - Status: %d - Duration: %v", 
			c.Request.Method, 
			c.Request.URL.Path, 
			c.Writer.Status(), 
			duration)
	}
}

// RateLimit implementa rate limiting básico (placeholder)
func RateLimit() gin.HandlerFunc {
	return func(c *gin.Context) {
		// TODO: Implementar rate limiting real (Redis-based)
		c.Next()
	}
}

// Auth middleware de autenticação (placeholder)
func Auth() gin.HandlerFunc {
	return func(c *gin.Context) {
		// TODO: Implementar autenticação real
		c.Next()
	}
}

// generateRequestID gera um ID único para o request
func generateRequestID() string {
	return time.Now().Format("20060102150405") + "-" + randomString(6)
}

// randomString gera string aleatória para IDs
func randomString(length int) string {
	const charset = "abcdefghijklmnopqrstuvwxyz0123456789"
	b := make([]byte, length)
	for i := range b {
		b[i] = charset[time.Now().UnixNano()%int64(len(charset))]
	}
	return string(b)
}
