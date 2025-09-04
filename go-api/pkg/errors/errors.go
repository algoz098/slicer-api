package errors

import (
	"net/http"

	"slicer-api/internal/domain"
)

// Common error types
var (
	ErrNotFound         = &APIError{Code: "not_found", Message: "Recurso não encontrado", Status: http.StatusNotFound}
	ErrInvalidInput     = &APIError{Code: "invalid_input", Message: "Dados de entrada inválidos", Status: http.StatusBadRequest}
	ErrInternalError    = &APIError{Code: "internal_error", Message: "Erro interno do servidor", Status: http.StatusInternalServerError}
	ErrUnauthorized     = &APIError{Code: "unauthorized", Message: "Não autorizado", Status: http.StatusUnauthorized}
	ErrForbidden        = &APIError{Code: "forbidden", Message: "Acesso negado", Status: http.StatusForbidden}
	ErrValidationFailed = &APIError{Code: "validation_failed", Message: "Validação falhou", Status: http.StatusUnprocessableEntity}
)

// APIError representa um erro da API
type APIError struct {
	Code       string      `json:"code"`
	Message    string      `json:"message"`
	Status     int         `json:"status"`
	StatusCode int         `json:"-"`  // Alias para Status
	Details    interface{} `json:"details,omitempty"`
}

func (e *APIError) Error() string {
	return e.Message
}

// WithDetails adiciona detalhes ao erro
func (e *APIError) WithDetails(details interface{}) *APIError {
	return &APIError{
		Code:    e.Code,
		Message: e.Message,
		Status:  e.Status,
		Details: details,
	}
}

// ToResponse converte APIError para ErrorResponse
func (e *APIError) ToResponse() domain.ErrorResponse {
	return domain.ErrorResponse{
		Error:   e.Code,
		Message: e.Message,
		Code:    e.Status,
		Details: e.Details,
	}
}

// New cria um novo APIError
func New(code, message string, status int) *APIError {
	return &APIError{
		Code:    code,
		Message: message,
		Status:  status,
	}
}

// BadRequest cria um erro de bad request
func BadRequest(message string) *APIError {
	return &APIError{
		Code:       "bad_request",
		Message:    message,
		Status:     http.StatusBadRequest,
		StatusCode: http.StatusBadRequest,
	}
}

// NotFound cria um erro de not found
func NotFound(resource string) *APIError {
	return &APIError{
		Code:       "not_found",
		Message:    resource + " não encontrado",
		Status:     http.StatusNotFound,
		StatusCode: http.StatusNotFound,
	}
}

// InternalServerError cria um erro interno do servidor
func InternalServerError(message string) *APIError {
	return &APIError{
		Code:       "internal_server_error",
		Message:    message,
		Status:     http.StatusInternalServerError,
		StatusCode: http.StatusInternalServerError,
	}
}

// ValidationError cria um erro de validação
func ValidationError(details interface{}) *APIError {
	return &APIError{
		Code:       "validation_error",
		Message:    "Erro de validação",
		Status:     http.StatusBadRequest,
		StatusCode: http.StatusBadRequest,
		Details:    details,
	}
}

// PayloadTooLarge cria um erro de payload muito grande
func PayloadTooLarge(message string) *APIError {
	return &APIError{
		Code:       "payload_too_large",
		Message:    message,
		Status:     http.StatusRequestEntityTooLarge,
		StatusCode: http.StatusRequestEntityTooLarge,
	}
}

// UnsupportedMediaType cria um erro de tipo de mídia não suportado
func UnsupportedMediaType(message string) *APIError {
	return &APIError{
		Code:       "unsupported_media_type",
		Message:    message,
		Status:     http.StatusUnsupportedMediaType,
		StatusCode: http.StatusUnsupportedMediaType,
	}
}
