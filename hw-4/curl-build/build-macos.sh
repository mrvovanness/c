#!/usr/bin/env bash
set -euo pipefail

CURL_VERSION="8.19.0"
CURL_URL="https://curl.se/download/curl-${CURL_VERSION}.tar.gz"
CURL_ARCHIVE="curl-${CURL_VERSION}.tar.gz"
CURL_DIR="curl-${CURL_VERSION}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

# --- Download ---
if [ ! -f "$CURL_ARCHIVE" ]; then
    echo "==> Downloading curl ${CURL_VERSION}..."
    curl -LO "$CURL_URL"
else
    echo "==> Archive $CURL_ARCHIVE already exists, skipping download."
fi

# --- Extract ---
if [ ! -d "$CURL_DIR" ]; then
    echo "==> Extracting $CURL_ARCHIVE..."
    tar xzf "$CURL_ARCHIVE"
else
    echo "==> Directory $CURL_DIR already exists, skipping extraction."
fi

cd "$CURL_DIR"

# --- Detect OpenSSL ---
OPENSSL_PREFIX=""
if command -v brew &>/dev/null; then
    OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null || true)"
fi
if [ -z "$OPENSSL_PREFIX" ] || [ ! -d "$OPENSSL_PREFIX" ]; then
    # Fallback: try common paths
    for p in /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3 /usr; do
        if [ -f "$p/lib/libssl.a" ] || [ -f "$p/lib/libssl.dylib" ] || [ -f "$p/lib/libssl.so" ]; then
            OPENSSL_PREFIX="$p"
            break
        fi
    done
fi

if [ -z "$OPENSSL_PREFIX" ]; then
    echo "ERROR: OpenSSL not found. Install it (e.g. 'brew install openssl@3') and re-run."
    exit 1
fi
echo "==> Using OpenSSL at: $OPENSSL_PREFIX"

# --- Configure (HTTP, HTTPS, TELNET only) ---
echo "==> Configuring curl with protocols: HTTP, HTTPS, TELNET..."
./configure \
    --disable-ftp \
    --disable-file \
    --disable-ldap \
    --disable-ldaps \
    --disable-rtsp \
    --disable-dict \
    --disable-tftp \
    --disable-pop3 \
    --disable-imap \
    --disable-smb \
    --disable-smtp \
    --disable-gopher \
    --disable-mqtt \
    --disable-ipfs \
    --disable-websockets \
    --enable-http \
    --enable-telnet \
    --with-openssl="$OPENSSL_PREFIX" \
    --without-libpsl \
    --without-nghttp2

# --- Build ---
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)"
echo "==> Building with ${JOBS} parallel jobs..."
make -j"$JOBS"

# --- Verify ---
echo ""
echo "========================================="
echo "  Build complete. Verifying:"
echo "========================================="
./src/curl --version
