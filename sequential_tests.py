import socket
import time
import random
import string
import argparse
import threading
import sys
from progress_bar import ProgressBar  # Import the ProgressBar class
import select

# Helper function to send TCP request to the cache server
def send_request(message, ip, port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((ip, port))
        s.sendall(message.encode())
        response = s.recv(1024)
    return response.decode()

# Function to send requests in a separate thread
def send_get_requests(ip, port, key_value_pairs, sockets, request_times, lock,number_of_requests_sent,socket_key_map ):
    for key in key_value_pairs.keys():
        message = construct_get_message(key)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((ip, port))
        s.sendall(message.encode())
        time.sleep(0.000010)
        # Thread-safe addition to shared resources
        with lock:
            sockets.append(s)
            request_times[s] = time.time()  # Track the start time for this socket
            socket_key_map[s] = key 
            number_of_requests_sent[0] += 1  # Increment the count of requests sent

# Construct GET request for the cache
def construct_get_message(key):
    return f"*2\r\n$3\r\nGET\r\n${len(key)}\r\n{key}\r\n"

# Construct SET request for the cache
def construct_set_message(key, value):
    return f"*3\r\n$3\r\nSET\r\n${len(key)}\r\n{key}\r\n${len(value)}\r\n{value}\r\n"

# Helper function to generate a random string for values
def random_string(length):
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for i in range(length))

# Set test (50 key-value pairs)
def set_test(ip, port, key_value_pairs, progress_bar):
    total_pairs = len(key_value_pairs)
    with open("sequential_test_logs.log", "a") as log_file:
        for index, (key, value) in enumerate(key_value_pairs.items()):
            message = construct_set_message(key, value)
            response = send_request(message, ip, port)
            log_file.write(f"\nSet {key}: {response.strip()}")
            if "+OK" not in response:
                log_file.write(f"Error setting key {key}")
            
            # Update progress
            progress_bar.update(index + 1)
        
    time.sleep(0.2) 
    progress_bar.stop()  # Stop the progress bar after completion
    print("\nSET requests completed.")

# Function to monitor responses in a separate thread
def monitor_responses(sockets, request_times, key_value_pairs, progress_bar, total_keys, log_file, lock,number_of_requests_sent,socket_key_map):
    total_time = 0
    valid_responses_count = 0
    while True:
        with lock:
            active_sockets = list(sockets)  # Create a snapshot of current sockets
            all_requests_fired = number_of_requests_sent[0] == len(key_value_pairs)  # Check if all requests are fired
        if not active_sockets and all_requests_fired:
            break  # Exit when there are no more sockets to monitor
        
        if active_sockets:
            readable, _, _ = select.select(active_sockets, [], [], None)  # Timeout of 1 second

            for s in readable:
                response = s.recv(1024).decode()
                end_time = time.time()

                with lock:
                    duration = end_time - request_times[s]
                    key = socket_key_map[s]

                    # Log response and duration
                    log_file.write(f"\nRetrieved {key} in {duration:.6f} seconds")
                    log_file.write(f" Response: {response.strip()}")

                    # Check if the response is valid (starts with "$")
                    if response.strip().startswith("$"):
                        total_time += duration
                        valid_responses_count += 1

                    # Close the socket and remove it from shared resources
                    s.close()
                    sockets.remove(s)

                    # Update progress bar
                    progress_bar.update(number_of_requests_sent[0])

    
    time.sleep(0.1)  
    progress_bar.stop()  # Stop the progress bar after completion
    print("\nGET requests completed.")
    # Calculate and log results
    if valid_responses_count > 0:
        average_time = total_time / valid_responses_count
        result_message = f"Average retrieval time for valid responses: {average_time:.6f} seconds"
        print(result_message)
    else:
        print("No valid responses received.")
        result_message = f"No valid responses received during sequential tests."

    with open("results.txt", "a") as result_log:
        result_log.write(result_message + "\n")
    


# Main function to manage threads
def sequential_get_test_multiplexing(ip, port, key_value_pairs, progress_bar):
    total_keys = len(key_value_pairs)
    sockets = []
    request_times = {}
    socket_key_maps = {}
    lock = threading.Lock()  # For thread-safe access to shared resources
    number_of_requests_sent = [0]  # Use a list to allow updates across threads (mutable shared variable)

    with open("sequential_test_logs.log", "a") as log_file:
        # Create and start threads
        sender_thread = threading.Thread(target=send_get_requests, args=(ip, port, key_value_pairs, sockets, request_times, lock,number_of_requests_sent,socket_key_maps))
        monitor_thread = threading.Thread(target=monitor_responses, args=(sockets, request_times, key_value_pairs, progress_bar, total_keys, log_file, lock,number_of_requests_sent,socket_key_maps))

        sender_thread.start()
        monitor_thread.start()

        # Wait for threads to finish
        sender_thread.join()
        monitor_thread.join()
            
# Sequential GET test for previously set keys
def sequential_get_test(ip, port, key_value_pairs, progress_bar):
    total_keys = len(key_value_pairs)
    total_time = 0
    valid_responses_count = 0  # Track the number of valid responses
    with open("sequential_test_logs.log", "a") as log_file:
        for index, key in enumerate(key_value_pairs.keys()):
            message = construct_get_message(key)
            start_time = time.time()
            response = send_request(message, ip, port)
            end_time = time.time()
            
            duration = end_time - start_time
            log_file.write(f"\nRetrieved {key} in {duration:.6f} seconds")
            log_file.write(f"Response: {response.strip()}")
            
            # Check if the response is valid (starts with "$")
            if response.strip().startswith("$"):
                total_time += duration
                valid_responses_count += 1  # Increment valid response count
            
            # Update progress
            progress_bar.update(index + 1)
        
    time.sleep(0.2) 
    progress_bar.stop()  # Stop the progress bar after completion
    print("\nGET requests completed.")

    if valid_responses_count > 0:
        average_time = total_time / valid_responses_count
        result_message = f"Average retrieval time for valid responses: {average_time:.6f} seconds"
        print(result_message)
    else:
        print("No valid responses received.")
        result_message=f"No valid responses received during sequential tests."
        
    with open("results.txt", "a") as logfile:
            logfile.write(result_message + "\n")

if __name__ == "__main__":
    # Create argument parser
    parser = argparse.ArgumentParser(description="Cache client to perform SET and GET requests")
    
    # Add mandatory arguments for cache server IP and port
    parser.add_argument('--cache_server_ip', type=str, required=True, help='IP address of the cache server')
    parser.add_argument('--cache_server_port', type=int, required=True, help='Port number of the cache server')
    
    # Parse the command line arguments
    args = parser.parse_args()
    
    # Assign IP and port from parsed arguments
    cache_server_ip = args.cache_server_ip
    cache_server_port = args.cache_server_port
    
    # Generate 50 random key-value pairs
    key_value_pairs = {f"key{i}": random_string(6) for i in range(1, 51)}

     # Create a ProgressBar instance for SET requests
    progress_bar_set = ProgressBar(len(key_value_pairs))
    
    # Start the progress bar thread for SET requests
    progress_thread_set = threading.Thread(target=progress_bar_set.display)
    

    # Perform SET requests
    print("Starting SET requests...")
    progress_thread_set.start()
    set_test(cache_server_ip, cache_server_port, key_value_pairs, progress_bar_set)
    
    # Wait for the progress thread to finish
    progress_thread_set.join()
    
    # Create a new ProgressBar instance for GET requests
    progress_bar_get = ProgressBar(len(key_value_pairs))
    
    # Start the progress bar thread for GET requests
    progress_thread_get = threading.Thread(target=progress_bar_get.display)
    
    
    # Perform GET requests after setting the values
    print("\nStarting GET requests...")
    progress_thread_get.start()
    sequential_get_test_multiplexing(cache_server_ip, cache_server_port, key_value_pairs, progress_bar_get)
    
    # Wait for the progress thread to finish
    progress_thread_get.join()

    print("\nSequential Tests completed.")
