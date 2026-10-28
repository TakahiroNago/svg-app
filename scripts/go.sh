#!/usr/bin/env bash
# Runs a Go command for the api module in a container, e.g.
#   scripts/go.sh go test ./...
set -euo pipefail
cd "$(dirname "$0")/../api"
docker run --rm -v "$PWD":/w -v svg-go-cache:/go -w /w golang:1.24-bookworm "$@"
