import socket
import threading
import random
import string
import argparse
import os
import asyncio
import time
from tqdm.asyncio import tqdm
from progress_bar import ProgressBar 
from connection_pool import ConnectionPool

def send_request(message, conn):
        try:
            conn.sendall(message.encode())
            response = conn.recv(1024)
            return response.decode()
        except socket.timeout:
            return "TIMEOUT"  # Return a specific message on timeout
        except Exception as e:
            return f"ERROR: {str(e)}"  # Return the error message

def random_string(length):
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for i in range(length))

def construct_get_message(key):
    return f"*2\r\n$3\r\nGET\r\n${len(key)}\r\n{key}\r\n"

def construct_set_message(key, value):
    return f"*3\r\n$3\r\nSET\r\n${len(key)}\r\n{key}\r\n${len(value)}\r\n{value}\r\n"

async def send_request_async(pool, key,retries=10000):
    for attempt in range(retries):
        conn_tuple = pool.get_connection()
        if conn_tuple is not None:
            message = construct_get_message(key)
            try:
                conn_tuple[0].sendall(message.encode())
                response = conn_tuple[0].recv(1024).decode()
                return response
            except socket.error as e:
                return f"ERROR: {e}"
            except socket.timeout:
                return "TIMEOUT"  # Return a specific message on timeout
            finally:
                # Always return the connection to the pool
                pool.return_connection(conn_tuple)
        else:
            continue
    return "No connection available after retries"

# Updated concurrent_test with progress tracking
async def concurrent_test(pool, keys,max_concurrency=os.cpu_count()):
    if max_concurrency is None:
        max_concurrency =  8
    # Use tqdm for async tasks
    with tqdm(total=len(keys), desc="Processing keys", unit="request") as progress:
        valid_responses_count = 0
        semaphore = asyncio.Semaphore(max_concurrency)  # Control number of active tasks
        log_entries = []
        
        async def limited_request(key):
            async with semaphore:
                progress.update(1)
                return await send_request_async(pool, key)

        tasks = [limited_request(key) for key in keys]
        
        # Gather responses with progress tracking
        for task in asyncio.as_completed(tasks):
            response = await task
              # Update progress bar after each task completion
            
            if response.strip().startswith("$"):  # Check for valid response
                valid_responses_count += 1
            elif response == "TIMEOUT":
                log_entries.append(f"\nRequest for key timed out.")  # Log the timeout
            else:
                log_entries.append(f"\nError response: {response}")  # Log other errors
                    
    with open("concurrent_test_logs.log", "a") as log_file:
        log_file.writelines("\n".join(log_entries))
    print(f"Total valid responses: {valid_responses_count} out of {len(keys)}")
    return valid_responses_count

def set_keys(pool, key_value_pairs,progress_bar):
    total_pairs = len(key_value_pairs)
    with open("concurrent_test_logs.log", "a") as log_file:
        for index, (key, value) in enumerate(key_value_pairs.items()):
            conn_tuple = pool.get_connection()
            message = construct_set_message(key, value)
            response = send_request(message, conn_tuple[0])
            pool.return_connection(conn_tuple)
            log_file.write(f"\nSet {key}: {response.strip()}")
            if "+OK" not in response:
                log_file.write(f"Error setting key {key}")
                
            # Update progress
            progress_bar.update(index + 1)
            
    time.sleep(0.2) 
    progress_bar.stop()  # Stop the progress bar after completion
    print("\nSET requests completed.")

if __name__ == "__main__":
    # Command line argument parsing
    parser = argparse.ArgumentParser(description="Cache server IP and port configuration.")
    parser.add_argument('--cache_server_ip', type=str, required=True, help='IP address of the cache server')
    parser.add_argument('--cache_server_port', type=int, required=True, help='Port number of the cache server')

    # Parse the arguments
    args = parser.parse_args()
    
    cache_server_ip = args.cache_server_ip
    cache_server_port = args.cache_server_port
    
    # Initialize the connection pool
    pool_size = 20  # Number of reusable connections in the pool
    pool = ConnectionPool(cache_server_ip, cache_server_port, pool_size=pool_size)
    
    base_pairs = 50  # Start with 50 key-value pairs
    increment = 50   # Increment by 50 for each test
    max_attempts = 10  # Limit the number of attempts
    
    valid_responses_count = 0

    for attempt in range(max_attempts):
        # Generate random key-value pairs
        key_value_pairs = {f"key{i}": random_string(6) for i in range(1, base_pairs + 1)}
        
         # Create a ProgressBar instance for SET requests
        progress_bar_set = ProgressBar(len(key_value_pairs))
        
        # Start the progress bar thread for SET requests
        progress_thread_set = threading.Thread(target=progress_bar_set.display)
        
        
        print(f"Starting SET requests to set {base_pairs} keys ...")
        progress_thread_set.start()
        set_keys(pool, key_value_pairs,progress_bar_set)  # Setting keys in cache
        
        
         # Wait for the progress thread to finish
        progress_thread_set.join()
        
        # Prepare to retrieve keys
        keys = list(key_value_pairs.keys())  # Use the same keys for retrieval
        
        # Perform concurrent GET requests
        print(f"\nStarting concurrent GET requests for {base_pairs} keys...")
        valid_responses_count = asyncio.run(concurrent_test(pool, keys))
        
        # Check if all responses were valid
        if valid_responses_count == len(keys):
            print(f"All {valid_responses_count} responses were valid. Increasing load.")
            base_pairs += increment  # Increase the number of key-value pairs
        else:
            print(f"Received {valid_responses_count} valid responses out of {len(keys)}. Stopping test.")
            break  # Stop if not all responses are valid
    
    with open('results.txt', 'a') as result_file:
        result_file.write(f"Server was able to handle {valid_responses_count} concurrent requests.\n")
    
    # Close all connections in the pool after tests are complete
    pool.close_all()
    
    print("\nConcurrent Tests completed.")