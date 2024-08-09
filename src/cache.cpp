#include <vector>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct NodeConnectionDetails {
    std::string node_id;
    std::string ip;
    int port;
};

class Cache {
private:
    std::vector<NodeConnectionDetails> nodes_;
    size_t next_node_ = 0;
    std::hash<std::string> hasher_;
    std::mutex mutex_;
    std::string cluster_id_;
    std::string ip_;
    int port;
    std::string node_id_being_deleted_;
        // Helper function to send data over TCP/IP
    void sendToNode(const std::string& ip, int port, const std::string& request) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed.");
        }

        sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error("Invalid address/Address not supported.");
        }

        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sockfd);
            throw std::runtime_error("Connection failed.");
        }

        send(sockfd, request.c_str(), request.size(), 0);
        close(sockfd);
    }
public:
    Cache(std::string& ip,int& port): ip_(ip), port_(port) {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        cluster_id_ = boost::uuids::to_string(uuid);
    }

    void addNode(const NodeConnectionDetails& node_connection_details) {
        std::lock_guard<std::mutex> lock(mutex_);
        //after migration completes
        nodes_.push_back(node_connection_details);
    }
    void removeNode(const std::string& node_id) {

        // Find the node with the matching node_id
        auto it = std::remove_if(nodes_.begin(), nodes_.end(),
            [&node_id](const NodeConnectionDetails& node) {
                return node.node_id == node_id;
            });

        // If we found the node, handle data migration
        if (it != nodes_.end()) {
            NodeConnectionDetails node_to_remove = *it;

            // Collect node IDs to migrate data to
            std::vector<std::string> target_nodes;
            for (const auto& node : nodes_) {
                if (node.node_id != node_id) {
                    target_nodes.push_back(node.node_id);
                }
            }

            // Send migration message to the target nodes
            migrateData(node_to_remove.node_id, target_nodes);

            node_id_being_deleted_ = node_to_remove.node_id;

            // Erase the node from the vector
            nodes_.erase(it, nodes_.end());
        } else {
            throw std::runtime_error("Node with ID " + node_id + " not found.");
        }
    }

    void migrateData(const std::string& node_id, const std::vector<std::string>& target_nodes) {
        // Construct the MIGRATE message
        std::string migrate_message = "MIGRATE " + node_id;
        for (const auto& target_node : target_nodes) {
            migrate_message += "," + target_node;
        }
        
        // Send the MIGRATE message to each target node
        for (const auto& node : nodes_) {
            if (std::find(target_nodes.begin(), target_nodes.end(), node.node_id) != target_nodes.end()) {
                sendToNode(node.ip, node.port, migrate_message);
            }
        }
    }
    std::string Cache::getClusterId() const {
        return cluster_id_;
    }

    void routeGetRequest(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Extract key from the request string
        size_t key_start = request.find("\r\n", 4) + 2;
        std::string key = request.substr(key_start, request.find("\r\n", key_start) - key_start);

        // Find the node that holds the key
        for (const auto& node : nodes_) {
                std::string response = sendToNode(node.ip, node.port, request);
                if (!response.empty() && response[0] == '$') { // Assuming a valid response starts with '$'
                    std::cout << "Response from node " << node.ip << ":" << node.port << " - " << response << std::endl;
                    return;
                }
        }

        throw std::runtime_error("Key does not belong to any node in the routing table.");
    }

    void routeSetRequest(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t num_nodes = nodes_.size();
        if (num_nodes == 0) {
            std::cerr << "No nodes available to route request." << std::endl;
            return;
        }

        // Find the next node that is not being deleted
        size_t start_node = next_node_;
        do {
            const auto& node = nodes_[next_node_];
            if (node.node_id != node_id_being_deleted_) {
                // Valid node, proceed with sending request
                sendToNode(node.ip, node.port, request);
                next_node_ = (next_node_ + 1) % num_nodes;
                return;
            }
            // Skip to the next node
            next_node_ = (next_node_ + 1) % num_nodes;
        } while (next_node_ != start_node);

        std::cerr << "All nodes are currently being deleted or no nodes available." << std::endl;
    }

    void get(const std::string& key){

        std::lock_guard<std::mutex> lock(mutex_);
        
        // Ensure the key is not empty
        if (key.empty()) {
            std::cerr << "Key is empty. Cannot route request." << std::endl;
            return;
        }

        // Construct the GET request according to the protocol
        std::string request = "*2\r\n$3\r\nGET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";


        routeGetRequest(request);

    }
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Ensure the key and value are not empty
        if (key.empty() || value.empty()) {
            std::cerr << "Key or value is empty. Cannot route request." << std::endl;
            return;
        }

        // Construct the SET request according to the protocol
        std::string request = "*3\r\n$3\r\nSET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
        request += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";

        routeSetRequest(request);
    }
    
}