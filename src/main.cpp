#include <iostream>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "cache_node.h"


void showUsageMessage(const std::string& command) {
    if (command == "add_new_node_to_existing_cluster") {
        std::cerr << "Usage: add_new_node_to_existing_cluster --memory_limit <bytes> "
                     "--time_till_eviction <seconds> --eviction_strategy <NoEviction|LRU|LFU> "
                     "--cluster_id <id> --cluster_ip <ip> --cluster_port <port> [--ip <node_ip>] [--port <node_port>]" << std::endl;
    }
    else if (command == "create_cache_cluster") {
        std::cerr << "Usage: create_cache_cluster [--ip <ip>] [--port <port>]" << std::endl;
    } 
    else if (command == "GET") {
        std::cerr << "Usage:  GET <cache_server_ip> <cache_server_port> <key> " << std::endl;
    }
    else if (command == "SET") {
        std::cerr << "Usage:  SET <cache_server_ip> <cache_server_port> <key> <value>" << std::endl;
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
    }
}

std::string sendTcpRequest(const std::string& cache_ip, int cache_port, const std::string& message) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};  // Buffer to hold the response

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return "";
    }

    // Configure server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(cache_port);

    // Convert IPv4 addresses from text to binary form
    if (inet_pton(AF_INET, cache_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return "";
    }

    // Connect to the cache server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        return "";
    }

    // Send the message to the cache server
    send(sock, message.c_str(), message.length(), 0);
    std::cout << "Message sent: " << message << std::endl;

    // Receive the response from the server
    int bytes_read = read(sock, buffer, 1024);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';  // Null-terminate the response
        std::cout << "Server response: " << buffer << std::endl;
    }

    // Close the socket
    close(sock);

    // Return the response as a string
    return std::string(buffer);
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "No command provided." << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "create_cache_cluster") {
        // Default values for optional IP and port
        std::string ip = DEFAULT_IP;
        int port = DEFAULT_CACHE_PORT;

        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "--ip") {
                ip = argv[++i];
            } else if (std::string(argv[i]) == "--port") {
                port = std::stoi(argv[++i]);
            }
        }

        if (argc>4) {
            std::cerr << "Invalid arguments for create_cache_cluster" << std::endl;
            showUsageMessage("create_cache_cluster");
            return 1;
        }

        // Create the cache instance and start the server
        Cache cache(ip, port);
        cache.startCacheServer();
         
        
    } else if (command == "add_new_node_to_existing_cluster") {
        // Required arguments
        size_t memory_limit = 0;
        int eviction_time = 0;
        EvictionStrategy eviction_strategy = NoEviction;

        // Optional IP and port for the new node
        std::string ip = DEFAULT_IP;
        int port = DEFAULT_CACHE_NODE_PORT;

        // Cluster IP and port
        std::string cluster_ip;
        int cluster_port = 0;
        std::string cluster_id;
        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "--memory_limit") {
                memory_limit = std::stoul(argv[++i]);
            } else if (std::string(argv[i]) == "--time_till_eviction") {
                eviction_time = std::stoi(argv[++i]);
            } else if (std::string(argv[i]) == "--eviction_strategy") {
                std::string strategy = argv[++i];
                if (strategy == "NoEviction") {
                    eviction_strategy = NoEviction;
                } else if (strategy == "LRU") {
                    eviction_strategy = LRU;
                } else if (strategy == "LFU") {
                    eviction_strategy = LFU;
                } else {
                    std::cerr << "Invalid eviction strategy." << std::endl;
                    return 1;
                }
            } else if (std::string(argv[i]) == "--ip") {
                ip = argv[++i];
            } else if (std::string(argv[i]) == "--port") {
                port = std::stoi(argv[++i]);
            } else if (std::string(argv[i]) == "--cluster_id") {
                cluster_id = argv[++i];
            } else if (std::string(argv[i]) == "--cluster_ip") {
                cluster_ip = argv[++i];
            } else if (std::string(argv[i]) == "--cluster_port") {
                cluster_port = std::stoi(argv[++i]);
            }
        }

        // Check if mandatory arguments are provided
        if (memory_limit == 0 || eviction_time == 0 || cluster_ip.empty() || cluster_port == 0) {
            std::cerr << "Invalid arguments for add_new_node_to_existing_cluster." << std::endl;
            showUsageMessage("add_new_node_to_existing_cluster");
            return 1;
        }

        // Create the cache node instance and add to cluster
        CacheNode node(memory_limit, std::chrono::seconds(eviction_time), eviction_strategy, ip, port);
        node.assignToCluster(cluster_id,cluster_ip, cluster_port);

    }
    else if(command == "GET"){
        if (argc != 5) {
            std::cerr << "Invalid arguments for GET." << std::endl;
            showUsageMessage("GET");
            return 1;
        }
        std::string cache_ip = argv[2];
        int cache_port = std::stoi(argv[3]);
        std::string key = argv[4];

        std::string request = "*2\r\n$3\r\nGET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
        try{
            std::string response = sendTcpRequest(cache_ip, cache_port, request);
            // Parse and validate the response
            if (response.empty()) {
                throw std::runtime_error("Empty response from cache.");
            } else if (response == "Error: Key does not belong to any node in the routing table.") {
                throw std::runtime_error("Key not found.");
            } else if (response[0] == '$') {
                // Extract value from response
                size_t value_start = response.find("\r\n") + 2;
                std::string value = response.substr(value_start, response.find("\r\n", value_start) - value_start);
                std::cout << "Value: " << value << std::endl;
            } else if (response.find("Error: ") == 0){
                std::cout << "Custom Error: " << response << std::endl;
            } 
            else if (response == "-ERR unknown command\r\n"){
                std::cout << response;
            }
            else {
                throw std::runtime_error("Invalid response format.");
            }
        }catch(const std::runtime_error& e){
            std::cerr << e.what() << std::endl;
        }
        
    } 
    else if(command == "SET"){
        if (argc != 6) {
            std::cerr << "Invalid arguments for SET." << std::endl;
            showUsageMessage("SET");
            return 1;
        }
        std::string cache_ip = argv[2];
        int cache_port = std::stoi(argv[3]);
        std::string key = argv[4];
        std::string value = argv[5];
        std::string request = "*3\r\n$3\r\nSET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
        request += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";

        try{
            std::string response = sendTcpRequest(cache_ip, cache_port, request);
            if (response=="+OK\r\n"){
            std::cout<<"Success"<<std::endl;
            }
            else{
                std::cout<<"Set Failed : " <<response<<std::endl; 
            }
        }catch(const std::runtime_error& e){
            std::cerr << e.what() << std::endl;
        }
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
