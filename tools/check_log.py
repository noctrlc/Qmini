import paramiko
import time

host = "192.144.133.168"
user = "ubuntu"
password = "l02.07.12"

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(host, username=user, password=password, timeout=15)

stdin, stdout, stderr = client.exec_command("cat ~/server.log")
time.sleep(2)
out = stdout.read().decode('utf-8', errors='replace')
print(out)

client.close()
