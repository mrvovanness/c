#!/bin/sh
SOCK=/tmp/ls-daemon-test.sock
FILE=/tmp/ls-daemon-test.txt
CONF=/tmp/ls-daemon-test.conf
PIDF=/tmp/ls-daemon-test.pid

trap 'kill $PID 2>/dev/null; rm -f $SOCK $FILE $CONF $PIDF' EXIT

printf 'file=%s\nsocket=%s\npidfile=%s\n' "$FILE" "$SOCK" "$PIDF" > "$CONF"

echo "hello world" > "$FILE"
./ls-daemon -f -c "$CONF" &
PID=$!
sleep 0.5

echo "Test 1: expect 12"
sh query.sh "$SOCK"

echo "more data" >> "$FILE"
echo "Test 2: expect 22"
sh query.sh "$SOCK"

rm "$FILE"
echo "Test 3: expect ERROR"
sh query.sh "$SOCK"

echo "new" > "$FILE"
echo "Test 4: expect 4"
sh query.sh "$SOCK"

echo "Test 5: expect 'another instance is already running'"
./ls-daemon -f -c "$CONF" 2>&1 >/dev/null || true
