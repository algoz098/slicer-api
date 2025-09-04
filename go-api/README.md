# Slicer API - Versão Simplificada

API REST minimalista para futuro processamento de modelos 3D e geração de arquivos G-code.

## Status Atual

Esta é uma versão simplificada da API, contendo apenas os endpoints essenciais para verificação de sistema. Os recursos de processamento de modelos, jobs e perfis foram removidos para manter uma estrutura limpa e focada.

## 🚀 Quick Start

### Executar API

```bash
# Instalar dependências
go mod tidy

# Gerar documentação Swagger
swag init -g ./cmd/server/main.go -o ./docs

# Executar em modo desenvolvimento
go run ./cmd/server

# Ou compilar e executar
go build -o bin/slicer-api.exe ./cmd/server
./bin/slicer-api.exe
```

A API estará disponível em: http://localhost:8080

### Testar Endpoints

```bash
# Endpoint raiz
curl http://localhost:8080/

# Health check
curl http://localhost:8080/api/v1/health

# Informações do sistema
curl http://localhost:8080/api/v1/info

# Métricas do sistema
curl http://localhost:8080/api/v1/metrics

# Documentação Swagger
# Abrir no navegador: http://localhost:8080/swagger/index.html
```

## Endpoints Disponíveis

### Sistema
- `GET /` - Informações básicas da API
- `GET /api/v1/health` - Status de saúde do sistema
- `GET /api/v1/info` - Informações detalhadas do sistema
- `GET /api/v1/metrics` - Métricas de observabilidade do sistema
- `GET /swagger/*` - Documentação Swagger

## Estrutura do Projeto

```
cmd/
  server/           # Ponto de entrada da aplicação
internal/
  config/           # Configurações
  domain/           # Modelos de domínio (apenas system e common)
  handlers/         # Handlers HTTP (apenas system)
  middleware/       # Middlewares
  services/         # Lógica de negócio (apenas system)
pkg/
  errors/           # Tratamento de erros
  logger/           # Sistema de logs
docs/               # Documentação Swagger
```

## Como testar

Execute o script de teste incluído:

```bash
# PowerShell
.\test-api.ps1
```

Ou teste manualmente:

```bash
# PowerShell
Invoke-RestMethod -Uri "http://localhost:8080/" -Method GET
Invoke-RestMethod -Uri "http://localhost:8080/api/v1/health" -Method GET  
Invoke-RestMethod -Uri "http://localhost:8080/api/v1/info" -Method GET

# cURL (se disponível)
curl http://localhost:8080/
curl http://localhost:8080/api/v1/health
curl http://localhost:8080/api/v1/info
```

## Próximos Passos

Esta estrutura simplificada serve como base para futuras expansões:
- Implementação de processamento de modelos 3D
- Sistema de jobs para slicing
- Gerenciamento de perfis de impressão
- Sistema de cache e filas
- Autenticação e autorização
- Integração com OrcaSlicer core

## Desenvolvimento

### Instalar ferramentas:
```bash
go install github.com/swaggo/swag/cmd/swag@latest
```

### Gerar documentação Swagger:
```bash
swag init -g ./cmd/server/main.go -o ./docs
```

### Executar testes:
```bash
go test ./...
```

## Tecnologias Utilizadas

- **Go 1.21** - Linguagem de programação
- **Gin** - Framework web para Go
- **Swag** - Geração automática de documentação Swagger

## Docker

### Build e execução com Docker:
```bash
docker build -t slicer-api .
docker run -p 8080:8080 slicer-api
```

### Ou com docker-compose:
```bash
docker-compose up
```

## Contribuição

1. Fork o projeto
2. Crie uma branch para sua feature
3. Implemente os testes
4. Execute os testes
5. Faça o commit das mudanças
6. Abra um Pull Request

---

**Esta versão simplificada serve como base sólida para o desenvolvimento futuro da API de slicing.**
5. Configurar Docker
6. Implementar CI/CD
