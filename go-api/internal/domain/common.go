package domain

// ErrorResponse representa uma resposta de erro padronizada
type ErrorResponse struct {
	Error   string      `json:"error" example:"invalid_input"`
	Message string      `json:"message" example:"Dados de entrada inválidos"`
	Code    int         `json:"code" example:"400"`
	Details interface{} `json:"details,omitempty"`
}

// PaginatedResponse representa uma resposta paginada
type PaginatedResponse struct {
	Data       interface{} `json:"data"`
	Page       int         `json:"page"`
	PageSize   int         `json:"page_size"`
	Total      int         `json:"total"`
	TotalPages int         `json:"total_pages"`
	HasNext    bool        `json:"has_next"`
	HasPrev    bool        `json:"has_prev"`
}

// SuccessResponse representa uma resposta de sucesso simples
type SuccessResponse struct {
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
}
