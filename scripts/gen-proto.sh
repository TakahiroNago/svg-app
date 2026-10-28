#!/usr/bin/env bash
# Lints the proto files and regenerates the Go stubs in api/gen using a tools container.
set -euo pipefail
cd "$(dirname "$0")/.."

docker build -q -t svg-proto-tools -f docker/proto-tools.Dockerfile docker >/dev/null
docker run --rm -v "$PWD":/workspace -w /workspace svg-proto-tools \
  sh -c 'buf lint && buf generate'
