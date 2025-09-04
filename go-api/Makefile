# Makefile para Slicer API

# Variáveis
BINARY_NAME=slicer-api
CMD_PATH=./cmd/server
BUILD_DIR=./bin

# Comandos principais
.PHONY: run build test clean dev docs help

# Executar em modo desenvolvimento
run:
	@echo "🚀 Iniciando servidor em modo desenvolvimento..."
	go run $(CMD_PATH)

# Gerar documentação Swagger
docs:
	@echo "📚 Gerando documentação Swagger..."
	@if command -v swag > /dev/null; then \
		swag init -g $(CMD_PATH)/main.go -o ./docs; \
	else \
		echo "❌ swag não encontrado. Instale com: go install github.com/swaggo/swag/cmd/swag@latest"; \
	fi

# Compilar aplicação
build: docs
	@echo "🔨 Compilando aplicação..."
	go build -o $(BUILD_DIR)/$(BINARY_NAME) $(CMD_PATH)

# Executar testes
test:
	@echo "🧪 Executando testes..."
	go test -v ./...

# Executar testes com coverage
test-coverage:
	@echo "📊 Executando testes com coverage..."
	go test -v -cover ./...

# Limpar arquivos compilados
clean:
	@echo "🧹 Limpando arquivos compilados..."
	rm -rf $(BUILD_DIR)/*

# Modo desenvolvimento com hot reload (requer air)
dev:
	@echo "🔥 Iniciando com hot reload..."
	@if command -v air > /dev/null; then \
		air; \
	else \
		echo "❌ Air não encontrado. Instale com: go install github.com/cosmtrek/air@latest"; \
		echo "📝 Usando go run normal..."; \
		go run $(CMD_PATH); \
	fi

# Verificar dependências
deps:
	@echo "📦 Verificando dependências..."
	go mod tidy
	go mod verify

# Verificar código
lint:
	@echo "🔍 Verificando código..."
	@if command -v golangci-lint > /dev/null; then \
		golangci-lint run; \
	else \
		echo "❌ golangci-lint não encontrado. Usando go vet..."; \
		go vet ./...; \
	fi

# Formatar código
fmt:
	@echo "✨ Formatando código..."
	go fmt ./...

# Docker commands
docker-build:
	@echo "🐳 Construindo imagem Docker..."
	docker build -t $(BINARY_NAME) .

docker-run:
	@echo "🐳 Executando container Docker..."
	docker run -p 8080:8080 $(BINARY_NAME)

# Instalar ferramentas de desenvolvimento
install-tools:
	@echo "🛠️ Instalando ferramentas de desenvolvimento..."
	go install github.com/cosmtrek/air@latest
	go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest

# Mostrar ajuda
help:
	@echo "📖 Comandos disponíveis:"
	@echo "  make run           - Executar servidor"
	@echo "  make build         - Compilar aplicação"
	@echo "  make test          - Executar testes"
	@echo "  make test-coverage - Testes com coverage"
	@echo "  make dev           - Modo desenvolvimento (hot reload)"
	@echo "  make clean         - Limpar arquivos compilados"
	@echo "  make deps          - Verificar dependências"
	@echo "  make lint          - Verificar código"
	@echo "  make fmt           - Formatar código"
	@echo "  make docker-build  - Construir imagem Docker"
	@echo "  make docker-run    - Executar container"
	@echo "  make install-tools - Instalar ferramentas dev"
	@echo "  make help          - Mostrar esta ajuda"

# Comando padrão
all: deps fmt lint test build
