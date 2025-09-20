.PHONY: help show-meta check-deps-exists check-core-exists check-cli-exists \
	deps-build core-build cli-build build-all \
	deps-push core-push cli-push push-all \
	login-ghcr linux-build-inside-docker

# ---- Configuracao ----
# Pode sobrescrever via ambiente: make OWNER=me ORCASLICER_SUFFIX=b
ORCASLICER_SUFFIX ?= a
REGISTRY ?= ghcr.io
# Tenta deduzir o owner a partir do git remote
OWNER ?= $(shell git config --get remote.origin.url | sed -E 's#.*[:/]([^/]+)/[^/]+(.git)?#\1#')
# Deriva versao/arch usando os mesmos scripts do CI
VERSION ?= $(shell ORCASLICER_SUFFIX=$(ORCASLICER_SUFFIX) bash scripts/ci/derive_meta.sh | awk -F= '/^version=/{print $$2}')
ARCH ?= $(shell bash scripts/ci/derive_meta.sh | awk -F= '/^arch=/{print $$2}')

DEPS_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-build-deps:$(VERSION)-$(ARCH)
CORE_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-core:$(VERSION)-$(ARCH)
CLI_IMAGE  := $(REGISTRY)/$(OWNER)/orcaslicer-cli:$(VERSION)-$(ARCH)

help:
	@echo "Targets principais:"
	@echo "  show-meta                 - Mostra OWNER/VERSION/ARCH e nomes das imagens"
	@echo "  deps-build|core-build|cli-build - Build local (sem push)"
	@echo "  build-all                 - Build de todas as imagens (sem push)"
	@echo "  deps-push|core-push|cli-push   - Build+push (usa scripts do CI)"
	@echo "  push-all                  - Build+push de todas"
	@echo "  check-*-exists            - Verifica se a imagem existe no registry"
	@echo "  login-ghcr                - docker login ghcr.io (usa GHCR_USER/GHCR_TOKEN)"
	@echo "  linux-build-inside-docker - Reproduz o build do OrcaSlicer+addon dentro do Docker"

show-meta:
	@echo "OWNER=$(OWNER)"
	@echo "ORCASLICER_SUFFIX=$(ORCASLICER_SUFFIX)"
	@echo "VERSION=$(VERSION)"
	@echo "ARCH=$(ARCH)"
	@echo "DEPS_IMAGE=$(DEPS_IMAGE)"
	@echo "CORE_IMAGE=$(CORE_IMAGE)"
	@echo "CLI_IMAGE=$(CLI_IMAGE)"

# ---- Checks ----
check-deps-exists:
	bash scripts/ci/check_image_exists.sh "$(DEPS_IMAGE)"

check-core-exists:
	bash scripts/ci/check_image_exists.sh "$(CORE_IMAGE)"

check-cli-exists:
	bash scripts/ci/check_image_exists.sh "$(CLI_IMAGE)"

# ---- Build local (sem push) ----
# Usa --load para carregar a imagem no Docker local
# Necessita buildx; plataforma fixada em linux/amd64 para bater com o CI

deps-build:
	docker buildx build \
		--platform linux/amd64 \
		--target deps \
		-t "$(DEPS_IMAGE)" \
		--load \
		.

core-build:
	docker buildx build \
		--platform linux/amd64 \
		--target core \
		--build-arg BASE_DEPS_IMAGE="$(DEPS_IMAGE)" \
		--build-arg USE_PREBUILT_DEPS=true \
		-t "$(CORE_IMAGE)" \
		--load \
		.

cli-build:
	docker buildx build \
		--platform linux/amd64 \
		--target cli \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(CLI_IMAGE)" \
		--load \
		.

build-all: deps-build core-build cli-build

# ---- Build + Push (mesmo comportamento do CI) ----
# Reutiliza os scripts do CI, que ja incluem --push

deps-push:
	bash scripts/ci/build_deps_image.sh "$(DEPS_IMAGE)"

core-push:
	bash scripts/ci/build_core_image.sh "$(CORE_IMAGE)" "$(DEPS_IMAGE)"

cli-push:
	bash scripts/ci/build_cli_image.sh "$(CLI_IMAGE)" "$(CORE_IMAGE)"

push-all: deps-push core-push cli-push

# ---- Utilitarios ----
login-ghcr:
	@if [ -z "$$GHCR_USER" ] || [ -z "$$GHCR_TOKEN" ]; then \
		echo "Defina GHCR_USER e GHCR_TOKEN no ambiente."; \
		echo "Ex.: GHCR_USER=seu-usuario GHCR_TOKEN=ghp_xxx make login-ghcr"; \
		exit 2; \
	fi
	docker login ghcr.io -u "$$GHCR_USER" -p "$$GHCR_TOKEN"

# Reproduz o step do CI que compila OrcaSlicer + addon dentro de um container Ubuntu
linux-build-inside-docker:
	bash scripts/ci/linux_build_inside_docker.sh "$(PWD)"

