import socket

HOST = '0.0.0.0'
PORT = 9997         # Choose an available port

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
    server_socket.bind((HOST, PORT))
    server_socket.listen()

    print(f"Server listening on {HOST}:{PORT}")

    connection, address = server_socket.accept()
    print(f"Connected by {address}")

    with open('tctl', 'w') as fifo:
        while True:
            data = connection.recv(1024)
            if not data:
                break
            print(data)
            fifo.write(data.decode('utf-8'))
            fifo.flush()

    print("Connection closed")
