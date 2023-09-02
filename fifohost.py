import socket
import os
fifo_path = "rds_ctl"
if not os.path.exists(fifo_path): os.mkfifo(fifo_path)
server_ip = "0.0.0.0"  # Change this to the appropriate IP address
server_port = 9997     # Change this to the appropriate port
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((server_ip, server_port))
server.listen(1)
print("R27 NRDSOS 1.1 (THIS IS A INTERNAL APP USED IN R27'S INTERNAL RDS SERVICE)")
print("Listening for incoming connections...")
while True:
    client, client_address = server.accept()
    print(f"Connection from {client_address}")
    while True:
        try:
            # Read data from the client
            data = client.recv(1024).decode('utf-8')
            if data:
                print(f"Received: {data}")
                with open(fifo_path, "w") as fifo: fifo.write(data)
        except Exception as e:
            print(f"Error: {e}")
            break
server.close()
