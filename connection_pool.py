import socket
from queue import Queue, Empty
import threading
import time
import struct
class ConnectionPool:
    def __init__(self, ip, port, pool_size=10,source_port_start=30000,source_port_end=50000):
        self.ip = ip
        self.port = port
        self.pool_size = pool_size
        self.source_port_start = source_port_start
        self.next_source_port = source_port_start
        self.source_port_end = source_port_end
        self.pool = Queue(maxsize=pool_size)
        self.lock = threading.Lock() 
        self._create_pool()
        

    def _create_pool(self):
        for _ in range(self.pool_size):
            conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # Attempt to bind to a unique source port within range
            with self.lock:
                for port in range(self.next_source_port, self.source_port_end):
                    try:
                        conn.bind(('', port))
                        # print(f"Bound to source port: {port}")
                        self.next_source_port = port + 1
                        break
                    except OSError as e:
                        print(f"Port {port} is in use, trying next port.")
                        continue
                else:
                    print("No available ports in range.")
                    conn.close()
                    return None  # No available port to bind
            
            try:
                conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
                conn.connect((self.ip, self.port))
                self.pool.put((conn, port))
            except socket.error as e:
                print(f"Connection failed: {e}")
                conn.close()
    
    def get_connection(self):
        try:
            return self.pool.get_nowait()
        except Empty:
            return None

    def return_connection(self, conn_tuple):
        # Close the socket forcefully
        # conn_tuple[0].setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
        # conn_tuple[0].close()
        # conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        # conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # conn.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        # conn.bind(('', conn_tuple[1]))
        # conn.connect((self.ip, self.port))
        # if not self.pool.full():
        #     self.pool.put((conn,conn_tuple[1]))
        self.pool.put(conn_tuple)

    def close_all(self):
        while not self.pool.empty():
            conn_tuple = self.pool.get_nowait()
            conn_tuple[0].close()