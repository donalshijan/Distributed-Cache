
#!/bin/bash

# Import common functions
source ./common.sh
source ./run_unit_tests.sh


# Remove old logs
echo "Cleaning up old logs..."
rm -f cluster_output.log node_*_output.log

# Build the project
build_project

# run unit test
run_unit_tests

# Set up the cluster
setup_cluster

# Add nodes to the cluster
add_nodes_to_cluster

# Define cache server IP and port
CLUSTER_IP="127.0.0.1"
CLUSTER_PORT="7069"


# Remove results.txt if it exists
if [ -f "results.txt" ]; then
    echo "Removing old results.txt file..."
    rm "results.txt"
fi
# Install dependencies
echo "Installing dependencies..."
pip3 install -r requirements.txt

if [ -f "sequential_test_logs.log" ]; then
    rm "sequential_test_logs.log"
    echo "old sequential_test_logs.log deleted."
fi

# Run sequential tests
echo "Running sequential tests..."
python3 sequential_tests.py --cache_server_ip "$CLUSTER_IP" --cache_server_port "$CLUSTER_PORT"

if [ -f "concurrent_test_logs.log" ]; then
    rm "concurrent_test_logs.log"
    echo "old concurrent_test_logs.log deleted."
fi
# Run concurrent tests
echo "Running concurrent tests..."
# python3 concurrent_tests.py --cache_server_ip "$CLUSTER_IP" --cache_server_port "$CLUSTER_PORT"
python3 concurrent_tests_using_connection_and_thread_pooling.py --cache_server_ip "$CLUSTER_IP" --cache_server_port "$CLUSTER_PORT"

echo "All tests completed."

# Cleanup
cleanup

echo "See Test results in results.txt"
echo "See test logs in sequential_test_logs.log and concurrent_test_logs.log"

pkill -9 distributed_cache