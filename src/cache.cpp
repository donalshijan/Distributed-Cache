#include "cache_node.h"
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
#include <fcntl.h>
#include <sys/event.h> 
#include <thread>
#include <functional>
#include <sstream>
#include <signal.h>
#include <sys/wait.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <netinet/tcp.h>
#include "connection_pool.h"
#include <chrono>
#include <map>
#include <iomanip>
#include <ctime>

std::map<std::string, std::tuple<std::string, std::chrono::steady_clock::time_point, int>> buffer_map;
std::mutex buffer_mutex; // To ensure thread safety

std::map<std::string, std::tuple<std::string, std::chrono::steady_clock::time_point, int>> unit_test_buffer_map;
std::mutex unit_test_buffer_mutex; // To ensure thread safety

std::unordered_map<std::string, int> socket_map;

Cache::Cache(const std::string& ip, const int& port) : ip_(ip), port_(port), connection_pool(){
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        cluster_id_ = boost::uuids::to_string(uuid);
    }
// Helper function to write the response data from the CURL call
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

    int setNonBlockingSocket(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

std::string Cache::getClusterId() const {
    return this->cluster_id_;
}

        // Helper function to send data over TCP/IP
std::string Cache::sendToNode(const std::string& node_id,  const std::string& request) {
        int sockfd;
        try {
            // Attempt to get an existing connection
            sockfd = connection_pool.getConnection(node_id);

            // Check if the connection is still alive
            char buffer;
            int alive_check = recv(sockfd, &buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);

            if (alive_check == 0 || (alive_check < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                // Connection is closed or in an error state, remove it from pool
                connection_pool.removeConnection(node_id);

                // Find node details
                auto nodeDetails = std::find_if(nodes_.begin(), nodes_.end(),
                                                [&node_id](const NodeConnectionDetails& node) {
                                                    return node.node_id == node_id;
                                                });

                // Add a new connection to the pool
                connection_pool.addConnection(*nodeDetails);
                sockfd = connection_pool.getConnection(node_id);  // Retrieve the new connection
            }
        } catch (const std::runtime_error& e) {
            // If connection does not exist, create it and add to pool
            auto nodeDetails = std::find_if(nodes_.begin(), nodes_.end(),[&node_id](const NodeConnectionDetails& node) {return node.node_id == node_id;});
            connection_pool.addConnection(*nodeDetails);  // Add new connection to the pool
            sockfd = connection_pool.getConnection(node_id);  // Retrieve the new connection
        }

        // setNonBlockingSocket(sockfd);

        // sockaddr_in serv_addr;
        // serv_addr.sin_family = AF_INET;
        // serv_addr.sin_port = htons(port);
        // if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        //     close(sockfd);
        //     throw std::runtime_error("Invalid address/Address not supported.");
        // }

        // if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        //     close(sockfd);
        //     throw std::runtime_error("Connection failed.");
        // }
        // // Wait for the socket to be writable (i.e., connection established)
        // fd_set write_fds;
        // FD_ZERO(&write_fds);
        // FD_SET(sockfd, &write_fds);
        // timeval timeout = {2, 0}; // 2 seconds timeout
        // if (select(sockfd + 1, nullptr, &write_fds, nullptr, &timeout) <= 0) {
        //     close(sockfd);
        //     throw std::runtime_error("Connection timed out.");
        // }

        ssize_t total_sent = 0;
        ssize_t bytes_to_send = request.size();
        const char* data = request.c_str();

        while (total_sent < bytes_to_send) {
            ssize_t sent = send(sockfd, data + total_sent, bytes_to_send - total_sent, 0);
            
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;  // Interrupted by a signal, retry
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket temporarily unavailable, wait and retry
                    usleep(1000);  // sleep briefly and retry
                    continue;
                }
                else {
                    // Some other error occurred
                    throw std::runtime_error("Send failed");
                }
            }
            total_sent += sent;
        }
        // Receive the response
        char buffer[1024] = {0};
        int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            // close(sockfd);
            throw std::runtime_error("Failed to receive response.");
        }
        // close(sockfd);
        return std::string(buffer, bytes_received);
    }

    NodeConnectionDetails& Cache::findNodeForKey(const std::string& key) {
        int key_hash = hasher_(key);
        auto it = hash_ring_.lower_bound(key_hash);

        if (it == hash_ring_.end()) {
            // Wrap around to the first node
            it = hash_ring_.begin();
        }

        return it->second;
    }

    void Cache::addNode(const NodeConnectionDetails& node_connection_details) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < virtual_nodes_count_; ++i) {
            int hash = hasher_(node_connection_details.node_id + std::to_string(i));
            hash_ring_[hash] = node_connection_details;
        }
        nodes_.push_back(node_connection_details);
    }

    void Cache::removeNode(const std::string& node_id) {

        // Find the node with the matching node_id
        auto it = std::remove_if(nodes_.begin(), nodes_.end(),
            [&node_id](const NodeConnectionDetails& node) {
                return node.node_id == node_id;
            });

        // If we found the node, handle data migration
        if (it != nodes_.end()) {
            NodeConnectionDetails node_to_remove = *it;

            for (int i = 0; i < virtual_nodes_count_; ++i) {
                int hash = hasher_(node_id + std::to_string(i));
                hash_ring_.erase(hash);
            }

            // Send migration message to the target nodes
            migrateData(node_to_remove.node_id);

            node_ids_being_deleted_.push_back(node_to_remove.node_id);

            // Erase the node from the vector
            nodes_.erase(it, nodes_.end());
        } else {
            throw std::runtime_error("Node with ID " + node_id + " not found.");
        }
    }

    void Cache::migrateData(const std::string& node_id) {
    // Find the node with the matching node_id
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
            [&node_id](const NodeConnectionDetails& node) {
                return node.node_id == node_id;
            });

        if (it != nodes_.end()) {
            // Construct the MIGRATE message
            std::stringstream migrate_message;

            // Start with the MIGRATE command
            migrate_message << "*1\r\n$7\r\nMIGRATE\r\n";

            // Add the source node ID
            migrate_message << "$" << node_id.size() << "\r\n" << node_id << "\r\n";

            // Add the number of target nodes
            migrate_message << "*";
            // migrate_message << target_node_ids.size() << "\r\n";
            migrate_message << "1" << "\r\n";

            std::stringstream target_info;
            target_info << this->ip_ << ":" << this->port_;
            std::string target_info_str = target_info.str();

            migrate_message << "$" << target_info_str.size() << "\r\n" << target_info_str << "\r\n";
            // Send the MIGRATE message to the node with node_id
            try {
                // Call the sendToNode method and store the return message.
                std::string response = sendToNode(it->node_id, migrate_message.str());

                // Process the response if no exception was thrown.
                std::cout << "[Cache] Migrate Message sent successfully. Response: " << response << std::endl;

            } catch (const std::exception& e) {
                // Handle the error if an exception is thrown.
                std::cerr << "[Cache] Failed to send Migrate message to node: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "[Cache] Error: Node with ID " << node_id << " not found in nodes list." << std::endl;
        }
    }

    int Cache::getNextNodeClockwise(int current_hash) {
    // Ensure nodes are sorted on the ring based on their hash values
    if (hash_ring_.empty()) {
        throw std::runtime_error("Hash ring is empty.");
    }

    // Find the current hash's position in the hash ring
    auto it = hash_ring_.upper_bound(current_hash);

    // If we've reached the end, wrap around to the beginning
    if (it == hash_ring_.end()) {
        it = hash_ring_.begin();
    }

    // Store the starting iterator to detect when we've wrapped around
    auto starting_position = it;

    // Iterate clockwise until we find a distinct physical node
    do {
        // Check if the current node is distinct from the starting hash
        if (it->first != current_hash) {
            return it->first;  // Return the distinct node
        }

        // Move to the next position, wrapping around if necessary
        ++it;
        if (it == hash_ring_.end()) {
            it = hash_ring_.begin();
        }
    } while (it != starting_position);  // Stop if we've wrapped around the entire ring

    return it->first;
}


    NodeConnectionDetails& Cache::getStartingNode(const std::string& request){
        // Extract key from the request string
        size_t pos = 0;

        // Skip the command count prefix, e.g., "*2\r\n"
        pos = request.find("\r\n");

        if (pos != std::string::npos) {
            pos += 6; // Move past "\r\n$_\r\n"
        }
        pos += 4; // Move past "GET\r\n"
        // Parse key length
        size_t key_length_start = request.find("$", pos) + 1;
        size_t key_length_end = request.find("\r\n", key_length_start);
        int key_length = std::stoi(request.substr(key_length_start, key_length_end - key_length_start));

        // Extract key
        pos = key_length_end + 2; // Move past "\r\n"
        std::string key = request.substr(pos, key_length);
        return findNodeForKey(key);
    }

    void Cache::addRequestToBuffer(const std::string& request) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        // Find the starting node ID for the request
        std::string starting_node_id = getStartingNode(request).node_id;
        // Check if the node's buffer exists
        auto& buffer_entry = buffer_map[starting_node_id];
        auto& buffer_string = std::get<0>(buffer_entry);
        auto& first_entry_time = std::get<1>(buffer_entry);
        auto& request_count = std::get<2>(buffer_entry);

        // Append the request to the buffer
        buffer_string += request;
        // If it's the first entry, record the time
        if (request_count == 0) {
            first_entry_time = std::chrono::steady_clock::now();
        }

        // Update the request count
        request_count++;
    }

    void Cache::addRequestToUnitTestBuffer(const std::string& request) {
        std::lock_guard<std::mutex> lock(unit_test_buffer_mutex);
        // Find the starting node ID for the request
        std::string starting_node_id = getStartingNode(request).node_id;
        // Check if the node's buffer exists
        auto& buffer_entry = unit_test_buffer_map[starting_node_id];
        auto& buffer_string = std::get<0>(buffer_entry);
        auto& first_entry_time = std::get<1>(buffer_entry);
        auto& request_count = std::get<2>(buffer_entry);

        // Append the request to the buffer
        buffer_string += request;
        // If it's the first entry, record the time
        if (request_count == 0) {
            first_entry_time = std::chrono::steady_clock::now();
        }

        // Update the request count
        request_count++;
    }

    void Cache::routeGetRequestsInBuffer(const std::string& starting_node_id, std::string buffer_string) {

            auto hash_it = std::find_if(hash_ring_.begin(), hash_ring_.end(),
                [&starting_node_id](const auto& pair) {
                    return pair.second.node_id == starting_node_id;
                });

            if (hash_it == hash_ring_.end()) {
                throw std::runtime_error("Key does not belong to any node in the routing table.");
            }

            int current_hash = hash_it->first; // Starting node hash

            int starting_hash = current_hash;
             // Find the node that holds the key
            do {
                auto current_hash_node_pair = std::find_if(hash_ring_.begin(), hash_ring_.end(),
                [&current_hash](const auto& pair) {
                    return pair.first == current_hash;
                });
                // Check if the iterator is valid
                if (current_hash_node_pair == hash_ring_.end()) {
                    throw std::runtime_error("Hash not found in hash ring.");
                }
                NodeConnectionDetails current_node = current_hash_node_pair->second;
                std::string new_buffer_string;
                // Try to fetch the key from the current node
                std::string response;
                try {
                    response = sendToNode(current_node.node_id, buffer_string);
                } catch (const std::runtime_error& e) {
                    std::cerr << "[Cache] Error: " << e.what() << " on node " << current_node.node_id << std::endl;
                }
                size_t pos = 0;
                while (pos < response.size()) {
                    // Extract the substring for this request-response pair
                    size_t next_star = response.find('*', pos + 1);
                    std::string req_resp_substr;
                    if (next_star == std::string::npos) {
                        // If this is the last pair, take from pos to the end of the string
                        req_resp_substr = response.substr(pos);
                    } else {
                        // Otherwise, take from pos to next_star
                        req_resp_substr = response.substr(pos, next_star - pos);
                    }
                    // Check for valid response
                    size_t first_hash = req_resp_substr.find('#');
                    if (first_hash != std::string::npos) {
                        size_t second_hash = req_resp_substr.find('#', first_hash + 1);
                        std::string response_value = req_resp_substr.substr(first_hash + 1, second_hash - first_hash - 1);
                        if (response_value == "$-1\r\n") {
                            // Invalid response, add to new buffer string
                            std::string original_request = req_resp_substr.substr(0, first_hash);
                            new_buffer_string += original_request;
                        } else {
                            // Valid response, send to client
                            size_t socket_start = req_resp_substr.find('{');
                            size_t socket_end = req_resp_substr.find('}', socket_start);
                            if (socket_start != std::string::npos && socket_end != std::string::npos) {
                                std::string socket_id_str = req_resp_substr.substr(socket_start + 1, socket_end - socket_start - 1);
                                auto it = socket_map.find(socket_id_str);
                                int socket_id = it->second;
                                // Send the response back to the client
                                ssize_t total_sent = 0;
                                ssize_t bytes_to_send = response_value.size();
                                const char* data = response_value.c_str();

                                while (total_sent < bytes_to_send) {
                                    ssize_t sent = send(socket_id, data + total_sent, bytes_to_send - total_sent, 0);
                                    
                                    if (sent < 0) {
                                        if (errno == EINTR) {
                                            std::cerr << "[LOG] Send interrupted by signal. Retrying..." << std::endl;
                                            continue;  // Interrupted by a signal, retry
                                        }
                                        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                            // Socket temporarily unavailable, wait and retry
                                            std::cerr << "[LOG] Socket temporarily unavailable. Retrying after brief sleep..." << std::endl;
                                            usleep(1000);  // sleep briefly and retry
                                            continue;
                                        }
                                        else {
                                            std::cerr << "Send failed: " << strerror(errno) << " (errno: " << errno << ")" << std::endl;
                                            // Some other error occurred
                                            throw std::runtime_error("Send failed");
                                        }
                                    }
                                    total_sent += sent;
                                }
                                #ifdef PRODUCTION
                                close(socket_id); // Close connection after handling
                                #endif
                            }
                        }
                    }

                    // Move to the next request-response substring
                    if (next_star == std::string::npos) {
                        break; // End of the response string
                    }
                    pos = next_star;
                }
                // Update buffer_string in buffer_map for the next iteration
                buffer_string = new_buffer_string;
                // Move to the next node in a clockwise direction on the ring
                current_hash = getNextNodeClockwise(current_hash);
            } while (!buffer_string.empty() && current_hash != starting_hash);  // Continue until we return to the starting node
            // Handle remaining requests in the buffer
            size_t pos = 0;
            while (pos < buffer_string.size()) {
                size_t next_star = buffer_string.find('*', pos + 1);
                std::string req_resp_substr;
                if (next_star == std::string::npos) {
                    req_resp_substr = buffer_string.substr(pos);
                } else {
                    req_resp_substr = buffer_string.substr(pos, next_star - pos);
                }

                size_t socket_start = req_resp_substr.find('{');
                size_t socket_end = req_resp_substr.find('}', socket_start);
                if (socket_start != std::string::npos && socket_end != std::string::npos) {
                    std::string socket_id_str = req_resp_substr.substr(socket_start + 1, socket_end - socket_start - 1);
                    int socket_id = std::stoi(socket_id_str);

                    std::string error_message = "-ERR: Key does not belong to any node in the routing table.";
                    send(socket_id, error_message.c_str(), error_message.size(), 0);
                    #ifdef PRODUCTION
                    close(socket_id);
                    #endif
                }

                if (next_star == std::string::npos) {
                    break;
                }
                pos = next_star;
            }
    }

    std::string Cache::routeGetRequest(const std::string& request) {
        addRequestToUnitTestBuffer(request);
        std::lock_guard<std::mutex> lock(unit_test_buffer_mutex);
        std::string starting_node_id = getStartingNode(request).node_id;
        std::string responseToReturn = "";
        auto it = unit_test_buffer_map.find(starting_node_id);
        if (it != unit_test_buffer_map.end()) {
            auto& [buffer_string, first_entry_time, request_count] = it->second;
            // Locate the NodeConnectionDetails pointer in the nodes_ vector

            // Find the hash value of the starting node
            auto hash_it = std::find_if(hash_ring_.begin(), hash_ring_.end(),
                [&starting_node_id](const auto& pair) {
                    return pair.second.node_id == starting_node_id;
                });

            if (hash_it == hash_ring_.end()) {
                throw std::runtime_error("Key does not belong to any node in the routing table.");
            }

            int current_hash = hash_it->first; // Starting node hash

            int starting_hash = current_hash;
            // std::cout<<"starting search for buffer request"<<std::endl;
             // Find the node that holds the key
            do {
                auto current_hash_node_pair = std::find_if(hash_ring_.begin(), hash_ring_.end(),
                [&current_hash](const auto& pair) {
                    return pair.first == current_hash;
                });
                // Check if the iterator is valid
                if (current_hash_node_pair == hash_ring_.end()) {
                    throw std::runtime_error("Hash not found in hash ring.");
                }
                NodeConnectionDetails current_node = current_hash_node_pair->second;
                std::string new_buffer_string;
                // Try to fetch the key from the current node
                std::string response;
                try {
                    response = sendToNode(current_node.node_id, buffer_string);
                } catch (const std::runtime_error& e) {
                    std::cerr << "[Cache] Error: " << e.what() << " on node " << current_node.node_id << std::endl;
                    // return "Error: " + std::string(e.what());
                }
                size_t pos = 0;
                while (pos < response.size()) {
                    // Extract the substring for this request-response pair
                    size_t next_star = response.find('*', pos + 1);
                    std::string req_resp_substr;
                    if (next_star == std::string::npos) {
                        // If this is the last pair, take from pos to the end of the string
                        req_resp_substr = response.substr(pos);
                    } else {
                        // Otherwise, take from pos to next_star
                        req_resp_substr = response.substr(pos, next_star - pos);
                    }
                    // Check for valid response
                    size_t first_hash = req_resp_substr.find('#');
                    if (first_hash != std::string::npos) {
                        size_t second_hash = req_resp_substr.find('#', first_hash + 1);
                        std::string response_value = req_resp_substr.substr(first_hash + 1, second_hash - first_hash - 1);
                        if (response_value == "$-1\r\n") {
                            // Invalid response, add to new buffer string
                            std::string original_request = req_resp_substr.substr(0, first_hash);
                            new_buffer_string += original_request;
                        } else {
                            responseToReturn += response_value;
                        }
                    }

                    // Move to the next request-response substring
                    if (next_star == std::string::npos) {
                        break; // End of the response string
                    }
                    pos = next_star;
                }
                // Update buffer_string in buffer_map for the next iteration
                buffer_string = new_buffer_string;
                // Move to the next node in a clockwise direction on the ring
                current_hash = getNextNodeClockwise(current_hash);
            } while (!buffer_string.empty() && current_hash != starting_hash);  // Continue until we return to the starting node
            // std::cout<<"GOTOUT"<<std::endl;
            // Handle remaining requests in the buffer
            if(!buffer_string.empty()){
                throw std::runtime_error("Key does not belong to any node in the routing table.");
            }

            // Reset the buffer
            buffer_string.clear();
            first_entry_time = std::chrono::steady_clock::now(); 
            request_count = 0;
        }
        return responseToReturn;
    }

    std::string Cache::routeSetRequest(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t num_nodes = nodes_.size();
        if (num_nodes == 0) {
            std::cerr << "[Cache] No nodes available to route request." << std::endl;
            return "Failed:No nodes available to route request.";
        }

        size_t pos = 0;

        // Skip the command count prefix, e.g., "*2\r\n"
        pos = request.find("\r\n");

        if (pos != std::string::npos) {
            pos += 6; // Move past "\r\n$_\r\n"
        }
        pos += 4; // Move past "GET\r\n"
        // Parse key length
        size_t key_length_start = request.find("$", pos) + 1;
        size_t key_length_end = request.find("\r\n", key_length_start);
        int key_length = std::stoi(request.substr(key_length_start, key_length_end - key_length_start));

        // Extract key
        pos = key_length_end + 2; // Move past "\r\n"
        std::string key = request.substr(pos, key_length);
        NodeConnectionDetails& node = findNodeForKey(key);
        std::string response;

        try {
            response = sendToNode(node.node_id, request);
        } catch (const std::runtime_error& e) {
            std::cerr << "[Cache] Error: " << e.what() << std::endl;
            return std::string("Error: ") + e.what();
        }
        return response;
    }

    std::string Cache::get(const std::string& key){        
        // Ensure the key is not empty
        if (key.empty()) {
            std::cerr << "[Cache] Key is empty. Cannot route request." << std::endl;
            throw std::runtime_error("Key is Empty");
        }

        // Construct the GET request according to the protocol
        std::string request = "*2\r\n$3\r\nGET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";

        std::string response;
         try {
        // Attempt to route the request and get the response
        response = routeGetRequest(request);
        
        } catch (const std::runtime_error& e) {
        // Log the error and return the original error message
            std::cerr << "[Cache] Error: " << e.what() << std::endl;
            return std::string("-ERR: ") + e.what();
        }
        return response;
    }
    std::string Cache::set(const std::string& key, const std::string& value) {
        // Ensure the key and value are not empty
        if (key.empty() || value.empty()) {
            std::cerr << "[Cache] Key or value is empty. Cannot route request." << std::endl;
            return "Failed:Key or value is empty. Cannot route request.";
        }

        // Construct the SET request according to the protocol
        std::string request = "*3\r\n$3\r\nSET\r\n";
        request += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
        request += "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";

        return routeSetRequest(request);
    }

    void Cache::shutDownCacheServer(){
        this->stopServer.store(true);
        char wakeup_signal = 1;  // Any data will do
        if (write(wakeup_pipe[1], &wakeup_signal, sizeof(wakeup_signal)) == -1) {
            std::cerr << "[Cache] Failed to write to wakeup pipe: " << strerror(errno) << std::endl;
        }
    }
    
bool Cache::isServerStopped() const {
        return stopServer.load();
    }
    void Cache::handleClient(int new_socket) {
        handle_client(new_socket);
    }


void send_fd(int unix_socket, int fd_to_send) {
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(fd_to_send))];
    memset(buf, 0, sizeof(buf));

    struct iovec io = { .iov_base = (void*) "FD", .iov_len = 2 };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    struct cmsghdr *cmsg = (struct cmsghdr *) buf;
    msg.msg_control = cmsg;
    msg.msg_controllen = CMSG_LEN(sizeof(fd_to_send));

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd_to_send));

    *((int *) CMSG_DATA(cmsg)) = fd_to_send;

    if (sendmsg(unix_socket, &msg, 0) < 0) {
        throw std::runtime_error("Failed to send file descriptor");
    }
}

int recv_fd(int unix_socket) {
    struct msghdr msg = {0};
    char m_buffer[256];
    struct iovec io = { .iov_base = m_buffer, .iov_len = sizeof(m_buffer) };

    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(unix_socket, &msg, 0) < 0) {
        throw std::runtime_error("Failed to receive file descriptor");
    }

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        int fd;
        std::memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
        return fd;  // Return the received FD
    }

    return -1;
}

void accept_connections(Cache* cache, int kq_client, std::vector<int> unix_sockets) {
    struct timespec timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_nsec = 0;
    struct kevent event;

    while (!cache->isServerStopped()) {
        fd_set readfds;
        struct timeval timeout;

        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;

        FD_ZERO(&readfds);

        // Add each socket in unix_sockets to the readfds set
        int max_fd = -1;
        for (int unix_socket : unix_sockets) {
            FD_SET(unix_socket, &readfds);
            if (unix_socket > max_fd) {
                max_fd = unix_socket;
            }
        }

        // Wait for any socket to be ready for reading
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity > 0) {
            // Iterate over each socket to check if it’s ready
            for (int unix_socket : unix_sockets) {
                if (FD_ISSET(unix_socket, &readfds)) {
                    // Receive a new connection from the listener process
                    int new_socket = recv_fd(unix_socket);
                    if (new_socket != -1) {
                        EV_SET(&event, new_socket, EVFILT_READ, EV_ADD | EV_CLEAR | EV_ENABLE, 0, 0, NULL);
                        if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
                            std::cerr << "[Cache] Failed to register new_socket with kqueue." << std::endl;
                            close(new_socket);
                        } else {
                            std::cout << "[Cache] New connection accepted." << std::endl;
                        }
                    }
                }
            }
        }
    }
}


void handle_client_connections(int kq, Cache* cache,int wakeup_pipe[2]) {
    struct timespec timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_nsec = 0;
    struct kevent event;
    struct kevent events[10];
    while (!cache->isServerStopped()) {

        // Check for buffers ready to process
        {
            std::unique_lock<std::mutex> lock(buffer_mutex);
            auto now = std::chrono::steady_clock::now();

            for (auto it = buffer_map.begin(); it != buffer_map.end();) {
                auto current_entry = *it;
                auto& [starting_node_id, buffer_tuple] = current_entry;
                auto& [buffer_string, first_entry_time, request_count] = buffer_tuple;
                // std::cout<<"request_count"<<request_count<<std::endl;
                // std::cout<<"duration "<<std::chrono::duration_cast<std::chrono::microseconds>(now - first_entry_time).count()<<std::endl;
                // Process if buffer conditions are met
                if (request_count >= 0 || 
                    (std::chrono::duration_cast<std::chrono::microseconds>(now - first_entry_time).count() >= 0 && request_count>0)) {
                    it = buffer_map.erase(it);
                    lock.unlock();
                    cache->routeGetRequestsInBuffer(starting_node_id,buffer_string);
                    lock.lock();
                }
                 else {
                    ++it; // Only increment if the current entry is not erased
                }
            }
        }

        int nev = kevent(kq, NULL, 0, events, 10, NULL);  // Wait for read events
        if (nev == -1) {
            std::cerr << "[Cache] Error with kevent (client event loop): " << strerror(errno) << std::endl;
            break;
        }
        if (nev == 0) {
            // No events, check if stopServer is set
            if (cache->isServerStopped()) {
                break;
                }
        }
        if (nev > 0) {
            for (int i = 0; i < nev; i++) {
                if (cache->isServerStopped())
                { 
                    break;
                }
                if (events[i].ident == wakeup_pipe[0]) {
                    // Received wakeup signal, break out of loop
                    std::cout << "[Cache] Wakeup signal received, shutting down accept loop." << std::endl;
                    break;
                }
                if (events[i].flags & EV_ERROR) {
                    std::cerr << "[Cache] Kqueue error on fd: " << events[i].ident << std::endl;
                    continue;
                }

                // Check if the event is a read event (EVFILT_READ) for the client socket
                if (events[i].filter == EVFILT_READ) {
                    int client_fd = events[i].ident;
                    cache->handleClient(client_fd);
                    #ifdef PRODUCTION
                    std::cout << "[Cache] Client connection closed." << std::endl;
                    // Remove client socket from kqueue
                    EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                    kevent(kq, &event, 1, NULL, 0, NULL);
                    #endif
                }
            }
        }
        
    }
}

// Global variable to track child processes
static std::vector<pid_t> child_pids;

// Signal handler to terminate child processes
static void handle_signal(int sig) {
    std::cerr << "[Cache] Received signal " << sig << ", shutting down..." << std::endl;
    
    // Terminate all child processes
    for (pid_t pid : child_pids) {
        if (pid > 0) {
            kill(pid, SIGTERM);  // Send SIGTERM to each child process
        }
    }
    
    // Exit after terminating children
    exit(0);
}

int get_core_count() {
    int count;
    size_t len = sizeof(count);
     if (sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0) != 0) {
        perror("sysctlbyname failed");
        return -1;
    }
    return count;
}

void set_affinity(int core_id) {
    // Get the number of CPU cores and ensure core_id is valid
    int core_count = get_core_count();
    if (core_id < 0 || core_id >= core_count) {
        fprintf(stderr, "Invalid core_id: %d. Must be between 0 and %d.\n", core_id, core_count - 1);
        return;
    }
    
    thread_affinity_policy_data_t policy = { core_id };
    thread_port_t thread = pthread_mach_thread_np(pthread_self());

    kern_return_t result = thread_policy_set(thread, THREAD_AFFINITY_POLICY,(thread_policy_t)&policy, 1);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "Error setting thread affinity: %d\n", result);
    } else {
        printf("Thread affinity set to core %d\n", core_id);
    }
}

int setup_listener(std::string ip, int port){
    int listener_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create a socket
    if ((listener_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        throw std::runtime_error("Socket creation failed.");
    }

    // Allow the port to be reused
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        close(listener_fd);
        throw std::runtime_error("Failed to set SO_REUSEADDR.");
    }
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        close(listener_fd);
        throw std::runtime_error("Failed to set SO_REUSEPORT.");
    }

    // Bind the socket to the IP and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip.c_str());  // Bind to the IP provided
    address.sin_port = htons(port);

    if (bind(listener_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(listener_fd);
        throw std::runtime_error("Failed to bind socket.");
    }

    // Start listening for incoming connections
    if (listen(listener_fd, 3) < 0) {
        close(listener_fd);
        throw std::runtime_error("Listen failed.");
    }

    // Set server socket to non-blocking mode
    if (setNonBlockingSocket(listener_fd) == -1) {
        std::cerr << "[Cache] Failed to set socket to non-blocking mode." << std::endl;
        close(listener_fd);
        return -1;
    }
    return listener_fd;
}

void listener_process(int unix_socket, std::string ip, int port, int core_id) {

    // set_affinity(core_id);
    // Set up socket, bind, listen, accept, etc.
    int listener_fd = setup_listener(ip,port);
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    if( listener_fd==-1){
        throw std::runtime_error("[Cache] Listener setup failed");
    }
    std::cout<<"[Cache] listener starting..."<<std::endl;
    while (true) {
        int new_socket = accept(listener_fd, (struct sockaddr*)&address, &addrlen);
            // Enable TCP keep-alive

        int keepalive = 1; // Enable keepalive
        int keepidle = 300; // 300 seconds (5 minutes) before sending keepalive probes
        int keepintvl = 60; // 60 seconds between keepalive probes
        int keepcnt = 5; // Number of probes before dropping the connection
        setsockopt(new_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle, sizeof(keepidle)); // Note: macOS uses TCP_KEEPALIVE instead of TCP_KEEPIDLE
        setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
        setsockopt(new_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
        if (new_socket < 0) {
            if (errno == EINTR) {
                // The accept call was interrupted by a signal; retry.
                // std::cerr << "accept was interrupted, retrying...\n";
                close(new_socket);
                continue;
            } else {
                // Some other error occurred
                // std::cerr << "Error on accept: " << strerror(errno) << std::endl;
                close(new_socket);
                continue; // Optionally, you could break if you want to terminate.
            }
    }
    setNonBlockingSocket(new_socket);
    // printf("Connection accepted by process ID: %d\n", getpid());
     // Send the new socket FD to the other process
    send_fd(unix_socket, new_socket);
    }
    close(listener_fd);
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
        std::cerr << "[Cache] Error fetching events from kqueue." << std::endl;
        return;
    }

    for (int i = 0; i < num_events; ++i) {
        int client_fd = events[i].ident;
        
        // Add the socket to a list for cleanup
        fds_to_close.push_back(client_fd);

        // Remove it from kqueue
        EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        if (kevent(kq, &event, 1, nullptr, 0, nullptr) == -1) {
            std::cerr << "[Cache] Failed to remove client_fd " << client_fd << " from kqueue." << std::endl;
        }
    }

    // Now, close all the sockets that were in kqueue
    for (int client_fd : fds_to_close) {
        close(client_fd);
    }
    // Finally, close the kqueue itself
    close(kq);
    std::cout << "[Cache] Kqueue and all associated sockets have been cleaned up." << std::endl;
}

void Cache::startCacheServer(){
    #ifdef PRODUCTION
    std::cout<<"Production Mode enabled"<<std::endl;
    #else 
    std::cout<<"Testing Mode Enabled"<<std::endl;
    #endif
    this->stopServer.store(false);
    std::vector<int> server_read_sockets;
    // int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    int num_cores = 1;
    bool is_parent = false;

    // Set up signal handling for graceful shutdown
    struct sigaction sa;
    sa.sa_handler = handle_signal;  // Specify the handler function
    sigemptyset(&sa.sa_mask);       // Block no additional signals
    sa.sa_flags = 0;                // No special flags
    sigaction(SIGTERM, &sa, NULL);  // Handle SIGTERM
    sigaction(SIGINT, &sa, NULL);   // Handle SIGINT (Ctrl+C)
    sigaction(SIGKILL, &sa, NULL); 
            
    // Fork or use separate processes for multiple listeners
    for (int i = 0; i < num_cores; i++) {
        int unix_socket[2];
        // Create a Unix domain socket pair
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, unix_socket) == -1) {
            perror("socketpair");
            throw std::runtime_error("Failed to create unix domain socket.");
        }
        setNonBlockingSocket(unix_socket[0]);
        setNonBlockingSocket(unix_socket[1]);
        // Store the read end of each socket for the server
        server_read_sockets.push_back(unix_socket[0]);
        pid_t pid = fork();
        if (pid == 0) {
            listener_process(unix_socket[1],this->ip_,this->port_,i);  // Use different ports
            close(unix_socket[1]);
            exit(0);
        }
        else if (pid > 0) {
            // In parent process
            child_pids.push_back(pid);  // Track child PID in parent
            
        } else {
            // Fork failed
            std::cerr << "[Cache] Failed to fork process." << std::endl;
        }
    }

    // Run the server process in the main process
        is_parent = true;
        this->startCacheServer_(server_read_sockets);
        std::cout<<"[Cache] Server stopped"<<std::endl;
        for(int unix_socket : server_read_sockets){
            close(unix_socket);
        }
        // After the server finishes, send kill signal to child processes
        for (pid_t child_pid : child_pids) {
            // Check if the child process is still running
            if (kill(child_pid, 0) == 0) {
                kill(child_pid, SIGTERM);  // Send termination signal
            }  
        }
        // Parent process waits for child processes to terminate
        while (true) {
            pid_t pid = wait(NULL);
            if (pid == -1) {
                break;  // Break when no more child processes
            }
        }
        std::cout << "[Cache] All Child Listener processes terminated" << std::endl;
        std::cout<<"[Cache] Server Shutdown completed."<<std::endl;
}

void Cache::startCacheServer_(std::vector<int> unix_sockets) {

    int kq_client = kqueue();  // For handling client events
        if (kq_client == -1) {
            throw std::runtime_error("Failed to create kqueue.");
        }

    struct kevent event;

   
    if (pipe(wakeup_pipe) == -1) {
        throw std::runtime_error("Failed to create wakeup pipe.");
    } else {
    std::cout << "[Cache] Wakeup pipe created successfully" << std::endl;
}
    // Set both ends of the pipe to non-blocking
    setNonBlockingSocket(wakeup_pipe[0]);
    setNonBlockingSocket(wakeup_pipe[1]);

    // Register the read end of the wakeup pipe with kqueue
EV_SET(&event, wakeup_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
        throw std::runtime_error("Failed to register wakeup pipe with kqueue.");
    } else {
    std::cout << "[Cache] Wakeup pipe registered with kqueue" << std::endl;
}
        std::thread acceptThread([this,kq_client,unix_sockets] {
    accept_connections(this,kq_client,unix_sockets);
});
    std::thread clientEventThread([this, kq_client] {
    handle_client_connections(kq_client,this,this->wakeup_pipe);
});
    std::cout<<"[Cache] Cluster Manager is Active for Cluster ID: "<<this->cluster_id_<< std::endl;
    std::cout << "[Cache] Cache server started and listening on " << ip_ << ":" << port_ << std::endl;

// Join threads when shutting down
    acceptThread.join();
    clientEventThread.join();
    cleanup_kqueue(kq_client);
    close(kq_client); 
    std::cout << "[Cache] Server Shutting down..." << std::endl;
}



void Cache::handle_client(int new_socket){
    char buffer[1024] = {0};
    int retries = 1000;
    while (retries > 0) {
        int valread = read(new_socket, buffer, 1024);
        if (valread > 0) {
            // Process the data
            buffer[valread]='\0';
            break;
        } else if (valread == 0) {
            // Connection closed
            std::cerr << "[Cache] Client disconnected." << std::endl;
            close(new_socket);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket temporarily unavailable, retry
                retries--;
                continue;
            } else {
                std::cerr << "[Cache] Failed to read from socket: " << strerror(errno) << std::endl;
                close(new_socket);
                return;
            }
        }
    }
    if (retries == 0) {
        std::cerr << "[Cache] Read failed after multiple attempts." << std::endl;
        close(new_socket);
        return;
    }

        std::string request(buffer);
        std::string response;
         // Check if the request is a PING request
        if (request.substr(0, 4) == "PING") {
            // Send a PONG response back to the client
            std::string response = "PONG";
            send(new_socket, response.c_str(), response.size(), 0);
        }
        // std::cout<<"Request:"<<request<<std::endl;
        size_t pos = 0;
        pos = request.find("\r\n");

        if (pos != std::string::npos) {
            pos += 6; // Move past "\r\n$_\r\n"
        }
        // Process the request (simple GET and SET handling)
        try{
            std::string s = request.substr(pos, 3);
        }
        catch(const std::exception& e) {
            std::cout << "Caught a standard exception: " << e.what() << std::endl;
            close(new_socket);
            return;
        }
        if (request.substr(pos, 3) == "GET") {
            request += "{";
            request += std::to_string(new_socket);
            request += "}";
            request += "\r\n";
            std::string socket_id_str= std::to_string(new_socket);
            socket_map[socket_id_str] = new_socket;
            addRequestToBuffer(request);
            return; // Don't process immediately

        } else if (request.substr(pos, 3) == "SET") {

            std::string result = routeSetRequest(request);
            if (result == "Failed:No nodes available to route request.") {
                response = "-ERR No nodes available to route request.\r\n";
            } else if (result == "Failed:All nodes are currently being deleted or no nodes available.") {
                response = "-ERR All nodes are currently being deleted or no nodes available.\r\n";
            }
            else {
                // If it's not a failure, respond with OK
                response = "+OK\r\n";
            }
        } 
        else if (request.substr(pos, 3) == "ADD") {
            // Handle the "ADD" case to add a new node to the cluster

            // Move past "ADD\r\n"
            pos += 5;
            
            // Extract the IP address
            size_t ip_length_start = request.find("$", pos) + 1;
            size_t ip_length_end = request.find("\r\n", ip_length_start);
            int ip_length = std::stoi(request.substr(ip_length_start, ip_length_end - ip_length_start));

            pos = ip_length_end + 2; // Move past "\r\n"
            std::string node_ip = request.substr(pos, ip_length);

            pos += ip_length + 2; // Move past IP and "\r\n"

            // Extract the port
            size_t port_length_start = request.find("$", pos) + 1;
            size_t port_length_end = request.find("\r\n", port_length_start);
            int port_length = std::stoi(request.substr(port_length_start, port_length_end - port_length_start));

            pos = port_length_end + 2; // Move past "\r\n"
            int node_port = std::stoi(request.substr(pos, port_length));
            boost::uuids::uuid uuid = boost::uuids::random_generator()();
            std::string node_id = boost::uuids::to_string(uuid);
            NodeConnectionDetails node{node_id, node_ip, node_port};
            // Call the addNode method with the extracted IP and port
            addNode(node);
            response = "+OK Node Added\r\n";  // Response indicating the node was successfully added
            response += this->cluster_id_;
        }
        else {
            response = "-ERROR Unknown command\r\n";
        }

        // Send the response back to the client
        ssize_t total_sent = 0;
        ssize_t bytes_to_send = response.size();
        const char* data = response.c_str();

        while (total_sent < bytes_to_send) {
            ssize_t sent = send(new_socket, data + total_sent, bytes_to_send - total_sent, 0);
            
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;  // Interrupted by a signal, retry
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket temporarily unavailable, wait and retry
                    usleep(1000);  // sleep briefly and retry
                    continue;
                }
                else {
                    // Some other error occurred
                    throw std::runtime_error("Send failed");
                }
            }
            total_sent += sent;
        }
        #ifdef PRODUCTION
        close(new_socket); // Close connection after handling
        #endif
}

