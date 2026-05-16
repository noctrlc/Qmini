import paramiko
import time
import os
import sys

host = "192.144.133.168"
user = "ubuntu"
password = "l02.07.12"

# Resolve paths relative to this script
script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
server_src = os.path.join(project_dir, "signaling_server", "signaling_server_linux.c")

if not os.path.exists(server_src):
    print(f"ERROR: Server source not found at {server_src}")
    sys.exit(1)

print(f"=== Deploying {server_src} -> {host} ===")

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(host, username=user, password=password, timeout=15)

# Upload source
print("=== Uploading ===")
sftp = client.open_sftp()
sftp.put(server_src, "/home/ubuntu/signaling_server_linux.c")
sftp.close()

# Compile and restart
cmds = """
echo "=== Compiling ==="
gcc -O2 ~/signaling_server_linux.c -o ~/signaling_server_new -lpthread && echo "OK" || echo "FAIL"

echo "=== Stopping old server ==="
pkill signaling_server 2>/dev/null || true
sleep 1
PID=$(netstat -tlnp 2>/dev/null | grep 9088 | awk '{print $7}' | cut -d/ -f1)
[ -n "$PID" ] && kill -9 $PID 2>/dev/null
sleep 1

echo "=== Starting new server ==="
mv ~/signaling_server_new ~/signaling_server
rm -f ~/server.log
nohup ./signaling_server 9088 > server.log 2>&1 &
sleep 2

echo "=== Verification ==="
netstat -tlnp 2>/dev/null | grep 9088
echo "---"
cat ~/server.log
"""

print("=== Deploying ===")
stdin, stdout, stderr = client.exec_command(cmds)
time.sleep(8)
out = stdout.read().decode('utf-8', errors='replace')
err = stderr.read().decode('utf-8', errors='replace')
print(out)
if err:
    print("STDERR:", err)

client.close()
print("\n=== DONE ===")
