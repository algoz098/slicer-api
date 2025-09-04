package config

import (
	"os"
	"strconv"
	"time"
)

// Config contém todas as configurações da aplicação
type Config struct {
	Server   ServerConfig
	Storage  StorageConfig
	Queue    QueueConfig
	Cache    CacheConfig
	Workers  WorkersConfig
	Logging  LoggingConfig
	Security SecurityConfig
}

// ServerConfig configurações do servidor HTTP
type ServerConfig struct {
	Port            string
	Host            string
	ReadTimeout     time.Duration
	WriteTimeout    time.Duration
	ShutdownTimeout time.Duration
	MaxRequestSize  int64
}

// StorageConfig configurações de armazenamento
type StorageConfig struct {
	ModelsPath     string
	ResultsPath    string
	MaxFileSize    int64
	CleanupEnabled bool
	RetentionDays  int
}

// QueueConfig configurações da fila de jobs
type QueueConfig struct {
	RedisURL      string
	StreamName    string
	ConsumerGroup string
	MaxRetries    int
	RetryDelay    time.Duration
	BatchSize     int
	AckTimeout    time.Duration
}

// CacheConfig configurações do cache
type CacheConfig struct {
	Enabled       bool
	MaxSize       int64
	TTL           time.Duration
	CleanInterval time.Duration
}

// WorkersConfig configurações dos workers
type WorkersConfig struct {
	Count             int
	MaxConcurrent     int
	HeartbeatInterval time.Duration
	SlicerBinaryPath  string
	WorkerTimeout     time.Duration
}

// LoggingConfig configurações de logging
type LoggingConfig struct {
	Level      string
	Format     string // json ou text
	Output     string // stdout, file, ou both
	FilePath   string
	MaxSize    int // MB
	MaxBackups int
	MaxAge     int // days
}

// SecurityConfig configurações de segurança
type SecurityConfig struct {
	EnableAuth     bool
	JWTSecret      string
	APIKeys        []string
	RateLimit      int // requests per minute
	TrustedProxies []string
}

// Load carrega configurações de variáveis de ambiente
func Load() *Config {
	return &Config{
		Server: ServerConfig{
			Port:            getEnv("PORT", "8080"),
			Host:            getEnv("HOST", "0.0.0.0"),
			ReadTimeout:     getDurationEnv("READ_TIMEOUT", 30*time.Second),
			WriteTimeout:    getDurationEnv("WRITE_TIMEOUT", 30*time.Second),
			ShutdownTimeout: getDurationEnv("SHUTDOWN_TIMEOUT", 10*time.Second),
			MaxRequestSize:  getIntEnv("MAX_REQUEST_SIZE", 100*1024*1024), // 100MB
		},
		Storage: StorageConfig{
			ModelsPath:     getEnv("MODELS_PATH", "./uploads/models"),
			ResultsPath:    getEnv("RESULTS_PATH", "./uploads/results"),
			MaxFileSize:    getIntEnv("MAX_FILE_SIZE", 100*1024*1024), // 100MB
			CleanupEnabled: getBoolEnv("CLEANUP_ENABLED", true),
			RetentionDays:  int(getIntEnv("RETENTION_DAYS", 30)),
		},
		Queue: QueueConfig{
			RedisURL:      getEnv("REDIS_URL", "redis://localhost:6379"),
			StreamName:    getEnv("QUEUE_STREAM", "slicer:jobs"),
			ConsumerGroup: getEnv("CONSUMER_GROUP", "slicer-workers"),
			MaxRetries:    int(getIntEnv("MAX_RETRIES", 3)),
			RetryDelay:    getDurationEnv("RETRY_DELAY", 5*time.Second),
			BatchSize:     int(getIntEnv("BATCH_SIZE", 10)),
			AckTimeout:    getDurationEnv("ACK_TIMEOUT", 60*time.Second),
		},
		Cache: CacheConfig{
			Enabled:       getBoolEnv("CACHE_ENABLED", true),
			MaxSize:       getIntEnv("CACHE_MAX_SIZE", 10*1024*1024*1024), // 10GB
			TTL:           getDurationEnv("CACHE_TTL", 7*24*time.Hour),     // 7 days
			CleanInterval: getDurationEnv("CACHE_CLEAN_INTERVAL", 1*time.Hour),
		},
		Workers: WorkersConfig{
			Count:             int(getIntEnv("WORKER_COUNT", 3)),
			MaxConcurrent:     int(getIntEnv("MAX_CONCURRENT", 5)),
			HeartbeatInterval: getDurationEnv("HEARTBEAT_INTERVAL", 30*time.Second),
			SlicerBinaryPath:  getEnv("SLICER_BINARY", "./bin/slicer-headless"),
			WorkerTimeout:     getDurationEnv("WORKER_TIMEOUT", 30*time.Minute),
		},
		Logging: LoggingConfig{
			Level:      getEnv("LOG_LEVEL", "info"),
			Format:     getEnv("LOG_FORMAT", "json"),
			Output:     getEnv("LOG_OUTPUT", "stdout"),
			FilePath:   getEnv("LOG_FILE", "./logs/slicer-api.log"),
			MaxSize:    int(getIntEnv("LOG_MAX_SIZE", 100)),
			MaxBackups: int(getIntEnv("LOG_MAX_BACKUPS", 5)),
			MaxAge:     int(getIntEnv("LOG_MAX_AGE", 30)),
		},
		Security: SecurityConfig{
			EnableAuth:     getBoolEnv("ENABLE_AUTH", false),
			JWTSecret:      getEnv("JWT_SECRET", ""),
			APIKeys:        getStringSliceEnv("API_KEYS", []string{}),
			RateLimit:      int(getIntEnv("RATE_LIMIT", 100)), // 100 req/min
			TrustedProxies: getStringSliceEnv("TRUSTED_PROXIES", []string{}),
		},
	}
}

// Validate valida se as configurações são válidas
func (c *Config) Validate() []string {
	var errors []string

	// Validar configurações obrigatórias
	if c.Server.Port == "" {
		errors = append(errors, "PORT é obrigatório")
	}

	if c.Storage.ModelsPath == "" {
		errors = append(errors, "MODELS_PATH é obrigatório")
	}

	if c.Storage.ResultsPath == "" {
		errors = append(errors, "RESULTS_PATH é obrigatório")
	}

	if c.Queue.RedisURL == "" {
		errors = append(errors, "REDIS_URL é obrigatório")
	}

	if c.Workers.SlicerBinaryPath == "" {
		errors = append(errors, "SLICER_BINARY é obrigatório")
	}

	// Validar ranges
	if c.Workers.Count <= 0 {
		errors = append(errors, "WORKER_COUNT deve ser maior que 0")
	}

	if c.Storage.MaxFileSize <= 0 {
		errors = append(errors, "MAX_FILE_SIZE deve ser maior que 0")
	}

	if c.Security.EnableAuth && c.Security.JWTSecret == "" {
		errors = append(errors, "JWT_SECRET é obrigatório quando autenticação está habilitada")
	}

	return errors
}

// IsDevelopment retorna true se está em ambiente de desenvolvimento
func (c *Config) IsDevelopment() bool {
	return getEnv("GIN_MODE", "debug") != "release"
}

// IsProduction retorna true se está em ambiente de produção
func (c *Config) IsProduction() bool {
	return getEnv("GIN_MODE", "debug") == "release"
}

// Funções auxiliares para parsing de variáveis de ambiente

func getEnv(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

func getIntEnv(key string, defaultValue int64) int64 {
	if value := os.Getenv(key); value != "" {
		if parsed, err := strconv.ParseInt(value, 10, 64); err == nil {
			return parsed
		}
	}
	return defaultValue
}

func getBoolEnv(key string, defaultValue bool) bool {
	if value := os.Getenv(key); value != "" {
		if parsed, err := strconv.ParseBool(value); err == nil {
			return parsed
		}
	}
	return defaultValue
}

func getDurationEnv(key string, defaultValue time.Duration) time.Duration {
	if value := os.Getenv(key); value != "" {
		if parsed, err := time.ParseDuration(value); err == nil {
			return parsed
		}
	}
	return defaultValue
}

func getStringSliceEnv(key string, defaultValue []string) []string {
	if value := os.Getenv(key); value != "" {
		// TODO: Implementar parsing de slice (ex: "key1,key2,key3")
		return []string{value}
	}
	return defaultValue
}
