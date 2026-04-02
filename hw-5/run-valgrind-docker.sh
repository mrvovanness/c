#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building Docker image for valgrind ==="
#docker build --platform linux/amd64 -t clib-valgrind .

echo ""
echo "=== Running valgrind on tests ==="
docker run --rm --platform linux/amd64 -v "$(pwd):/app" clib-valgrind
