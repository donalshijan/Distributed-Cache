// #include <gtest/gtest.h>
// #include <thread>
// #include "cache_node.h"

// // Unit Test for Cache::set and Cache::get
// TEST(CacheTest, SetAndGet) {
//     std::string ip = "127.0.0.1";
//     int port = 7069;
//     Cache cache(ip, port);
//     CacheNode node = CacheNode(1024 * 1024, std::chrono::seconds(5), EvictionStrategy::NoEviction); // 1MB max memory, 5 seconds TTL
//     node.assignToCluster(cache.getClusterId(), ip, port);
    
//     NodeConnectionDetails node_connection_details;
//     node_connection_details.node_id = node.getNodeId();
//     node_connection_details.ip = ip;
//     node_connection_details.port = 8069;
//     cache.addNode(node_connection_details);

//     // Test setting values
//     std::string setResponse = cache.set("key1", "value1");
//     EXPECT_EQ(setResponse, "+OK\r\n") << "Set operation failed!";

//     // Test getting the value
//     std::string response = cache.get("key1");
//     EXPECT_FALSE(response.empty());
//     EXPECT_NE(response, "$-1\r\n");
    
//     if (response[0] == '$') {
//         size_t value_start = response.find("\r\n") + 2;
//         std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
//         EXPECT_EQ(value, "value1");
//     } else {
//         FAIL() << "Unexpected response format: " << response;
//     }

//     // Test TTL expiration
//     std::this_thread::sleep_for(std::chrono::seconds(6));
//     response = cache.get("key1");
//     EXPECT_EQ(response, "$-1\r\n") << "TTL expiration failed!";
// }

// // Unit Test for Cache::get with non-existing key
// TEST(CacheTest, GetNonExistingKey) {
//     std::string ip = "127.0.0.1";
//     int port = 7069;
//     Cache cache(ip, port);
    
//     std::string response = cache.get("key3");
//     EXPECT_EQ(response, "$-1\r\n") << "Expected key not found!";
// }

// int main(int argc, char **argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
