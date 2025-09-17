# OrcaSlicerCli Development Guide

Este documento fornece informações técnicas sobre como compilar, executar e estender o OrcaSlicerCli.

## Visão Geral da Arquitetura

O OrcaSlicerCli é estruturado em camadas modulares:

```
OrcaSlicerCli/
├── src/
│   ├── core/           # Biblioteca principal que encapsula libslic3r
│   ├── commands/       # Implementação de comandos CLI
│   ├── utils/          # Utilitários (logging, parsing, etc.)
│   ├── Application.cpp # Coordenador principal da aplicação
│   └── main.cpp        # Ponto de entrada
├── scripts/            # Scripts de build e desenvolvimento
├── docs/               # Documentação
└── examples/           # Exemplos de uso
```

### Componentes Principais

#### 1. CliCore (`src/core/`)
- **CliCore**: Interface principal para funcionalidades do OrcaSlicer
- **SlicingEngine**: Engine de slicing (planejado)
- **ConfigManager**: Gerenciamento de configurações (planejado)
- **FileUtils**: Utilitários para manipulação de arquivos (planejado)

#### 2. Sistema de Comandos (`src/commands/`)
- **BaseCommand**: Classe base para todos os comandos
- **SliceCommand**: Comando de slicing
- **InfoCommand**: Comando de informações do modelo
- **VersionCommand**: Comando de versão
- **HelpCommand**: Comando de ajuda

#### 3. Utilitários (`src/utils/`)
- **ArgumentParser**: Parser de argumentos de linha de comando
- **Logger**: Sistema de logging estruturado
- **ErrorHandler**: Tratamento de erros e exceções

## Pré-requisitos

### Dependências do Sistema
- **CMake 3.13+**
- **Compilador C++17** (GCC 8+, Clang 8+, MSVC 2019+)
- **OrcaSlicer** compilado e funcional

### Dependências do OrcaSlicer
O projeto reutiliza as dependências do OrcaSlicer:
- Boost (system, filesystem, thread, log, locale, regex)
- TBB (Threading Building Blocks)
- OpenVDB
- NLopt
- CGAL
- OpenCV
- E outras conforme definidas no OrcaSlicer

## Compilação

### Método Rápido (Recomendado)

```bash
# Clone e navegue para o diretório
cd orcaslicer-addon/OrcaSlicerCli

# Build completo (Release)
./scripts/build.sh

# Build de desenvolvimento (Debug)
./scripts/dev.sh build

# Build apenas CLI (se dependências já estão prontas)
./scripts/build.sh --cli-only
```

### Método Manual

```bash
# 1. Certifique-se que o OrcaSlicer está compilado
cd ../OrcaSlicer
./build_release_macos.sh  # ou build_linux.sh

# 2. Configure o OrcaSlicerCli
cd ../OrcaSlicerCli
mkdir build && cd build

# 3. Configure com CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DORCASLICER_ROOT_DIR=../../OrcaSlicer \
      ..

# 4. Compile
make -j$(nproc)

# 5. Instale (opcional)
make install
```

### Opções de Build

#### Scripts de Build
- `./scripts/build.sh` - Script principal de build (Unix/Linux/macOS)
- `./scripts/build.bat` - Script de build para Windows
- `./scripts/dev.sh` - Helper para desenvolvimento

#### Opções do CMake
- `CMAKE_BUILD_TYPE`: Debug, Release, RelWithDebInfo
- `ORCASLICER_ROOT_DIR`: Caminho para o diretório do OrcaSlicer
- `ORCACLI_STATIC_LINKING`: Usar linking estático (padrão: ON)
- `ORCACLI_BUILD_TESTS`: Compilar testes (padrão: OFF)

## Execução

### Uso Básico

```bash
# Ajuda geral
./orcaslicer-cli --help

# Slice um modelo
./orcaslicer-cli slice --input model.stl --output model.gcode

# Informações sobre um modelo
./orcaslicer-cli info --input model.stl

# Versão
./orcaslicer-cli version
```

### Desenvolvimento

```bash
# Executar durante desenvolvimento
./scripts/dev.sh run -- --help
./scripts/dev.sh run -- slice --input test.stl --output test.gcode
```

## Estrutura do Código

### Convenções de Código
- **Estilo**: Seguir convenções do OrcaSlicer
- **Namespaces**: Usar `OrcaSlicerCli` como namespace principal
- **Headers**: Usar guards `#pragma once`
- **Includes**: Agrupar por categoria (std, boost, orcaslicer, local)

### Padrões de Design
- **RAII**: Gerenciamento automático de recursos
- **PIMPL**: Implementação privada para classes principais
- **Command Pattern**: Para comandos CLI
- **Factory Pattern**: Para criação de comandos (planejado)

### Tratamento de Erros
```cpp
// Usar exceções customizadas
throw CliException(ErrorCode::InvalidFile, "File not found", filename);

// Ou usar ErrorHandler para logging automático
ErrorHandler::handleError(ErrorCode::SlicingError, "Slicing failed", details);

// Execução segura
auto result = ErrorHandler::safeExecute([&]() {
    // código que pode falhar
});
```

### Logging
```cpp
#include "utils/Logger.hpp"

// Usar macros para logging
LOG_INFO("Starting operation...");
LOG_ERROR("Operation failed: " + error_message);
LOG_DEBUG("Debug info: " + debug_data);

// Configurar nível de log
Logger::getInstance().setLogLevel(LogLevel::Debug);
```

## Extensão do Projeto

### Adicionando Novos Comandos

1. **Criar classe de comando**:
```cpp
// src/commands/MyCommand.hpp
class MyCommand : public BaseCommand {
public:
    int execute(const ArgumentParser::ParseResult& args) override;
    std::string getName() const override { return "mycommand"; }
    std::string getDescription() const override { return "My custom command"; }
};
```

2. **Implementar comando**:
```cpp
// src/commands/MyCommand.cpp
int MyCommand::execute(const ArgumentParser::ParseResult& args) {
    // Implementação do comando
    return 0;
}
```

3. **Registrar no Application.cpp**:
```cpp
// Em setupArgumentParser()
ArgumentParser::CommandDef my_cmd("mycommand", "My custom command");
my_cmd.arguments = { /* definir argumentos */ };
m_parser->addCommand(my_cmd);

// Em executeCommand()
if (command == "mycommand") {
    return handleMyCommand(args);
}
```

### Adicionando Funcionalidades ao Core

1. **Estender CliCore**: Adicionar métodos públicos
2. **Implementar na Impl**: Adicionar lógica na classe privada
3. **Usar libslic3r**: Integrar com componentes do OrcaSlicer
4. **Documentar**: Atualizar documentação da API

### Testes (Planejado)

```cpp
// Estrutura planejada para testes
#include <catch2/catch.hpp>
#include "core/CliCore.hpp"

TEST_CASE("CliCore initialization") {
    CliCore core;
    auto result = core.initialize();
    REQUIRE(result.success);
}
```

## Debugging

### Debug Builds
```bash
# Build debug
./scripts/build.sh -t Debug

# Executar com debugger
gdb ./build/bin/orcaslicer-cli
```

### Logging Verboso
```bash
# Ativar logs debug
./orcaslicer-cli --log-level debug slice --input test.stl --output test.gcode

# Ou usar flag verbose
./orcaslicer-cli --verbose slice --input test.stl --output test.gcode
```

### Análise de Problemas
1. **Verificar logs**: Usar `--verbose` ou `--log-level debug`
2. **Validar entrada**: Usar comando `info` para verificar modelos
3. **Testar configuração**: Verificar se OrcaSlicer funciona standalone
4. **Verificar dependências**: Confirmar que todas as libs estão linkadas

## Contribuição

### Workflow de Desenvolvimento
1. **Fork** do repositório
2. **Branch** para feature: `git checkout -b feature/nova-funcionalidade`
3. **Implementar** mudanças seguindo convenções
4. **Testar** localmente
5. **Commit** com mensagens descritivas
6. **Pull Request** com descrição detalhada

### Checklist para PRs
- [ ] Código compila sem warnings
- [ ] Funcionalidade testada manualmente
- [ ] Documentação atualizada se necessário
- [ ] Segue convenções de código
- [ ] Não quebra funcionalidades existentes

## Troubleshooting

### Problemas Comuns

**Erro: "OrcaSlicer root directory not found"**
- Verificar se OrcaSlicer está no diretório pai
- Usar `--orcaslicer-root` para especificar caminho

**Erro: "libslic3r not found"**
- Compilar OrcaSlicer primeiro
- Verificar se build/src/libslic3r/libslic3r.a existe

**Erro: "OpenVDB not found"**
- Compilar dependências do OrcaSlicer
- Verificar CMAKE_PREFIX_PATH

### Logs de Debug
```bash
# Ativar logs máximos
export SLIC3R_LOGLEVEL=9
./orcaslicer-cli --log-level debug --verbose [comando]
```

## Roadmap

### Funcionalidades Planejadas
- [ ] Sistema de plugins
- [ ] Processamento em lote
- [ ] Configuração via YAML/JSON
- [ ] Integração com CI/CD
- [ ] Testes automatizados
- [ ] Documentação da API
- [ ] Suporte a mais formatos de arquivo

### Melhorias Técnicas
- [ ] Otimização de performance
- [ ] Redução de dependências
- [ ] Suporte a threading
- [ ] Cache de configurações
- [ ] Validação de entrada mais robusta
