#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p backups
chmod 700 backups
umask 077
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
file="backups/ai-companion-postgres-${stamp}.dump"

docker compose -f docker-compose.production.yml exec -T db sh -c 'pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" -Fc' > "$file"
sha256sum "$file" > "${file}.sha256"
find backups -type f -name 'ai-companion-postgres-*.dump' -mtime +14 -delete
find backups -type f -name 'ai-companion-postgres-*.dump.sha256' -mtime +14 -delete
echo "Created ${file}"
echo "Copy this backup off-host and test restore regularly."
