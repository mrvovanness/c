#!/bin/sh
set -e

DAEMON=./ls-daemon
TEST_FILE=/tmp/ls-daemon-test-file.txt
TEST_SOCKET=/tmp/ls-daemon-test.sock
TEST_CONF=/tmp/ls-daemon-test.conf
DPID=

cleanup() {
    [ -n "$DPID" ] && kill "$DPID" 2>/dev/null && wait "$DPID" 2>/dev/null
    rm -f "$TEST_FILE" "$TEST_SOCKET" "$TEST_CONF"
}
trap cleanup EXIT

query() {
    python3 -c "
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('$TEST_SOCKET')
print(s.recv(256).decode(), end='')
s.close()
"
}

assert_eq() {
    if [ "$1" != "$2" ]; then
        echo "FAIL: expected '$2', got '$1'"
        exit 1
    fi
}

assert_prefix() {
    case "$1" in
        "$2"*) ;;
        *) echo "FAIL: expected prefix '$2', got '$1'"; exit 1 ;;
    esac
}

printf 'file=%s\nsocket=%s\n' "$TEST_FILE" "$TEST_SOCKET" > "$TEST_CONF"
rm -f "$TEST_SOCKET"
echo "hello world" > "$TEST_FILE"

$DAEMON -f -c "$TEST_CONF" &
DPID=$!

# while true; do
#     sleep 30
# done
sleep 0.3

echo "=== Test 1: initial size (expect 12) ==="
RESULT=$(query)
echo "$RESULT"
assert_eq "$RESULT" "12"

echo "more data" >> "$TEST_FILE"
echo "=== Test 2: size after append (expect 22) ==="
RESULT=$(query)
echo "$RESULT"
assert_eq "$RESULT" "22"

rm -f "$TEST_FILE"
echo "=== Test 3: file removed (expect ERROR) ==="
RESULT=$(query)
echo "$RESULT"
assert_prefix "$RESULT" "ERROR:"

echo "new" > "$TEST_FILE"
echo "=== Test 4: file re-created (expect 4) ==="
RESULT=$(query)
echo "$RESULT"
assert_eq "$RESULT" "4"

echo "=== All tests passed ==="
