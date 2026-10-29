#!/usr/bin/env bash
# Azeroth Warfare: start or stop the server in WSL's Docker (the batch files at the repo root
# call this). `start`: the database, login and world servers, waiting for the world to load;
# `stop`: the login and world servers (the database keeps running, its data untouched).
set -u
cd ~/azeroth-warfare-server/azeroth-warfare || { echo "No server checkout at ~/azeroth-warfare-server"; exit 1; }

case "${1:-start}" in
    start)
        docker compose up -d db realmd mangosd || exit 1
        echo
        echo "Waiting for the world to load..."
        for _ in $(seq 1 60); do
            if docker compose logs --no-color mangosd 2>&1 | grep -q 'World initialized'; then
                break
            fi
            sleep 3
        done
        docker compose ps --format '{{.Service}}: {{.Status}}'
        ;;
    stop)
        docker compose stop mangosd realmd || exit 1
        docker compose ps --format '{{.Service}}: {{.Status}}'
        ;;
    *)
        echo "usage: server.sh start|stop"
        exit 2
        ;;
esac
