package logger

import (
	"log"
	"os"

	"slicer-api/internal/config"
)

// Logger interface para logging estruturado
type Logger interface {
	Debug(msg string, fields ...interface{})
	Info(msg string, fields ...interface{})
	Warn(msg string, fields ...interface{})
	Error(msg string, fields ...interface{})
	Fatal(msg string, fields ...interface{})
}

// MockLogger implementação mock do logger para testes
type MockLogger struct{}

func (m *MockLogger) Debug(msg string, fields ...interface{}) {}
func (m *MockLogger) Info(msg string, fields ...interface{})  {}
func (m *MockLogger) Warn(msg string, fields ...interface{})  {}
func (m *MockLogger) Error(msg string, fields ...interface{}) {}
func (m *MockLogger) Fatal(msg string, fields ...interface{}) {}

// SimpleLogger implementação simples do logger
type SimpleLogger struct {
	logger *log.Logger
}

// New cria uma nova instância do logger
func New(cfg *config.LoggingConfig) Logger {
	// TODO: Implementar logger estruturado real (logrus, zap, etc.)
	// Por enquanto, usar log padrão do Go
	
	logger := log.New(os.Stdout, "", log.LstdFlags)
	
	return &SimpleLogger{
		logger: logger,
	}
}

func (l *SimpleLogger) Debug(msg string, fields ...interface{}) {
	l.logger.Printf("[DEBUG] "+msg, fields...)
}

func (l *SimpleLogger) Info(msg string, fields ...interface{}) {
	l.logger.Printf("[INFO] "+msg, fields...)
}

func (l *SimpleLogger) Warn(msg string, fields ...interface{}) {
	l.logger.Printf("[WARN] "+msg, fields...)
}

func (l *SimpleLogger) Error(msg string, fields ...interface{}) {
	l.logger.Printf("[ERROR] "+msg, fields...)
}

func (l *SimpleLogger) Fatal(msg string, fields ...interface{}) {
	l.logger.Fatalf("[FATAL] "+msg, fields...)
}
