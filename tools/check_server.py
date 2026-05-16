import paramiko
import time

host = "192.144.133.168"
user = "ubuntu"
password = "l02.07.12"

print("=== Connecting ===")
try:
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, username=user, password=password, timeout=15)
    print("Connected OK")
except Exception as e:
    print(f"Connection FAILED: {e}")
    exit(1)

# Simple command, no sudo needed
stdin, stdout, stderr = client.exec_command("ps aux | grep signal | grep -v grep; echo '---'; netstat -tlnp 2>/dev/null | grep 9088 || echo 'port 9088 not listening'; echo '---'; cat ~/server.log 2>/dev/null | tail -20")

# Don't write to stdin for sudo - just read output
time.sleep(3)
out = stdout.read().decode('utf-8', errors='replace')
err = stderr.read().decode('utf-8', errors='replace')
print(out)
if err:
    print("STDERR:", err)

client.close()
