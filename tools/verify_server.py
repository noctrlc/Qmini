import paramiko
import time

host = "192.144.133.168"
user = "ubuntu"
password = "l02.07.12"

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(host, username=user, password=password, timeout=15)

# Check critical code section
commands = """
echo "=== Check client_t struct (should have local_port) ==="
grep -A 7 "^typedef struct {" ~/signaling_server_linux.c | head -12

echo ""
echo "=== Check REGISTER handler (should use local_port for existing peers) ==="
grep -A 35 "if (strncmp(line, \"REGISTER \"" ~/signaling_server_linux.c | grep -E "local_port|ntohs|addr\.sin_port"

echo ""
echo "=== Check broadcast to new peer about existing members ==="
grep -B2 -A4 "clients\[j\]\.local_port" ~/signaling_server_linux.c

echo ""
echo "=== Binary modified time ==="
ls -la ~/signaling_server

echo ""
echo "=== Current server PID ==="
ps aux | grep signaling_server | grep -v grep
"""

stdin, stdout, stderr = client.exec_command(commands)
time.sleep(2)
out = stdout.read().decode('utf-8', errors='replace')
err = stderr.read().decode('utf-8', errors='replace')
print(out)
if err:
    print("STDERR:", err)

client.close()
