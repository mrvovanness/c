#!/bin/bash
set -e

docker run --rm --platform linux/amd64 \
  -v "$(pwd)":/work -w /work \
  ubuntu bash -c "
    apt-get update -qq &&
    apt-get install -y -qq nasm gcc make > /dev/null 2>&1 &&
    make clean && make && ./asm-prog
  "
