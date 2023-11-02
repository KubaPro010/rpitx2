import socket
import os
import threading

fifo_path = "tctl"
if not os.path.exists(fifo_path):
    os.mkfifo(fifo_path)

server_ip = "0.0.0.0"  # Change this to the appropriate IP address
server_port = 9997  # Change this to the appropriate port
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((server_ip, server_port))
server.listen(5)  # Allow multiple connections

print("R27 NRDSOS 1.1 (THIS IS AN INTERNAL APP USED IN R27'S INTERNAL RDS SERVICE)")
print("Listening for incoming connections...")

def handle_client(client_socket):
    while True:
        try:
            data = client_socket.recv(1024).decode('utf-8')
            if not data:
                break
            print(f"Received from {client_socket.getpeername()}: {data}")
            with open(fifo_path, "w") as fifo:
                fifo.write(data)
        except Exception as e:
            print(f"Error: {e}")
            break
    client_socket.close()

while True:
    client, client_address = server.accept()
    print(f"Connection from {client_address}")
    client_handler = threading.Thread(target=handle_client, args=(client,))
    client_handler.start()
