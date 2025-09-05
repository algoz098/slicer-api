# Plano de Implementação do Sistema de Perfis - Slicer API

## Objetivo
Replicar o sistema de perfis do OrcaSlicer na API, permitindo que arquivos de entrada contenham configurações embutidas e sejam mescladas com perfis locais, exatamente como o OrcaSlicer faz.

## Problema Atual
- A API atual aceita apenas parâmetros básicos (bed_width, bed_height, spacing)
- Não há suporte a perfis de impressora, filamento ou processo
- Arquivos com configurações embutidas (ex: 3MF) não são processados
- Não há mesclagem de configurações como no OrcaSlicer

## Solução Proposta

### 1. Estrutura de Perfis
Usar o mesmo sistema de arquivos do OrcaSlicer:
- **Printer Profiles**: `data/printer/` - JSON com configurações da impressora
- **Filament Profiles**: `data/filament/` - JSON com configurações do filamento  
- **Process Profiles**: `data/process/` - JSON com configurações de processo
- **User Presets**: Combinações salvas de printer + filament + process

### 2. Parsing de Arquivos com Configurações Embutidas
- **3MF Files**: Configurações em `_rels/.rels` ou `3D/3dmodel.model`
- **Prusa Project Files**: Configurações embutidas
- **Outros formatos**: Metadata ou arquivos companion

### 3. Lógica de Mesclagem (Merge Logic)
Seguir a mesma prioridade do OrcaSlicer:
1. **Configurações do arquivo** (se existirem)
2. **Perfil selecionado** (printer/filament/process)
3. **Defaults do programa**

### 4. Integração com API

#### Novos Endpoints
- `GET /profiles/printers` - Listar perfis de impressora disponíveis
- `GET /profiles/filaments` - Listar perfis de filamento
- `GET /profiles/processes` - Listar perfis de processo
- `POST /profiles/load` - Carregar perfis do diretório Orca
- `GET /profiles/defaults` - Obter configurações padrão

#### Extensão dos Endpoints Existentes
- `/slicer/analyze` e `/slicer/analyze/batch` aceitarão:
  - `profile_printer`: Nome do perfil de impressora
  - `profile_filament`: Nome do perfil de filamento
  - `profile_process`: Nome do perfil de processo
  - `use_embedded_config`: Boolean para usar config do arquivo

### 5. Implementação Técnica

#### Estruturas de Dados
```go
type PrinterProfile struct {
    Name        string                 `json:"name"`
    BedSize     BedSize               `json:"bed_size"`
    NozzleSize  float64               `json:"nozzle_size"`
    // ... outros campos
}

type FilamentProfile struct {
    Name        string                 `json:"name"`
    Type        string                 `json:"type"`
    Temperature TemperatureSettings   `json:"temperature"`
    // ... outros campos
}

type ProcessProfile struct {
    Name            string              `json:"name"`
    LayerHeight     float64            `json:"layer_height"`
    Infill          InfillSettings     `json:"infill"`
    // ... outros campos
}

type EmbeddedConfig struct {
    Printer *PrinterProfile `json:"printer,omitempty"`
    Filament *FilamentProfile `json:"filament,omitempty"`
    Process *ProcessProfile `json:"process,omitempty"`
}
```

#### Serviço de Perfis
```go
type ProfileService interface {
    LoadProfilesFromDirectory(dir string) error
    GetPrinterProfile(name string) (*PrinterProfile, error)
    GetFilamentProfile(name string) (*FilamentProfile, error)
    GetProcessProfile(name string) (*ProcessProfile, error)
    ParseEmbeddedConfig(file *multipart.FileHeader) (*EmbeddedConfig, error)
    MergeConfigs(embedded *EmbeddedConfig, printer *PrinterProfile, filament *FilamentProfile, process *ProcessProfile) (*MergedConfig, error)
}
```

### 6. Fluxo de Processamento
1. **Receber arquivo** via endpoint
2. **Extrair config embutida** se `use_embedded_config=true`
3. **Carregar perfis locais** baseados nos parâmetros `profile_*`
4. **Mesclar configurações** seguindo a lógica de prioridade
5. **Aplicar configurações** no processamento do arquivo
6. **Retornar resultado** com informações dos perfis usados

### 7. Compatibilidade com OrcaSlicer
- Usar os mesmos arquivos JSON de configuração
- Manter compatibilidade com exports do OrcaSlicer
- Suporte a todas as versões de perfil do Orca 2.3.0+

### 8. Fases de Implementação
1. **Fase 1**: Estruturas básicas e parsing de JSON
2. **Fase 2**: Integração com endpoints existentes
3. **Fase 3**: Parsing de configs embutidas em arquivos
4. **Fase 4**: Lógica de mesclagem completa
5. **Fase 5**: Testes de compatibilidade com OrcaSlicer

### 9. Benefícios
- **Compatibilidade total** com OrcaSlicer
- **Flexibilidade** para usuários existentes
- **Mesclagem inteligente** de configurações
- **Reutilização** de perfis salvos
- **API consistente** com o comportamento esperado

### 10. Considerações Técnicas
- **Performance**: Cache de perfis carregados
- **Validação**: Verificar integridade dos arquivos JSON
- **Segurança**: Validar caminhos e conteúdo dos arquivos
- **Logging**: Rastrear quais perfis foram usados
- **Fallback**: Comportamento graceful se perfil não existir</content>
<parameter name="filePath">/Users/maosone/Downloads/slicer-api/reports/plano_implementacao_arranjo_orca.md
