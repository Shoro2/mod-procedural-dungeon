#!/bin/sh
# Read-only helper: run SQL from a file against acore_world.
# Usage: sh q.sh <sqlfile> [database]
CONF="C:/wowstuff/dcore/configs/worldserver.conf"
LINE=$(grep -m1 '^WorldDatabaseInfo' "$CONF" | sed 's/.*"\(.*\)".*/\1/')
HOST=$(echo "$LINE" | cut -d';' -f1)
PORT=$(echo "$LINE" | cut -d';' -f2)
USER=$(echo "$LINE" | cut -d';' -f3)
PASS=$(echo "$LINE" | cut -d';' -f4)
DB=$(echo "$LINE" | cut -d';' -f5)
if [ -n "$2" ]; then DB="$2"; fi
MYSQL="C:/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe"
MYSQL_PWD="$PASS" "$MYSQL" --host="$HOST" --port="$PORT" --user="$USER" --database="$DB" --batch --raw --default-character-set=utf8mb4 < "$1"
