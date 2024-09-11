#include <iostream>
#include "cache_node.h"
#include "cache.h"
#include <thread>

int main() {
    Cache cache= new Cache('192.168.1.1',4569);
    CacheNode node = new CacheNode(1024 * 1024, std::chrono::seconds(5)); // 1MB max memory, 5 seconds TTL
    node.assignToCluster(cache.getClusterId(),/*cache ip*/,/*cache port*/,);

    cache.addNode({"192.168.1.1", 6379,node.getNodeId()});
    // Setting values
    cache.set("key1", "value1");
    std::cout << "key1: " << cache.get("key1") << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(6)); // Wait for TTL to expire

    std::cout << "key1 after TTL: " << cache.get("key1") << std::endl;
    std::cout << "key3: " << cache.get("key3") << std::endl; // Key not found

    return 0;
}