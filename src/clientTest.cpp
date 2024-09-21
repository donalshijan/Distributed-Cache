// #include <iostream>
// #include "cache_node.h"
// #include <thread>

// int mainTest() {
//     std::string ip = "192.168.1.1";
//     int  port =4569;
//     Cache cache(ip, port);
//     CacheNode node =  CacheNode(1024 * 1024, std::chrono::seconds(5),EvictionStrategy::NoEviction); // 1MB max memory, 5 seconds TTL
//     node.assignToCluster(cache.getClusterId(),ip,port);
//     NodeConnectionDetails node_connection_details;
//     node_connection_details.node_id=node.getNodeId();
//     node_connection_details.ip="192.168.1.1";
//     node_connection_details.port=6379;
//     cache.addNode(node_connection_details);
//     // Setting values
//     std::string setResponse=cache.set("key1", "value1");

//     if (setResponse=="+OK\r\n"){
//         std::cout<<"Success"<<std::endl;
//     }
//     else{
//         std::cout<<"Set Failed : " <<setResponse<<std::endl; 
//     }

//     try {
//     // Get the value for key1
//     std::string response = cache.get("key1");

//     // Parse and validate the response
//     if (response.empty()) {
//         throw std::runtime_error("Empty response from cache.");
//     } else if (response == "$-1\r\n") {
//         throw std::runtime_error("Key not found.");
//     } else if (response[0] == '$') {
//         // Extract value from response
//         size_t value_start = response.find("\r\n") + 2;
//         std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
//         std::cout << "key1: " << value << std::endl;
//     } else if (response.find("Error: ") == 0){
//         std::cout << "Custom Error: " << response << std::endl;
//     } 
//     else if (response == "-ERR unknown command\r\n"){
//         std::cout << response;
//     }
//     else {
//         throw std::runtime_error("Invalid response format.");
//     }
// } catch (const std::runtime_error& e) {
//     std::cerr << e.what() << std::endl;
// }

// std::this_thread::sleep_for(std::chrono::seconds(6)); // Wait for TTL to expire

// try {
//     // Get the value for key1 after TTL expiration
//     std::string response = cache.get("key1");

//     // Parse and validate the response
//     if (response.empty()) {
//         throw std::runtime_error("Empty response from cache.");
//     } else if (response == "$-1\r\n") {
//         throw std::runtime_error("Key not found.");
//     } else if (response[0] == '$') {
//         size_t value_start = response.find("\r\n") + 2;
//         std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
//         std::cout << "key1 after TTL: " << value << std::endl;
//     }else if (response.find("Error: ") == 0){
//         std::cout << "Custom Error: " << response << std::endl;
//     } 
//     else if (response == "-ERR unknown command\r\n"){
//         std::cout << response;
//     } 
//     else {
//         throw std::runtime_error("Invalid response format.");
//     }
// } catch (const std::runtime_error& e) {
//     std::cerr << e.what() << std::endl;
// }

// try {
//     // Get the value for key3 which does not exist
//     std::string response = cache.get("key3");

//     // Parse and validate the response
//     if (response.empty()) {
//         throw std::runtime_error("Empty response from cache.");
//     } else if (response == "$-1\r\n") {
//         throw std::runtime_error("Key not found.");
//     } else if (response[0] == '$') {
//         size_t value_start = response.find("\r\n") + 2;
//         std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
//         std::cout << "key3: " << value << std::endl;
//     } else if (response.find("Error: ") == 0){
//         std::cout << "Custom Error: " << response << std::endl;
//     } 
//     else if (response == "-ERR unknown command\r\n"){
//         std::cout << response;
//     }
//     else {
//         throw std::runtime_error("Invalid response format.");
//     }
// } catch (const std::runtime_error& e) {
//     std::cerr << e.what() << std::endl;
// }
    
    

//     return 0;
// }