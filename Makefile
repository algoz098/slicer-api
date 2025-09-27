.PHONY: help show-meta check-deps-exists check-core-exists check-cli-exists \
	deps-build core-build cli-build addon-base-build addon-slim-build addon-build build-all \
	deps-push core-push cli-push addon-base-push addon-slim-push addon-push push-all \
	login-ghcr linux-build-inside-docker
	deps-push-local core-push-local cli-push-local addon-base-push-local addon-slim-push-local addon-push-local push-all-local \


# ---- Configuracao ----
# Pode sobrescrever via ambiente: make OWNER=me ORCASLICER_SUFFIX=b
ORCASLICER_SUFFIX ?= b
REGISTRY ?= ghcr.io
# Tenta deduzir o owner a partir do git remote
OWNER ?= $(shell git config --get remote.origin.url | awk -F'[/:]' '{print $$(NF-1)}')
# Deriva versao/arch usando os mesmos scripts do CI
VERSION ?= $(shell ORCASLICER_SUFFIX=$(ORCASLICER_SUFFIX) bash scripts/ci/derive_meta.sh | awk -F= '/^version=/{print $$2}')
ARCH ?= $(shell bash scripts/ci/derive_meta.sh | awk -F= '/^arch=/{print $$2}')


# Build platform and extra docker build args (override via: make PLATFORM=linux/arm64 DOCKER_BUILD_ARGS="--build-arg CI_MAX_JOBS=1")
PLATFORM ?= $(shell arch=$$(uname -m); if [ "$$arch" = "arm64" ] || [ "$$arch" = "aarch64" ]; then echo linux/arm64; else echo linux/amd64; fi)
DOCKER_BUILD_ARGS ?=

# ---- Paralelismo e controle de memoria ----
# LOW_MEM=1 força CI_MAX_JOBS=1 (a não ser que CI_MAX_JOBS já tenha sido definido)
# Alternativamente, defina CI_MAX_JOBS=N para controlar o -j do Ninja/CMake dentro do Docker
ifndef CI_MAX_JOBS
  ifeq ($(strip $(LOW_MEM)),1)
    CI_MAX_JOBS := 1
  endif
endif
ifneq ($(strip $(CI_MAX_JOBS)),)
  DOCKER_BUILD_ARGS += --build-arg CI_MAX_JOBS=$(CI_MAX_JOBS)
endif

DEPS_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-build-deps:$(VERSION)-$(ARCH)
CORE_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-core:$(VERSION)-$(ARCH)
CLI_IMAGE  := $(REGISTRY)/$(OWNER)/orcaslicer-cli:$(VERSION)-$(ARCH)
ADDON_BASE_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-addon-base:$(VERSION)-$(ARCH)
ADDON_SLIM_IMAGE := $(REGISTRY)/$(OWNER)/orcaslicer-addon-slim:$(VERSION)-$(ARCH)


help:
	@echo "Targets principais:"
	@echo "  show-meta                 - Mostra OWNER/VERSION/ARCH e nomes das imagens"
	@echo "  deps-build|core-build|cli-build - Build local (sem push)"
	@echo "  build-all                 - Build de todas as imagens (sem push)"
	@echo "  deps-push-local|core-push-local|cli-push-local - Push direto da imagem local (sem rebuild)"
	@echo "  addon-base-push-local|addon-slim-push-local|addon-push-local - Push local do addon"
	@echo "  push-all-local            - Push local de todas"

	@echo "  deps-push|core-push|cli-push   - Build+push (usa scripts do CI)"
	@echo "  push-all                  - Build+push de todas"
	@echo "  addon-base-build|addon-slim-build|addon-build - Build do addon (base e/ou slim)"
	@echo "  addon-base-push|addon-slim-push|addon-push - Build+push do addon"

	@echo "  check-*-exists            - Verifica se a imagem existe no registry"
	@echo "  login-ghcr                - docker login ghcr.io (usa GHCR_USER/GHCR_TOKEN)"
	@echo "  linux-build-inside-docker - Reproduz o build do OrcaSlicer+addon dentro do Docker"

show-meta:
	@echo "OWNER=$(OWNER)"
	@echo "ORCASLICER_SUFFIX=$(ORCASLICER_SUFFIX)"
	@echo "VERSION=$(VERSION)"
	@echo "ARCH=$(ARCH)"
	@echo "DEPS_IMAGE=$(DEPS_IMAGE)"
	@echo "ADDON_BASE_IMAGE=$(ADDON_BASE_IMAGE)"
	@echo "ADDON_SLIM_IMAGE=$(ADDON_SLIM_IMAGE)"

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
# Necessita buildx; plataforma autodetectada (linux/arm64 em Apple Silicon; linux/amd64 em x86_64)

deps-build:
	docker buildx build \
		--platform $(PLATFORM) \
		--target deps \
		-t "$(DEPS_IMAGE)" \
		--load $(DOCKER_BUILD_ARGS) \
		.

core-build:
	docker buildx build \
		--platform $(PLATFORM) \
		--target core \
		--build-arg BASE_DEPS_IMAGE="$(DEPS_IMAGE)" \
		--build-arg USE_PREBUILT_DEPS=true \
		-t "$(CORE_IMAGE)" \
		--load $(DOCKER_BUILD_ARGS) \
		.
addon-base-build:
	docker buildx build \
		--platform $(PLATFORM) \
		--target base \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(ADDON_BASE_IMAGE)" \
		--load $(DOCKER_BUILD_ARGS) \
		.

addon-slim-build:
	docker buildx build \
		--platform $(PLATFORM) \
		--target addon-slim \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(ADDON_SLIM_IMAGE)" \
		--load $(DOCKER_BUILD_ARGS) \
		.

addon-build: addon-base-build addon-slim-build


cli-build:
	docker buildx build \
		--platform $(PLATFORM) \
		--target cli \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(CLI_IMAGE)" \
		--load $(DOCKER_BUILD_ARGS) \
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


addon-base-push:
	docker buildx build \
		--platform $(PLATFORM) \
		--target base \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(ADDON_BASE_IMAGE)" \
		--push $(DOCKER_BUILD_ARGS) \
		.

addon-slim-push:
	docker buildx build \
		--platform $(PLATFORM) \
		--target addon-slim \
		--build-arg BASE_CORE_IMAGE="$(CORE_IMAGE)" \
		-t "$(ADDON_SLIM_IMAGE)" \
		--push $(DOCKER_BUILD_ARGS) \
		.

# ---- Push local (sem rebuild) ----

deps-push-local:
	docker push "$(DEPS_IMAGE)"

core-push-local:
	docker push "$(CORE_IMAGE)"

cli-push-local:
	docker push "$(CLI_IMAGE)"

addon-base-push-local:
	docker push "$(ADDON_BASE_IMAGE)"

addon-slim-push-local:
	docker push "$(ADDON_SLIM_IMAGE)"

addon-push-local: addon-base-push-local addon-slim-push-local

push-all-local: deps-push-local core-push-local cli-push-local addon-push-local


addon-push: addon-base-push addon-slim-push

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

