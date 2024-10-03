#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Array to store PIDs of all spawned processes
PIDS=()

# Variable to track if cleanup has already been run
CLEANED_UP=false

# Function to clean up all background processes
cleanup() {
    if [ "$CLEANED_UP" = false ]; then
        echo "Cleaning up..."
        for PID in "${PIDS[@]}"; do
            if kill -0 "$PID" 2>/dev/null; then
                echo -e "Killing process with PID: $PID \n"
                kill -9 "$PID" 2>/dev/null || echo "Failed to kill process $PID"

                # Wait for the process to actually terminate
                WAIT_TIME=0
                while kill -0 "$PID" 2>/dev/null; do
                    if [ "$WAIT_TIME" -ge 10 ]; then
                        echo "Process $PID did not terminate within 10 seconds, force exiting."
                        break
                    fi
                    sleep 1
                    WAIT_TIME=$((WAIT_TIME + 1))
                done

                # Confirm if process is still running after waiting
                if kill -0 "$PID" 2>/dev/null; then
                    echo -e "\nProcess $PID is still running after waiting."
                else
                    echo -e "\nProcess $PID terminated successfully."
                fi
            else
                echo "Process $PID is already terminated."
            fi
        done
        CLEANED_UP=true  # Mark cleanup as done
        echo "Cleanup Finished."
    else
        echo "Cleanup already done."
    fi
}

# Trap SIGINT, SIGTERM, and EXIT to clean up
trap cleanup SIGINT SIGTERM EXIT

# Remove existing log files if they exist
echo "Cleaning up old log files..."
if [ -f "cluster_output.log" ]; then
    rm "cluster_output.log"
fi

for PORT in 7070 7071 7072 7073; do
    LOG_FILE="node_${PORT}_output.log"
    if [ -f "$LOG_FILE" ]; then
        rm "$LOG_FILE"
    fi
done

# Remove results.txt if it exists
if [ -f "results.txt" ]; then
    echo "Removing old results.txt file..."
    rm "results.txt"
fi

# Build the project
echo "Building the project..."
conan install . --build=missing
cmake --preset conan-release
cmake --build --preset conan-release

# Run unit tests
echo "Running unit tests..."
ctest --verbose --test-dir ./build/Release  

# Check if unit tests passed
if [ $? -ne 0 ]; then
    echo "Unit tests failed. Exiting."
    exit 1
fi

# Set up the distributed cache cluster (run in background and capture output)
echo "Creating cache cluster..."
./build/Release/distributed_cache create_cache_cluster > cluster_output.log 2>&1 &

# Get the process ID of the background process
CLUSTER_PID=$!
PIDS+=("$CLUSTER_PID")  # Store the PID of the cluster process

# Wait until the output contains the Cluster ID
echo "Waiting for Cluster ID..."
while ! grep -q 'Cluster ID:' cluster_output.log; do
    sleep 1  # Wait for 1 second before checking again
done

# Extract Cluster ID from the output
CLUSTER_ID=$(grep 'Cluster ID:' cluster_output.log | awk '{print $3}')
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

    NODE_PID=$!
    PIDS+=("$NODE_PID")  # Store the PID of the node process

    # Wait until the log file contains the expected output
    echo "Waiting for node $PORT to finish setup..."
    while ! grep -q 'Cache Node Server is running on' "$LOG_FILE"; do
        sleep 1  # Wait for 1 second before checking again
    done

    # Extract Node ID from the log file
    NODE_ID=$(grep 'Node Id:' "$LOG_FILE" | awk '{print $3}')
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

# Cleanup (this will also be triggered by the trap)
cleanup

echo "See Test results in results.txt"