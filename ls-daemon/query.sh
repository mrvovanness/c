#!/bin/sh
python3 -c "
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
print(s.recv(256).decode(), end='')
" "$1"
