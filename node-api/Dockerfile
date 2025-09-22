# syntax=docker/dockerfile:1.7

# Slicer API image that consumes the OrcaSlicer addon base image and bakes the HTTP API
# Build args:
#   ADDON_BASE  - name:tag of the prebuilt addon base image (target=base from repo root Dockerfile)
#                 Defaults to local tag 'orca-addon:base'
ARG ADDON_BASE=orca-addon:base

FROM ${ADDON_BASE} as addon

# Build the Node API (TypeScript) in a separate stage to keep the final image slim
FROM node:24-bookworm-slim AS builder
WORKDIR /opt/orca/node-api

# Install dependencies first (better cache)
COPY node-api/package*.json ./
RUN npm ci

# Copy the rest and compile TS -> lib/
COPY node-api .
RUN npm run compile && npm prune --omit=dev

# Runtime image: reuse addon base so prebuilt native addon and resources are available
FROM ${ADDON_BASE} AS runtime
ENV NODE_ENV=production \
    ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources

WORKDIR /opt/orca/node-api
COPY --from=builder /opt/orca/node-api /opt/orca/node-api

# Expose default HTTP port
EXPOSE 3030

# Start the API (package.json has "start": "node lib/")
CMD ["npm", "start"]

