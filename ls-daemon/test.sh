#!/bin/sh
SOCK=/tmp/ls-daemon-test.sock
FILE=/tmp/ls-daemon-test.txt
CONF=/tmp/ls-daemon-test.conf

trap 'kill $PID 2>/dev/null; rm -f $SOCK $FILE $CONF' EXIT

printf 'file=%s\nsocket=%s\n' "$FILE" "$SOCK" > "$CONF"

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
