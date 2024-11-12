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
if [ -d "JmeterLoadTestFiles" ]; then
# Delete the folder and all its contents
echo "Removing old JmeterLoadTestFiles file..."
rm -rf JmeterLoadTestFiles
fi

# JMeter test case
jmeter -n -t Load_Test_For_Distributed_Cache.jmx  -l JmeterLoadTestFiles/testResults.jtl -j JmeterLoadTestFiles/testlogs.log


echo "All tests completed."

# Cleanup
cleanup

echo "See Test results by opening up testResults.jtl file under JmeterLoadTestFiles in various listeners added under thread group in jmeter gui mode"
echo "See Test logs in JmeterLoadTestFiles/testlogs.log"

pkill -9 distributed_cache