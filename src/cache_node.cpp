#include "cache_node.h"
#include <new>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "cache.h"

#define PORT 8069

CacheNode::CacheNode(size_t max_memory,std::chrono::seconds ttl, EvictionStrategy strategy) : max_memory_(max_memory), used_memory_(0), ttl_(ttl), strategy_(strategy) {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    node_id_ = boost::uuids::to_string(uuid);
}

void CacheNode::set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        remove_expired();
        // Calculate the size of the new key-value pair
        size_t key_size = key.size();
        size_t value_size = value.size();
        size_t total_size = key_size + value_size + sizeof(std::chrono::steady_clock::time_point);

        // Check if the new key-value pair can fit in the remaining memory
        if (total_size + used_memory_ > max_memory_) {
            evict();
        }

        try {
            // Insert the key-value pair into the store
            auto now = std::chrono::steady_clock::now();
            store_[key] = {value, now};
            used_memory_ += total_size;
            if (strategy_ == LRU) {
                // Check if the key is already in the LRU list
                    if (lru_map_.find(key) != lru_map_.end()) {
                        // Remove the key from its current position in the LRU list
                        lru_list_.erase(lru_map_[key]);
                    }
                // Add the key to the front of the LRU list (most recently used)
                lru_list_.push_front(key);
                // Update the map with the new position of the key in the LRU list
                lru_map_[key] = lru_list_.begin();
            } else if (strategy_ == LFU) {
                frequency_[key]++;
            }

        } catch (const std::bad_alloc&) {
            std::cerr << "Memory allocation failed." << std::endl;
        }
    }

std::string CacheNode::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
     if (it != store_.end()) {
        if (std::chrono::steady_clock::now() - it->second.second < ttl_) {
                if (strategy_ == LRU) {
                    // Move key to the front of LRU list
                    lru_list_.erase(lru_map_[key]);
                    lru_list_.push_front(key);
                    lru_map_[key] = lru_list_.begin();
                } else if (strategy_ == LFU) {
                    // Update frequency count
                    frequency_[key]++;
                }
            return it->second.first;
        } else {
            // Item is expired
            used_memory_ -= (it->first.size() + it->second.first.size() + sizeof(std::chrono::steady_clock::time_point));
            store_.erase(it);
        }
    }
    return ""; // Return empty string if key not found
}

size_t CacheNode::getMaxMemory() const {
    return max_memory_;
}

size_t CacheNode::getUsedMemory() const {
    return used_memory_;
}

void CacheNode::remove_expired() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = store_.begin(); it != store_.end(); ) {
        if (now - it->second.second >= ttl_) {
            used_memory_ -= (it->first.size() + it->second.first.size() + sizeof(std::chrono::steady_clock::time_point));
            it = store_.erase(it); // Remove expired item
        } else {
            ++it;
        }
    }
}

void CacheNode::evict() {
    if (strategy_ == LRU) {
        evictLRU();
    } else if (strategy_ == LFU) {
        evictLFU();
    } else if (strategy_ == NoEviction) {
        std::cerr << "No eviction strategy selected." << std::endl;
    }
}

void CacheNode::evictLRU() {
    while (used_memory_ > max_memory_ && !lru_list_.empty()) {
        auto key = lru_list_.back();
        remove(key);
    }
}

void CacheNode::evictLFU() {
    while (used_memory_ > max_memory_ && !frequency_.empty()) {
        auto min_freq = std::min_element(frequency_.begin(), frequency_.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        remove(min_freq->first);
    }
}

void CacheNode::remove(const std::string& key) {
    auto it = store_.find(key);
    if (it != store_.end()) {
        size_t key_size = key.size();
        size_t value_size = it->second.first.size();
        used_memory_ -= (key_size + value_size);

        if (strategy_ == LRU) {
            lru_list_.erase(lru_map_[key]);
            lru_map_.erase(key);
        } else if (strategy_ == LFU) {
            frequency_.erase(key);
        }

        store_.erase(it);
    }
}

std::string CacheNode::getNodeId() const {
    return node_id_;
}

void CacheNode::assignToCluster(const std::string& cluster_id, std::string& ip, int& port) {
    cluster_manager_details_.cluster_id_ = cluster_id;
    cluster_manager_details_.ip_=ip;
    cluster_manager_details_.port_=port;
}


void CacheNode::handle_client(int client_socket) {
    char buffer[1024];
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    buffer[bytes_read] = '\0';
    
    std::string request(buffer);
    std::string response;

    response=processRequest(request);

    write(client_socket, response.c_str(), response.length());
    close(client_socket);
}

std::string CacheNode::processRequest(const std::string& request) {
    size_t pos = 0;

    // Skip the command count prefix, e.g., "*2\r\n"
    pos = request.find("\r\n");

    if (pos != std::string::npos) {
        pos += 2; // Move past "\r\n"
    }

    // Parse the command
    if (request.substr(pos, 3) == "GET") {
        pos += 4; // Move past "GET\r\n"

        // Parse key length
        size_t key_length_start = request.find("$", pos) + 1;
        size_t key_length_end = request.find("\r\n", key_length_start);
        int key_length = std::stoi(request.substr(key_length_start, key_length_end - key_length_start));

        // Extract key
        pos = key_length_end + 2; // Move past "\r\n"
        std::string key = request.substr(pos, key_length);
        
        pos += key_length + 2; // Move past key and "\r\n"

        return get(key);

    } else if (request.substr(pos, 3) == "SET") {
        // Process SET request here (code provided earlier)
        pos += 4; // Move past "SET\r\n"

        // Parse key length
        size_t key_length_start = request.find("$", pos) + 1;
        size_t key_length_end = request.find("\r\n", key_length_start);
        int key_length = std::stoi(request.substr(key_length_start, key_length_end - key_length_start));

        // Extract key
        pos = key_length_end + 2; // Move past "\r\n"
        std::string key = request.substr(pos, key_length);
        
        pos += key_length + 2; // Move past key and "\r\n"

        // Parse value length
        size_t value_length_start = request.find("$", pos) + 1;
        size_t value_length_end = request.find("\r\n", value_length_start);
        int value_length = std::stoi(request.substr(value_length_start, value_length_end - value_length_start));

        // Extract value
        pos = value_length_end + 2; // Move past "\r\n"
        std::string value = request.substr(pos, value_length);
        
        // Store the key-value pair
        set(key,value);
        return  "+OK\r\n";
    }
return "-ERR unknown command\r\n";
}

void CacheNode::start_node_server() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, 3);

    std::cout << "Server is running on port " << PORT << std::endl;

    while (true) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket >= 0) {
            handle_client(client_socket);
        }
    }

    close(server_socket);
}
