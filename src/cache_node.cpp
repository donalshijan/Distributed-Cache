#include "cache_node.h"
#include <new>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <future>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <vector>




CacheNode::CacheNode(size_t max_memory,std::chrono::seconds ttl, EvictionStrategy strategy,std::string ip,int port) : max_memory_(max_memory), used_memory_(0), ttl_(ttl), strategy_(strategy),ip_(ip),port_(port) {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    node_id_ = boost::uuids::to_string(uuid);
    std::cout << "[CacheNode] Cache Node created with node id : "<<node_id_<< std::endl;
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
            std::cerr << "[CacheNode]  Memory allocation failed." << std::endl;
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
        std::cerr << "[CacheNode]  No eviction strategy selected." << std::endl;
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
            std::cout << "[CacheNode] Evicted key: " << key << std::endl;
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

void CacheNode::assignToCluster(const std::string& cluster_id, std::string& cluster_manager_ip, int& cluster_manager_port) {
    cluster_manager_details_.cluster_id_ = cluster_id;
    cluster_manager_details_.ip_=cluster_manager_ip;
    cluster_manager_details_.port_=cluster_manager_port;
    addNodeToCluster(cluster_manager_details_.ip_,cluster_manager_details_.port_);
}


void CacheNode::handle_client(int client_socket) {
    char buffer[1024] = {0};
    int retries = 1000;
    while (retries > 0) {
        int valread = read(client_socket, buffer, 1024);
        if (valread > 0) {
            // Process the data
            buffer[valread]='\0';
            break;
        } else if (valread == 0) {
            // Connection closed
            std::cerr << "[CacheNode] Client disconnected." << std::endl;
            // close(client_socket);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket temporarily unavailable, retry
                retries--;
                continue;
            } else {
                std::cerr << "[CacheNode] Failed to read from socket: " << strerror(errno) << std::endl;
                // close(client_socket);
                return;
            }
        }
    }
    if (retries == 0) {
        std::cerr << "[CacheNode] Read failed after multiple attempts." << std::endl;
        close(client_socket);
        return;
    }
    
    std::string request(buffer);
    std::string response;

    response=processRequest(request);

    ssize_t bytes_written = write(client_socket, response.c_str(), response.length());
    if (bytes_written == -1) {
        // Handle the error, e.g., log it or throw an exception
        perror("Error writing to socket");
    } else if (bytes_written < response.length()) {
        // Handle partial write if necessary
        std::cerr << "Warning: Partial write to socket." << std::endl;
    }
}

std::string CacheNode::sendToNode(const std::string& ip, int port, const std::string& request) {
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
        pos += 6; // Move past "\r\n$_\r\n"
    }
    

    // Parse the command
    if(request.substr(pos, 7)=="MIGRATE"){
        pos += 9; // Move past "MIGRATE\r\n"

        // Parse source node ID length and node ID
        size_t node_id_length_start = request.find("$", pos) + 1;
        size_t node_id_length_end = request.find("\r\n", node_id_length_start);
        int node_id_length = std::stoi(request.substr(node_id_length_start, node_id_length_end - node_id_length_start));

        pos = node_id_length_end + 2; // Move past "\r\n"
        std::string source_node_id = request.substr(pos, node_id_length);

        pos += node_id_length + 2; // Move past node ID and "\r\n"

        // Parse target nodes list
        size_t target_count_start = request.find("*", pos) + 1; // Skip the "*"
        size_t target_count_end = request.find("\r\n", target_count_start); // Find end of the line
        int target_count = std::stoi(request.substr(target_count_start, target_count_end - target_count_start)); 

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
            try {
                // Call the sendToNode method and store the return message.
                std::string response = sendToNode(target_node.first, target_node.second, set_message.str());

                // Process the response if no exception was thrown.
                std::cout << "[CacheNode] Set Message sent successfully. Response: " << response << std::endl;

            } catch (const std::exception& e) {
                // Handle the error if an exception is thrown.
                std::cerr << "[CacheNode]  Failed to send set message to node: " << e.what() << std::endl;
            }
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
        std::string value = get(key);
        // Check if the value is empty
        if (value.empty()) {
            // Return Redis-like nil response
            return "$-1\r\n";
        } else {
            // Return Redis-like bulk string with value size
            return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
        }

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
    while (!this->stopServer.load()) {
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
    while (!this->stopServer.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(ttl_+std::chrono::seconds(1))); // Adjust the interval as needed

        std::lock_guard<std::mutex> lock(mutex_);
        remove_expired(); // Call your existing remove_expired method
    }
}

void CacheNode::lazyDeleteStaleEntry() {
    while (!this->stopServer.load()) {
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

void CacheNode::addNodeToCluster(const std::string& cluster_manager_ip, int cluster_manager_port) {
    // Format: *2\r\n$3\r\nADD\r\n$<ip_length>\r\n<ip>\r\n$<port_length>\r\n<port>\r\n

    // Node's IP and Port
    std::string node_ip = this->ip_; // Assuming you have this stored in the node
    int node_port = this->port_;     // Assuming you have this stored in the node

    // Construct the message according to our custom protocol
    std::stringstream message;
    message << "*2\r\n"                   // Command count
            << "$3\r\nADD\r\n"            // Command
            << "$" << node_ip.size() << "\r\n" << node_ip << "\r\n" // IP of the node
            << "$" << std::to_string(node_port).size() << "\r\n" << node_port << "\r\n"; // Port of the node

    // Send the message to the cluster manager
    std::string response;

    try{
        response=sendToNode(cluster_manager_ip, cluster_manager_port, message.str());
    }catch (const std::runtime_error& e) {
    // Log the error and return the original error message
        std::cerr << "[CacheNode]  Error: " << e.what() << std::endl;
    }
    
    std::string expected_prefix = "+OK Node Added\r\n";
    if (response.substr(0, expected_prefix.size()) == expected_prefix){
        // Extract the UUID, which comes right after the prefix
        std::string uuid = response.substr(expected_prefix.size());

        // Output the message with the UUID
        std::cout << "[CacheNode] Node Id: "<<this->node_id_<<" added to cluster with id: " << uuid << std::endl;
        this->start_node_server();
    }
    else{
        std::cout<<"[CacheNode] Failed to add Node to cluster"<<response<<std::endl;
    }
}


     void CacheNode::shutDown_node_server(){
        this->stopServer.store(true);
        char wakeup_signal = 1;  // Any data will do
        if (write(wakeup_pipe[1], &wakeup_signal, sizeof(wakeup_signal)) == -1) {
        std::cerr << "[Cache] Failed to write to wakeup pipe: " << strerror(errno) << std::endl;
        }
    }
int setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

    bool CacheNode::isServerStopped() const {
        return stopServer.load();
    }
    void CacheNode::handleClient(int new_socket) {
        handle_client(new_socket);
    }

std::vector<int> clientSockets; // Global list of client sockets
std::mutex clientSocketsMutex; // Mutex to synchronize access to the client socket list


void handleClosedClientSockets(int kq_client, CacheNode* cache_node) {
    while (!cache_node->isServerStopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Monitor every 5 seconds

        std::lock_guard<std::mutex> lock(clientSocketsMutex); // Lock the client socket list
        for (auto it = clientSockets.begin(); it != clientSockets.end(); ) {
            int client_fd = *it;

            // Check if the client socket is still alive
            char buffer;
            int result = recv(client_fd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
            if (result == 0 || (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                // Connection closed or error
                std::cerr << "[CacheNode] Removing inactive client socket: " << client_fd << std::endl;

                // Remove from kqueue
                struct kevent event;
                EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
                    std::cerr << "[CacheNode] Failed to deregister client socket from kqueue." << std::endl;
                }

                close(client_fd); // Close the socket
                it = clientSockets.erase(it); // Remove socket from the list
            } else {
                ++it; // Move to the next socket
            }
        }
    }
}

void accept_connections(int kq, CacheNode* cache_node,int server_fd,int kq_client,int wakeup_pipe[2]) {
        struct timespec timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_nsec = 0;
    while (!cache_node->isServerStopped()) {

        struct kevent event;
        int nev = kevent(kq, NULL, 0, &event, 1, NULL);  // Wait for a connection event
        // std::cout << "[CacheNode] accept thread kevent returned with nev = " << nev << std::endl;
        if (nev == -1) {
            std::cerr << "[CacheNode] Error with kevent (accept loop): " << strerror(errno) << std::endl;
            break;
        }

        if (nev == 0) {
           
            // No events, check if stopServer is set
            if (cache_node->isServerStopped())
            {  
                break;
                }

        }
         if (nev > 0) {
            if (event.ident == wakeup_pipe[0]) {
                // Received wakeup signal, break out of loop
                std::cout << "[CacheNode] Wakeup signal received, shutting down accept loop." << std::endl;
                break;
            }
        }

        if (event.flags & EV_ERROR) {
                std::cerr << "[CacheNode] Kqueue error on fd: " << event.ident << std::endl;
                continue;
            }
        if (event.ident == server_fd ) {
            // New connection ready to accept
            int new_socket;
            struct sockaddr_in address;
            socklen_t addrlen = sizeof(address);
            
            while ((new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) >= 0) {
                if (cache_node->isServerStopped())
                { 
                    break;
                }
                if (setNonBlocking(new_socket) == -1) {
                    std::cerr << "[CacheNode] Failed to set client socket to non-blocking mode." << std::endl;
                    close(new_socket);
                } else {
                    std::cout << "[CacheNode] New connection accepted. " << std::endl;
                    // Register the new socket for read events
                    EV_SET(&event, new_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
                        std::cerr << "[CacheNode] Failed to register new_socket with kqueue." << std::endl;
                        close(new_socket);
                    }
                    else {
                        // Add new_socket to the clientSockets list in a thread-safe manner
                        std::lock_guard<std::mutex> lock(clientSocketsMutex);
                        clientSockets.push_back(new_socket);
                    }
                }
            }
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                std::cerr << "[CacheNode] Error on accept: " << strerror(errno) << std::endl;
            }
        }
    }
}

void handle_client_connections(int kq, CacheNode* cache_node,int server_fd,int wakeup_pipe[2]) {
    struct timespec timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_nsec = 0;
    struct kevent event;
    struct kevent events[10];
    while (!cache_node->isServerStopped()) {
        // std::cout << "[CacheNode] Handle connections running "<< "Process ID: " << getpid() << ", Thread ID: " << std::this_thread::get_id() << std::endl;
        int nev = kevent(kq, NULL, 0, events, 10, NULL);  // Wait for read events
        if (nev == -1) {
            std::cerr << "[CacheNode] Error with kevent (client event loop): " << strerror(errno) << std::endl;
            break;
        }
        if (nev == 0) {
            // No events, check if stopServer is set
            if (cache_node->isServerStopped()) {
                break;
                }
        }
        if (nev > 0) {
            for (int i = 0; i < nev; i++) {
                if (cache_node->isServerStopped())
                { 
                    break;
                }
                if (events[i].ident == wakeup_pipe[0]) {
                    // Received wakeup signal, break out of loop
                    std::cout << "[CacheNode] Wakeup signal received, shutting down accept loop." << std::endl;
                    break;
                }
                if (events[i].flags & EV_ERROR) {
                    std::cerr << "[CacheNode] Kqueue error on fd: " << events[i].ident << std::endl;
                    continue;
                }

                // Check if the event is a read event (EVFILT_READ) for the client socket
                if (events[i].filter == EVFILT_READ && events[i].ident!=server_fd) {
                    int client_fd = events[i].ident;
                    cache_node->handleClient(client_fd);
                }
            }
        }
    }
}

static void cleanup_kqueue(int kq) {
    struct kevent event;
    std::vector<int> fds_to_close;

    // Define an arbitrary large value to fetch events
    const int max_events = 1024;
    struct kevent events[max_events];
    
    // Get all the events currently in the kqueue
    int num_events = kevent(kq, nullptr, 0, events, max_events, nullptr);

    if (num_events < 0) {
        std::cerr << "[CacheNode] Error fetching events from kqueue." << std::endl;
        return;
    }

    for (int i = 0; i < num_events; ++i) {
        int client_fd = events[i].ident;
        
        // Add the socket to a list for cleanup
        fds_to_close.push_back(client_fd);

        // Remove it from kqueue
        EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        if (kevent(kq, &event, 1, nullptr, 0, nullptr) == -1) {
            std::cerr << "[CacheNode] Failed to remove client_fd " << client_fd << " from kqueue." << std::endl;
        }
    }

    // Now, close all the sockets that were in kqueue
    for (int client_fd : fds_to_close) {
        close(client_fd);
    }
    // Finally, close the kqueue itself
    close(kq);
    std::cout << "[CacheNode] Kqueue and all associated sockets have been cleaned up." << std::endl;
}

void CacheNode::start_node_server() {
    this->stopServer.store(false);
    int server_socket;
    int opt = 1;
    sockaddr_in server_addr{};
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        throw std::runtime_error("Socket creation failed.");
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        close(server_socket);
        throw std::runtime_error("Failed to set SO_REUSEADDR.");
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        close(server_socket);
        throw std::runtime_error("Failed to set SO_REUSEPORT.");
    } 

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(this->port_);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_socket);
        throw std::runtime_error("Failed to bind socket.");
    }

    if (listen(server_socket, 3) < 0) {
        close(server_socket);
        throw std::runtime_error("Listen failed.");
    }

    // Create two kqueues
    int kq_accept = kqueue();  // For accepting connections
        if (kq_accept == -1) {
            close(server_socket);
            throw std::runtime_error("Failed to create kqueue.");
        }
    int kq_client = kqueue();  // For handling client events
        if (kq_client == -1) {
            close(server_socket);
            throw std::runtime_error("Failed to create kqueue.");
        }
    
    struct kevent event;
    // Register server socket in kq_accept
    EV_SET(&event, server_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_accept, &event, 1, NULL, 0, NULL) == -1) {
        close(server_socket);
        throw std::runtime_error("Failed to register server_socket with kqueue.");
    }
    EV_SET(&event, server_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
        close(server_socket);
        throw std::runtime_error("Failed to register server_socket with kqueue.");
    }

   
    if (pipe(wakeup_pipe) == -1) {
        close(server_socket);
        throw std::runtime_error("Failed to create wakeup pipe.");
    } else {
    std::cout << "[CacheNode] Wakeup pipe created successfully" << std::endl;
}
    // Set both ends of the pipe to non-blocking
    setNonBlocking(wakeup_pipe[0]);
    setNonBlocking(wakeup_pipe[1]);

    // Register the read end of the wakeup pipe with kqueue
    EV_SET(&event, wakeup_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_accept, &event, 1, NULL, 0, NULL) == -1) {
        close(server_socket);
        throw std::runtime_error("Failed to register wakeup pipe with kqueue.");
    } else {
    std::cout << "[CacheNode] Wakeup pipe registered with kqueue" << std::endl;
}
EV_SET(&event, wakeup_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
        close(server_socket);
        throw std::runtime_error("Failed to register wakeup pipe with kqueue.");
    } else {
    std::cout << "[CacheNode] Wakeup pipe registered with kqueue" << std::endl;
}

    std::vector<std::thread> workerThreads;
    
    if(strategy_==EvictionStrategy::LRU){
        // Start the LRU tracking and updating thread
        std::thread lru_thread(&CacheNode::trackAndUpdateLRU, this);
        // Move it into the vector
        workerThreads.push_back(std::move(lru_thread));
    }
    if (strategy_ == EvictionStrategy::LFU) {
    // Start the LFU lazy deletion thread
    std::thread lfu_thread(&CacheNode::lazyDeleteStaleEntry, this);
    // Move it into the vector
    workerThreads.push_back(std::move(lfu_thread));
    }
    // Start the periodic expiration removal thread
    std::thread expire_thread(&CacheNode::remove_expired_periodically, this);
    // Move it into the vector
    workerThreads.push_back(std::move(expire_thread));  
    
    if (setNonBlocking(server_socket) == -1) {
        std::cerr << "[CacheNode]  Failed to set socket to non-blocking mode." << std::endl;
        close(server_socket); // Clean up the socket if the operation failed
    }
    
    std::thread acceptThread([this, kq_accept,server_socket,kq_client] {
    accept_connections(kq_accept, this,server_socket,kq_client,this->wakeup_pipe);
});
    std::thread clientEventThread([this, kq_client,server_socket] {
    handle_client_connections(kq_client,this,server_socket,this->wakeup_pipe);
});

    std::thread monitorClientSocketsThread([kq_client,this] {
        handleClosedClientSockets(kq_client,this);
    });

    std::cout << "[CacheNode] Cache Node Server is running on  "<<this->ip_<<":"<< this->port_ << std::endl;
    acceptThread.join();
    clientEventThread.join();
    monitorClientSocketsThread.join();
    std::lock_guard<std::mutex> lock(clientSocketsMutex); // Lock the client socket list
    for (auto it = clientSockets.begin(); it != clientSockets.end();){
        int socket = *it;
        // Remove from kqueue
        struct kevent event;
        EV_SET(&event, socket, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
            std::cerr << "[CacheNode] Failed to deregister client socket from kqueue." << std::endl;
        }

        close(socket); // Close the socket
        // Remove the socket from the clientSockets vector
        it = clientSockets.erase(it); // Erase returns the next iterator
    }
    close(server_socket);
    cleanup_kqueue(kq_client);
    close(kq_accept);         
    close(kq_client);
    // Join all worker threads
    for (auto& t : workerThreads) {
        if (t.joinable()) {
            t.join(); // Wait for the thread to finish
        }
    }
    std::cout<<"[CacheNode] Cache Node Server Shutting down..."<<std::endl;
}
