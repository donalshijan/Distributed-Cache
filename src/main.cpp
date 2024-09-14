#include <iostream>
#include "cache_node.h"
#include <thread>

int main() {
    std::string ip = "192.168.1.1";
    int  port =4569;
    Cache cache(ip, port);
    CacheNode node =  CacheNode(1024 * 1024, std::chrono::seconds(5),EvictionStrategy::NoEviction); // 1MB max memory, 5 seconds TTL
    node.assignToCluster(cache.getClusterId(),ip,port);
    NodeConnectionDetails node_connection_details;
    node_connection_details.node_id=node.getNodeId();
    node_connection_details.ip="192.168.1.1";
    node_connection_details.port=6379;
    cache.addNode(node_connection_details);
    // Setting values
    cache.set("key1", "value1");
    try{
        std::cout << "key1: " << cache.get("key1") << std::endl;
    }
    catch(const std::runtime_error& e){
        std::cerr << e.what() << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(6)); // Wait for TTL to expire
    try{
        std::cout << "key1 after TTL: " << cache.get("key1") << std::endl;
    }
    catch(const std::runtime_error& e){
        std::cerr << e.what() << std::endl;
    }

    try{
        std::cout << "key3: " << cache.get("key3") << std::endl; // Key not found
    }
    catch(const std::runtime_error& e){
        std::cerr << e.what() << std::endl;
    }
    
    

    return 0;
}