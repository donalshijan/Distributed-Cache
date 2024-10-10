#!/bin/bash

# Import common functions
source ./common.sh

# Remove old logs
echo "Cleaning up old logs..."
rm -f cluster_output.log node_*_output.log

# Build the project
build_project

# Set up the cluster
setup_cluster

# Add nodes to the cluster
add_nodes_to_cluster

# Check if the folder exists
if [ -d "JmeterConcurrencyTestFiles" ]; then
# Delete the folder and all its contents
echo "Removing old JmeterConcurrencyTestFiles file..."
rm -rf JmeterConcurrencyTestFiles
fi

# JMeter test case
jmeter -n -t Distributed_Cache_Concurrency_Test.jmx  -l JmeterConcurrencyTestFiles/testResults.jtl -j JmeterConcurrencyTestFiles/testlogs.log


echo "All tests completed."

# Cleanup
cleanup

echo "See Test results in JmeterConcurrencyTestResults.txt"
echo "See Test logs in JmeterConcurrencyTestFiles/testlogs.log"
