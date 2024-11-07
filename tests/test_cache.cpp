#include <thread>
#include <chrono>
#include "cache_node.h"
#include <gtest/gtest.h>

// Function to start the cache server
void startCacheServer(Cache &cache) {
    cache.startCacheServer();
}

// Function to start the node server
void startNodeServer(CacheNode &node, Cache &cache, std::string &ip, int port) {
    try{node.assignToCluster(cache.getClusterId(), ip, port);}
    catch(const std::runtime_error& e){
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// Unit Test for Cache::set and Cache::get
TEST(CacheTest, SetAndGet) {
    std::string ip = "127.0.0.1";
    int port = 7069;
    Cache cache(ip, port);
    // Start the cache server in a separate thread
    std::thread cacheThread(startCacheServer, std::ref(cache));
    // Give some time for servers to start (optional)
    std::this_thread::sleep_for(std::chrono::seconds(5));

    CacheNode node = CacheNode(1024 * 1024, std::chrono::seconds(5), EvictionStrategy::NoEviction); // 1MB max memory, 5 seconds TTL
    // Start the node server in a separate thread
    std::thread nodeThread(startNodeServer, std::ref(node), std::ref(cache), std::ref(ip), port);

    // Give some time for servers to start (optional)
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Test setting values
    std::string setResponse = cache.set("key1", "value1");
    EXPECT_EQ(setResponse, "+OK\r\n") << "Set operation failed!";

    // Test getting the value
    try{
        std::string response = cache.get("key1");
        EXPECT_FALSE(response.empty());
        EXPECT_NE(response, "$-1\r\n");
        
        if (response[0] == '$') {
            size_t value_start = response.find("\r\n") + 2;
            std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
            EXPECT_EQ(value, "value1");
        } else {
            FAIL() << "Unexpected response format: " << response;
        }
    }
    catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
    }

    // Test TTL expiration
    std::this_thread::sleep_for(std::chrono::seconds(6));
    try{
        std::string response = cache.get("key1");
        EXPECT_FALSE(response.empty());
        EXPECT_NE(response, "$-1\r\n");
        
        if (response[0] == '$') {
            size_t value_start = response.find("\r\n") + 2;
            std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
            EXPECT_EQ(value, "value1");
        } else {
            FAIL() << "Unexpected response format: " << response;
        }
    }
    catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    try{
        std::string response = cache.get("key1");
        std::cout<< response <<std::endl;
        EXPECT_EQ(response, "-ERR: Key does not belong to any node in the routing table.") << "TTL expiration failed!";
    }
    catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }

    node.shutDown_node_server();
    cache.shutDownCacheServer();
    
    // std::this_thread::sleep_for(std::chrono::seconds(6));
    nodeThread.join();
    cacheThread.join();
    std::this_thread::sleep_for(std::chrono::seconds(2));

}

// Unit Test for Cache::get with non-existing key
TEST(CacheTest, GetNonExistingKey) {
    std::string ip = "127.0.0.1";
    int port = 7069;
    Cache cache(ip, port);
    // Start the cache server in a separate thread
    std::thread cacheThread(startCacheServer, std::ref(cache));
    
    // Give some time for servers to start (optional)
    std::this_thread::sleep_for(std::chrono::seconds(1));

    CacheNode node = CacheNode(1024 * 1024, std::chrono::seconds(5), EvictionStrategy::NoEviction,"127.0.0.1",8069); // 1MB max memory, 5 seconds TTL
    // Start the node server in a separate thread
    std::thread nodeThread(startNodeServer, std::ref(node), std::ref(cache), std::ref(ip), port);
    // Give some time for servers to start (optional)
    std::this_thread::sleep_for(std::chrono::seconds(2));
    try{
    std::string response = cache.get("key3");
    EXPECT_EQ(response, "-ERR: Key does not belong to any node in the routing table.") << "Expected key not found!";
    }
    catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    std::this_thread::sleep_for(std::chrono::seconds(3));
    node.shutDown_node_server();
    cache.shutDownCacheServer();
    nodeThread.join();
    cacheThread.join();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
