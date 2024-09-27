#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Build the project
echo "Building the project..."
cmake --preset conan-release
cmake --build --preset conan-release

# Set up the distributed cache cluster (run in background and capture output)
echo "Creating cache cluster..."
./build/Release/distributed_cache create_cache_cluster > cluster_output.log 2>&1 &

# Get the process ID of the background process
CLUSTER_PID=$!

# Wait until the output contains the Cluster ID
echo "Waiting for Cluster ID..."
while ! grep -q 'Cluster ID:' cluster_output.log; do
    sleep 1  # Wait for 1 second before checking again
done

# Extract Cluster ID from the output
CLUSTER_ID=$(grep -oP 'Cluster ID:\K[^\s]+' cluster_output.log)
echo "Cluster ID extracted: $CLUSTER_ID"


# Define cache server IP and port
CLUSTER_IP="127.0.0.1"
CLUSTER_PORT="7069"

# Memory limit and eviction time
MEMORY_LIMIT=1048576
EVICTION_TIME=500

# Array of ports for the four cache nodes
NODE_PORTS=(7070 7071 7072 7073)

# Add four new nodes to the cache cluster
echo "Adding ${#NODE_PORTS[@]} nodes to the cache cluster..."

for PORT in "${NODE_PORTS[@]}"
do
    LOG_FILE="node_${PORT}_output.log"
    
    echo "Starting node on port $PORT..."
    ./build/Release/distributed_cache add_new_node_to_existing_cluster \
        --memory_limit "$MEMORY_LIMIT" \
        --time_till_eviction "$EVICTION_TIME" \
        --cluster_id "$CLUSTER_ID" \
        --cluster_ip "$CLUSTER_IP" \
        --cluster_port "$CLUSTER_PORT" \
        --port "$PORT" > "$LOG_FILE" 2>&1 &

    # Wait until the log file contains the expected output
    echo "Waiting for node $PORT to finish setup..."
    while ! grep -q 'Cache Node Server is running on' "$LOG_FILE"; do
        sleep 1  # Wait for 1 second before checking again
    done

    # Extract Node ID from the log file
    NODE_ID=$(grep -oP 'Node Id:\K[^\s]+' "$LOG_FILE")
    echo "Node with ID : $NODE_ID listening on  port: $PORT"
done

# Install dependencies
echo "Installing dependencies..."
pip3 install -r requirements.txt

# Run sequential tests
echo "Running sequential tests..."
python3 sequential_tests.py --cache_server_ip "$CLUSTER_IP" --cache_server_port "$CLUSTER_PORT"

# Run concurrent tests
echo "Running concurrent tests..."
python3 concurrent_tests.py --cache_server_ip "$CLUSTER_IP" --cache_server_port "$CLUSTER_PORT"

echo "All tests completed."


