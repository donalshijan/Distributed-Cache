# common.sh

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
                echo -e "\nKilling process with PID: $PID "
                # Kill child processes of this PID
                # CHILD_PIDS=$(pgrep -P "$PID")
                # for CHILD_PID in $CHILD_PIDS; do
                #     echo "Killing child process with PID: $CHILD_PID"
                #     kill -9 -"$CHILD_PID" 2>/dev/null || echo "Failed to kill child process $CHILD_PID"
                # done
                # Now kill the parent process
                kill -15 "$PID" 2>/dev/null || echo "Failed to kill process $PID"

                WAIT_TIME=0
                while kill -0 "$PID" 2>/dev/null; do
                    if [ "$WAIT_TIME" -ge 10 ]; then
                        echo "Process $PID did not terminate within 10 seconds, force exiting."
                        break
                    fi
                    sleep 1
                    WAIT_TIME=$((WAIT_TIME + 1))
                done

                 # If process is still running, forcefully kill it using SIGKILL (9)
                if kill -0 "$PID" 2>/dev/null; then
                    echo "Forcing termination of process with PID: $PID"
                    kill -9 "$PID" 2>/dev/null || echo "Failed to forcefully kill process $PID"
                fi

                if kill -0 "$PID" 2>/dev/null; then
                    echo  "Process $PID is still running after forcing termination."
                else
                    echo  "Process $PID terminated successfully."
                fi
            else
                echo -e "\nProcess $PID is already terminated."
            fi
        done
        CLEANED_UP=true
        echo "Cleanup Finished."
    else
        echo "Cleanup already done."
    fi
}

# Function to build the project
build_project() {
    echo "Building the project..."
    conan install . --build=missing
    cmake --preset conan-custom-preset
    cmake --build --preset conan-custom-preset

}

# Trap SIGINT, SIGTERM, and EXIT to clean up
trap cleanup SIGINT SIGTERM EXIT

# Function to set up the cache cluster
setup_cluster() {
    echo "Setting up cache cluster..."
    ./build/Release/distributed_cache create_cache_cluster > cluster_output.log 2>&1 &
    CLUSTER_PID=$!
    PIDS+=("$CLUSTER_PID")

    echo "Waiting for Cluster ID..."
    while ! grep -q 'Cluster ID:' cluster_output.log; do
        sleep 1
    done

    CLUSTER_ID=$(grep 'Cluster' cluster_output.log | awk '{print $9}')
    echo "Cluster ID: $CLUSTER_ID"
}

# Function to add nodes to the cluster
add_nodes_to_cluster() {
    NODE_PORTS=(7070 7071 7072 7073 7074 7075 7076 7077 7078 7079)
    MEMORY_LIMIT=1048576
    EVICTION_TIME=500

    for PORT in "${NODE_PORTS[@]}"; do
        LOG_FILE="node_${PORT}_output.log"
        echo "Creating new node on port $PORT..."
        ./build/Release/distributed_cache add_new_node_to_existing_cluster \
            --memory_limit "$MEMORY_LIMIT" \
            --time_till_eviction "$EVICTION_TIME" \
            --cluster_id "$CLUSTER_ID" \
            --cluster_ip "127.0.0.1" \
            --cluster_port "7069" \
            --port "$PORT" > "$LOG_FILE" 2>&1 &

        NODE_PID=$!
        PIDS+=("$NODE_PID")

        echo "Waiting for node on $PORT to finish setup..."
        while ! grep -q 'Cache Node created' "$LOG_FILE"; do
            sleep 1
        done

        NODE_ID=$(grep 'node id' "$LOG_FILE" | awk '{print $9}')
        echo "Node created with ID: $NODE_ID"

        while ! grep -q 'Cache Node Server' "$LOG_FILE"; do
            sleep 1
        done
        echo "Node with ID: $NODE_ID started on port $PORT."
    done
}
