TEST_SOCKET=/tmp/ls-daemon-test.sock

python3 -c "
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('$TEST_SOCKET')
print(s.recv(256).decode(), end='')
s.close()
"
