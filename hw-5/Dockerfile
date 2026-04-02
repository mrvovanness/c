FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    valgrind \
    libcurl4-openssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/test/package

CMD ["bash", "-c", "make clean && make valgrind 2>&1"]
