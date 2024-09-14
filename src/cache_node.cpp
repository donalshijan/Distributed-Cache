#include "cache_node.h"
#include <new>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <future>
#include <arpa/inet.h>

#define PORT 8069

CacheNode::CacheNode(size_t max_memory,std::chrono::seconds ttl, EvictionStrategy strategy,int port) : max_memory_(max_memory), used_memory_(0), ttl_(ttl), strategy_(strategy),port_(port) {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    node_id_ = boost::uuids::to_string(uuid);
}

std::string CacheNode::set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        remove_future_ = std::async(std::launch::async, &CacheNode::remove_expired, this);
        // Calculate the size of the new key-value pair
        size_t key_size = key.size();
        size_t value_size = value.size();
        size_t total_size = key_size + value_size + sizeof(std::chrono::steady_clock::time_point);

        auto it = store_.find(key);

        // If the key already exists, only update the value and timestamp
        if (it != store_.end()) {
            // Calculate the difference in memory usage for the value change
            size_t old_value_size = it->second.first.size();
            used_memory_ -= old_value_size;  // Subtract the old value size
            used_memory_ += value_size;      // Add the new value size

            // Update the value and timestamp
            it->second.first = value;
            it->second.second = std::chrono::steady_clock::now();

            if (strategy_ == LRU) {
                // Check if the key is already in the LRU list
                    if (lru_map_.find(key) != lru_map_.end()) {
                        // Remove the key from its current position in the LRU list
                        lru_list_.erase(lru_map_[key]);
                        lru_map_.erase(key);
                    }
                    lru_list_.push_front(key);
                    lru_map_[key] = lru_list_.begin();
            } else if (strategy_ == LFU) {
                frequency_[key]++;
                min_heap_.emplace(frequency_[key], key);
            }

            return "Success: Key-value pair updated.";
        }

        // Check if the new key-value pair can fit in the remaining memory
        if (total_size + used_memory_ > max_memory_) {
            evict();
        }

        if (used_memory_>= max_memory_){
            return "Cache Full: Unable to insert new key-value pair.";
        }

        try {
            // Insert the key-value pair into the store
            auto now = std::chrono::steady_clock::now();
            store_[key] = {value, now};
            used_memory_ += total_size;
            if (strategy_ == LFU) {
                frequency_[key]++;
                min_heap_.emplace(frequency_[key], key);
            }
            return "Success: Key-value pair inserted.";
        } catch (const std::bad_alloc&) {
            std::cerr << "Memory allocation failed." << std::endl;
            return "Memory allocation failed.";
        }
    }

    void CacheNode::wait_for_removal() {
        if (remove_future_.valid()) {
            remove_future_.wait(); // Wait for the async task to finish
        }
    }

std::string CacheNode::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
     if (it != store_.end()) {
        if (std::chrono::steady_clock::now() - it->second.second < ttl_) {
                if (strategy_ == LRU) {
                    auto lru_it = lru_map_.find(key);
                    if (lru_it != lru_map_.end()) {
                        lru_list_.erase(lru_it->second);  // Erase the key from LRU list
                        lru_map_.erase(lru_it);           // Remove the key from LRU map
                    }
                    lru_list_.push_front(key);
                    lru_map_[key] = lru_list_.begin();
                } else if (strategy_ == LFU) {
                    // Update frequency count
                    frequency_[key]++;
                    min_heap_.emplace(frequency_[key], key);
                }
            // Update the last usage timestamp
            it->second.second = std::chrono::steady_clock::now();
            return it->second.first;
        } else {
            // Item is expired
            used_memory_ -= (it->first.size() + it->second.first.size() + sizeof(std::chrono::steady_clock::time_point));
            // Save the value before erasing the entry
            std::string expired_value = it->second.first;
            if (strategy_ == LRU) {
                    auto lru_it = lru_map_.find(it->first);
                    if (lru_it != lru_map_.end()) {
                        lru_list_.erase(lru_it->second);  // Erase the key from LRU list
                        lru_map_.erase(lru_it);           // Remove the key from LRU map
                    }
            } else if (strategy_ == LFU) {
                    // Remove item from LFU
                    frequency_.erase(key);
            }
            store_.erase(it);
            return expired_value;
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
            if (strategy_ == LRU) {
                    auto lru_it = lru_map_.find(it->first);
                    if (lru_it != lru_map_.end()) {
                        lru_list_.erase(lru_it->second);  // Erase the key from LRU list
                        lru_map_.erase(lru_it);           // Remove the key from LRU map
                    }
            } else if (strategy_ == LFU) {
                    // Remove item from LFU
                    frequency_.erase(it->first);
            }
            it = store_.erase(it); // Remove expired item
        } else {
            ++it;
        }
    }
}

void CacheNode::evict() {
    wait_for_removal();
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

// void CacheNode::evictLFU() {
//     while (used_memory_ > max_memory_ && !frequency_.empty()) {
//         auto min_freq = std::min_element(frequency_.begin(), frequency_.end(),
//             [](const auto& a, const auto& b) { return a.second < b.second; });
//         remove(min_freq->first);
//     }
// }
void CacheNode::evictLFU() {
    // Evict the least frequently used item
    while (used_memory_ > max_memory_ && !min_heap_.empty() && !frequency_.empty()) {
        auto least_used = min_heap_.top();
        std::string key = least_used.second;

        // Check if the frequency in the map matches the heap's top
        if (frequency_[key] == least_used.first) {
            remove(key);
            frequency_.erase(key);
            min_heap_.pop();  // Remove the key from the heap
            std::cout << "Evicted key: " << key << std::endl;
            return;
        } else {
            // The top of the heap is stale (old frequency), discard it
            min_heap_.pop();
        }
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

std::string Cache::sendToNode(const std::string& ip, int port, const std::string& request) {
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
        // Receive the response
        char buffer[1024] = {0};
        int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            close(sockfd);
            throw std::runtime_error("Failed to receive response.");
        }
        close(sockfd);
        return std::string(buffer, bytes_received);
    }

std::string CacheNode::processRequest(const std::string& request) {
    size_t pos = 0;

    // Skip the command count prefix, e.g., "*2\r\n"
    pos = request.find("\r\n");

    if (pos != std::string::npos) {
        pos += 2; // Move past "\r\n"
    }

    // Parse the command
    if(request.substr(pos, 7)=="MIGRATE"){
        pos += 8; // Move past "MIGRATE\r\n"

        // Parse source node ID length and node ID
        size_t node_id_length_start = request.find("$", pos) + 1;
        size_t node_id_length_end = request.find("\r\n", node_id_length_start);
        int node_id_length = std::stoi(request.substr(node_id_length_start, node_id_length_end - node_id_length_start));

        pos = node_id_length_end + 2; // Move past "\r\n"
        std::string source_node_id = request.substr(pos, node_id_length);

        pos += node_id_length + 2; // Move past node ID and "\r\n"

        // Parse target nodes list
        size_t target_count_start = pos;
        int target_count = std::stoi(request.substr(pos, request.find("\r\n", target_count_start) - target_count_start));

        pos += std::to_string(target_count).length() + 2; // Move past target count

        std::vector<std::pair<std::string, int>> target_nodes; // List of (ip, port) tuples

        for (int i = 0; i < target_count; ++i) {
            // Parse each target node's IP:port
            size_t ip_port_length_start = request.find("$", pos) + 1;
            size_t ip_port_length_end = request.find("\r\n", ip_port_length_start);
            int ip_port_length = std::stoi(request.substr(ip_port_length_start, ip_port_length_end - ip_port_length_start));

            pos = ip_port_length_end + 2; // Move past "\r\n"
            std::string ip_port = request.substr(pos, ip_port_length);

            pos += ip_port_length + 2; // Move past IP:port and "\r\n"

            // Split IP and port
            size_t colon_pos = ip_port.find(":");
            std::string ip = ip_port.substr(0, colon_pos);
            int port = std::stoi(ip_port.substr(colon_pos + 1));

            target_nodes.emplace_back(ip, port);
        }

        // Round-robin key-value migration
        int target_index = 0;
        for (const auto& kv : store_) {
            const std::string& key = kv.first;
            const std::string& value = kv.second.first;

            // Construct the SET command for each target node
            std::stringstream set_message;
            set_message << "*3\r\n$3\r\nSET\r\n"
                        << "$" << key.size() << "\r\n" << key << "\r\n"
                        << "$" << value.size() << "\r\n" << value << "\r\n";

            // Send to the current target node
            const auto& target_node = target_nodes[target_index];
            sendToNode(target_node.first, target_node.second, set_message.str());

            // Move to the next target node (round-robin)
            target_index = (target_index + 1) % target_nodes.size();
        }

        return "+OK MIGRATION COMPLETED\r\n";
    }
    else if (request.substr(pos, 3) == "GET") {
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
        
       // Call the set method and get the result message
        std::string result = set(key, value);

        // Handle the response based on the result message
        if (result == "Success: Key-value pair inserted." || result == "Success: Key-value pair updated.") {
            return "+OK\r\n"; // Redis-like response for successful SET operation
        } else if (result == "Cache Full: Unable to insert new key-value pair.") {
            return "-ERROR Cache Full\r\n"; // Error message for cache full
        } else if (result == "Memory allocation failed.") {
            return "-ERROR Memory allocation failed\r\n"; // Error message for memory issues
        } else {
            return "-ERROR Unknown error\r\n"; // Catch-all for any other errors
        }
    }
return "-ERR unknown command\r\n";
}

void CacheNode::trackAndUpdateLRU() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10)); // Adjust the interval as needed

        std::lock_guard<std::mutex> lock(mutex_);
        
        // Create a vector of pairs to sort by timestamp
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> items;
        for (const auto& kv : store_) {
            items.push_back({kv.first, kv.second.second});
        }

        // Sort items by timestamp in decreasing order
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        // Clear existing LRU list and map
        lru_list_.clear();
        lru_map_.clear();

        // Update LRU list and map
        for (const auto& item : items) {
            lru_list_.push_front(item.first);
            lru_map_[item.first] = lru_list_.begin();
        }
    }
}

void CacheNode::remove_expired_periodically() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(ttl_+std::chrono::seconds(1))); // Adjust the interval as needed

        std::lock_guard<std::mutex> lock(mutex_);
        remove_expired(); // Call your existing remove_expired method
    }
}

void CacheNode::lazyDeleteStaleEntry() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Adjust the sleep interval as needed

        std::lock_guard<std::mutex> lock(mutex_); // Lock the data structures

        // Check if the min-heap is not empty
        while (!min_heap_.empty()) {
            auto min_entry = min_heap_.top(); // Get the minimum element from the heap (min frequency)

            const std::string& key = min_entry.second;
            int heap_frequency = min_entry.first;

            // Compare the heap frequency with the frequency stored in frequency_ map
            if (frequency_.find(key) != frequency_.end()) {
                int map_frequency = frequency_[key];

                if (heap_frequency != map_frequency) {
                    // Stale entry detected (heap's frequency is outdated)
                    min_heap_.pop(); // Remove it from the heap
                } else {
                    // No stale entry found, exit the loop
                    break;
                }
            } else {
                // Key is no longer in the map (probably removed), pop it from the heap
                min_heap_.pop();
            }
        }
    }
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
    
    if(strategy_==EvictionStrategy::LRU){
        // Start the LRU tracking and updating thread
        std::thread lru_thread(&CacheNode::trackAndUpdateLRU, this);
        lru_thread.detach(); // Detach the thread so it runs independently
    }
    if (strategy_ == EvictionStrategy::LFU) {
    // Start the LFU lazy deletion thread
    std::thread lfu_thread(&CacheNode::lazyDeleteStaleEntry, this);
    lfu_thread.detach(); // Detach the thread so it runs independently
}
    // Start the periodic expiration removal thread
    std::thread expire_thread(&CacheNode::remove_expired_periodically, this);
    expire_thread.detach(); // Detach the thread so it runs independently

    while (true) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket >= 0) {
            handle_client(client_socket);
        }
    }

    close(server_socket);
}
