package domain

import "time"

// HealthStatus representa o status de saúde do sistema
type HealthStatus struct {
	Status    string                 `json:"status"`
	Timestamp time.Time              `json:"timestamp"`
	Checks    map[string]HealthCheck `json:"checks"`
	Version   string                 `json:"version"`
	Uptime    string                 `json:"uptime"`
}

// HealthCheck representa o resultado de uma verificação de saúde
type HealthCheck struct {
	Status      string    `json:"status"`
	Response    string    `json:"response"`
	LastChecked time.Time `json:"last_checked"`
}
