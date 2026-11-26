#!/usr/bin/env bash
# Rebuilds and restarts the Go REST API and the C++ gRPC engine on the host this runs on,
# checks that conversion still answers, and puts the previous images back if it does not.
#
# The deploy workflow (.github/workflows/deploy.yml) runs it on the EC2 instance through SSM
# after it has fast-forwarded the checkout. It can also be run by hand from the repository root:
#
#   bash scripts/deploy-backend.sh <previous-commit> <new-commit>
#
# With both commits given, nothing is rebuilt when no file that goes into the two images changed
# between them. Without arguments it always rebuilds.
#
# Environment:
#   SVG_WEB_DIR         checkout of svg-web whose `web` container proxies /api/ to this API.
#                       It resolves the API's address at startup, so it is restarted after the
#                       API container is replaced. Default: ../svg-web next to this checkout.
#                       Set to an empty string to skip everything about the frontend.
#   PUBLIC_HEALTH_URL   public URL that reaches /healthz through every proxy.
#                       Default: https://aws-svg.art-full-stack.com/api/healthz. Empty to skip.
#   API_HEALTH_URL      Default: http://127.0.0.1:8080/healthz
#   WEB_API_HEALTH_URL  Default: http://127.0.0.1:8081/api/healthz
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

previous_commit="${1:-}"
new_commit="${2:-}"

svg_web_dir="${SVG_WEB_DIR-$repo_dir/../svg-web}"
public_health_url="${PUBLIC_HEALTH_URL-https://aws-svg.art-full-stack.com/api/healthz}"
api_health_url="${API_HEALTH_URL:-http://127.0.0.1:8080/healthz}"
web_api_health_url="${WEB_API_HEALTH_URL:-http://127.0.0.1:8081/api/healthz}"

# Everything that is copied into, or decides the configuration of, the two images.
image_inputs=(api server src proto docker CMakeLists.txt docker-compose.yml)

log() {
  printf '[deploy-backend] %s\n' "$*"
}

# Under SSM this runs as root in a checkout that belongs to another user, where git refuses to
# work ("dubious ownership"). Git is run as the owner of the checkout instead.
repo_git() {
  local owner
  owner="$(stat -c %U "$repo_dir" 2> /dev/null || true)"
  if [ "$(id -u)" = 0 ] && [ -n "$owner" ] && [ "$owner" != root ] && [ "$owner" != UNKNOWN ]; then
    sudo -u "$owner" -H git "$@"
  else
    git "$@"
  fi
}

wait_for() {
  local url="$1" attempts="${2:-30}"
  for _ in $(seq 1 "$attempts"); do
    if curl -fsS --max-time 5 "$url" > /dev/null 2>&1; then
      return 0
    fi
    sleep 2
  done
  log "no answer from $url"
  return 1
}

restart_frontend() {
  if [ -z "$svg_web_dir" ]; then
    return 0
  fi
  if [ ! -f "$svg_web_dir/docker-compose.yml" ]; then
    log "svg-web was not found at $svg_web_dir"
    return 1
  fi
  (cd "$svg_web_dir" && docker compose restart web)
}

start_services() {
  # The API waits for the engine, so the engine goes first and must be healthy.
  docker compose up -d --wait --no-deps --no-build cpp-engine
  docker compose up -d --no-deps --no-build api
}

verify() {
  wait_for "$api_health_url" || return 1
  restart_frontend || return 1
  if [ -n "$svg_web_dir" ]; then
    wait_for "$web_api_health_url" || return 1
  fi
  if [ -n "$public_health_url" ]; then
    wait_for "$public_health_url" 10 || return 1
  fi
}

if [ -n "$previous_commit" ] && [ -n "$new_commit" ]; then
  if [ "$previous_commit" = "$new_commit" ]; then
    log "already at $new_commit; nothing to deploy"
    exit 0
  fi
  changed=0
  repo_git diff --quiet "$previous_commit" "$new_commit" -- "${image_inputs[@]}" || changed=$?
  if [ "$changed" -eq 0 ]; then
    log "no file of the API or the engine changed between $previous_commit and $new_commit; nothing to rebuild"
    exit 0
  fi
  if [ "$changed" -ne 1 ]; then
    log "could not compare $previous_commit with $new_commit; rebuilding to be safe"
  fi
fi

mapfile -t images < <(docker compose config --images)
if [ "${#images[@]}" -eq 0 ]; then
  log "docker compose reported no images"
  exit 1
fi

# Keep what is running now, so a failed deployment can go back without building anything.
have_rollback=true
for image in "${images[@]}"; do
  if docker image inspect "$image" > /dev/null 2>&1; then
    docker image tag "$image" "$image:rollback"
  else
    have_rollback=false
  fi
done

log "building ${images[*]}"
docker compose build cpp-engine api

log "starting the new containers"
if start_services && verify; then
  log "deployed ${new_commit:-the current checkout}"
  exit 0
fi

log "the new version did not come up"
docker compose ps || true
docker compose logs --tail 40 cpp-engine api || true

if [ "$have_rollback" != true ]; then
  log "there was no previous image to go back to"
  exit 1
fi

log "putting the previous images back"
for image in "${images[@]}"; do
  docker image tag "$image:rollback" "$image"
done
if start_services && verify; then
  log "the previous version is running again; the deployment failed"
else
  log "the previous version did not come up either; the service needs attention"
fi
exit 1
