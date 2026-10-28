#!/usr/bin/env bash
# Runs the Go integration tests against a running stack (docker compose up -d).
set -euo pipefail
cd "$(dirname "$0")/../api"
docker run --rm --network host -v "$PWD":/w -v svg-go-cache:/go -w /w \
  -e API_URL="${API_URL:-http://localhost:8080}" golang:1.24-bookworm \
  go test -tags integration -count=1 -v ./test/integration
