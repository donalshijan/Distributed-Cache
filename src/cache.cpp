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



Cache::Cache(const std::string& ip, const int& port) : ip_(ip), port_(port) {
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

// std::string Cache::getPublicIp() {
//     CURL* curl;
//     CURLcode res;
//     std::string readBuffer;

//     curl = curl_easy_init();
//     if(curl) {
//         // Use an external service to get the public IP
//         curl_easy_setopt(curl, CURLOPT_URL, "https://ifconfig.me");
//         curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//         curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

//         res = curl_easy_perform(curl);
//         if(res != CURLE_OK) {
//             std::cerr << "Failed to fetch public IP: " << curl_easy_strerror(res) << std::endl;
//         }
//         curl_easy_cleanup(curl);
//     }

//     // Return the public IP address as a string
//     return readBuffer;
// }

std::string Cache::getClusterId() const {
    return this->cluster_id_;
}

        // Helper function to send data over TCP/IP
std::string Cache::sendToNode(const std::string& ip, int port, const std::string& request) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed.");
        }

        // setNonBlockingSocket(sockfd);

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
            close(sockfd);
            throw std::runtime_error("Failed to receive response.");
        }
        close(sockfd);
        return std::string(buffer, bytes_received);
    }

    void Cache::addNode(const NodeConnectionDetails& node_connection_details) {
        std::lock_guard<std::mutex> lock(mutex_);
        //after migration completes
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

            // Collect node IDs to migrate data to
            std::vector<std::string> target_node_ids;
            for (const auto& node : nodes_) {
                if (node.node_id != node_id) {
                    target_node_ids.push_back(node.node_id);
                }
            }

            // Send migration message to the target nodes
            migrateData(node_to_remove.node_id, target_node_ids);

            node_id_being_deleted_ = node_to_remove.node_id;

            // Erase the node from the vector
            nodes_.erase(it, nodes_.end());
        } else {
            throw std::runtime_error("Node with ID " + node_id + " not found.");
        }
    }

    void Cache::migrateData(const std::string& node_id, const std::vector<std::string>& target_node_ids) {
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
            migrate_message << target_node_ids.size() << "\r\n";

            // Add each target node's IP and port
            for (const auto& target_node_id : target_node_ids) {
                auto target_it = std::find_if(nodes_.begin(), nodes_.end(),
                    [&target_node_id](const NodeConnectionDetails& node) {
                        return node.node_id == target_node_id;
                    });

                if (target_it != nodes_.end()) {
                    std::stringstream target_info;
                    target_info << target_it->ip << ":" << target_it->port;
                    std::string target_info_str = target_info.str();

                    migrate_message << "$" << target_info_str.size() << "\r\n" << target_info_str << "\r\n";
                } else {
                    std::cerr << "[Cache] Warning: Target node with ID " << target_node_id << " not found in nodes list." << std::endl;
                }
            }
            // Send the MIGRATE message to the node with node_id
            try {
                // Call the sendToNode method and store the return message.
                std::string response = sendToNode(it->ip, it->port, migrate_message.str());

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

    std::string Cache::routeGetRequest(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Extract key from the request string
        size_t key_start = request.find("\r\n", 4) + 2;
        std::string key = request.substr(key_start, request.find("\r\n", key_start) - key_start);

        // Find the node that holds the key
        for (const auto& node : nodes_) {
                std::string response; 
                try{
                    response=sendToNode(node.ip, node.port, request);
                }catch (const std::runtime_error& e) {
                    std::cerr << "[Cache] Error: " << e.what() << std::endl;
                    return std::string("Error: ") + e.what();
                }
                if (!response.empty() && response[0] == '$') { // Assuming a valid response starts with '$'
                    if(response!="$-1\r\n"){
                        return response;
                    }   
                }
        }

        throw std::runtime_error("Key does not belong to any node in the routing table.");
    }

    std::string Cache::routeSetRequest(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t num_nodes = nodes_.size();
        if (num_nodes == 0) {
            std::cerr << "[Cache] No nodes available to route request." << std::endl;
            return "Failed:No nodes available to route request.";
        }

        // Find the next node that is not being deleted
        size_t start_node = next_node_;
        do {
            const auto& node = nodes_[next_node_];
            if (node.node_id != node_id_being_deleted_) {
                // Valid node, proceed with sending request
                std::string response;
                try{
                    response=sendToNode(node.ip, node.port, request);
                }catch (const std::runtime_error& e) {
                    std::cerr << "[Cache] Error: " << e.what() << std::endl;
                    return std::string("Error: ") + e.what();
                }
                
                next_node_ = (next_node_ + 1) % num_nodes;
                return response; 
            }
            // Skip to the next node
            next_node_ = (next_node_ + 1) % num_nodes;
        } while (next_node_ != start_node);

        std::cerr << "[Cache] All nodes are currently being deleted or no nodes available." << std::endl;
        return "Failed:All nodes are currently being deleted or no nodes available.";
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

void Cache::handleClientWorker(int kq, struct kevent &event) {
    while (!this->stopServer.load()) {
        int client_fd;

        // std::stringstream ss;
        // ss << std::this_thread::get_id();
        // std::string thread_id = ss.str();

        // Wait for a client connection to process
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return !client_queue.empty() || this->stopServer.load(); });
            if (this->stopServer.load()) {
                return; // Exit the worker thread gracefully
            }
            if (!client_queue.empty() && !this->stopServer.load()) {
                client_fd = client_queue.front();
                client_queue.pop();
            }
        }
        // Handle the client
        handle_client(client_fd);
    }
}
    void Cache::shutDownCacheServer(){
        this->stopServer.store(true);
        char wakeup_signal = 1;  // Any data will do
        if (write(wakeup_pipe[1], &wakeup_signal, sizeof(wakeup_signal)) == -1) {
        std::cerr << "[Cache] Failed to write to wakeup pipe: " << strerror(errno) << std::endl;
    }
        queue_cv.notify_all();
    }
    
bool Cache::isServerStopped() const {
        return stopServer.load();
    }
    void Cache::handleClient(int new_socket) {
        handle_client(new_socket);
    }
    std::queue<int>& Cache::getClientQueue(){
        return client_queue;
    }
    std::mutex& Cache::getQueueMutex(){
        return queue_mutex;
    }
    std::condition_variable& Cache::getQueueConditionVariable(){
        return queue_cv;
    }



void accept_connections(int kq, Cache* cache,int server_fd,int kq_client,int wakeup_pipe[2]) {
        struct timespec timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_nsec = 0;
    while (!cache->isServerStopped()) {

        struct kevent event;
        int nev = kevent(kq, NULL, 0, &event, 1, NULL);  // Wait for a connection event
        // std::cout << "[Cache] accept thread kevent returned with nev = " << nev << std::endl;
        if (nev == -1) {
            std::cerr << "[Cache] Error with kevent (accept loop): " << strerror(errno) << std::endl;
            break;
        }

        if (nev == 0) {
           
            // No events, check if stopServer is set
            if (cache->isServerStopped())
            {  
                break;
                }

        }
         if (nev > 0) {
            if (event.ident == wakeup_pipe[0]) {
                // Received wakeup signal, break out of loop
                std::cout << "[Cache] Wakeup signal received, shutting down accept loop." << std::endl;
                break;
            }
        }

        if (event.flags & EV_ERROR) {
                std::cerr << "[Cache] Kqueue error on fd: " << event.ident << std::endl;
                continue;
            }
        if (event.ident == server_fd ) {
            // New connection ready to accept
            int new_socket;
            struct sockaddr_in address;
            socklen_t addrlen = sizeof(address);
            
            while ((new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) >= 0) {
                if (cache->isServerStopped())
                { 
                    break;
                }
                if (setNonBlockingSocket(new_socket) == -1) {
                    std::cerr << "[Cache] Failed to set client socket to non-blocking mode." << std::endl;
                    close(new_socket);
                } else {
                    std::cout << "[Cache] New connection accepted." << std::endl;

                    // Register the new socket for read events
                    EV_SET(&event, new_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
                        std::cerr << "[Cache] Failed to register new_socket with kqueue." << std::endl;
                        close(new_socket);
                    }
                }
            }
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                std::cerr << "[Cache] Error on accept: " << strerror(errno) << std::endl;
            }
        }
    }
}

void handle_client_connections(int kq, Cache* cache,int server_fd,int wakeup_pipe[2]) {
    struct timespec timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_nsec = 0;
    while (!cache->isServerStopped()) {

        struct kevent event;
        struct kevent events[10];
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
            if (event.ident == wakeup_pipe[0]) {
                // Received wakeup signal, break out of loop
                std::cout << "[Cache] Wakeup signal received, shutting down accept loop." << std::endl;
                break;
            }
        }
        for (int i = 0; i < nev; i++) {
            if (cache->isServerStopped())
            { 
                break;
            }
            if (events[i].flags & EV_ERROR) {
                std::cerr << "[Cache] Kqueue error on fd: " << events[i].ident << std::endl;
                continue;
            }

            // Check if the event is a read event (EVFILT_READ) for the client socket
            if (events[i].filter == EVFILT_READ && events[i].ident!=server_fd) {
                int client_fd = events[i].ident;
                cache->handleClient(client_fd);
                std::cout << "[Cache] Client connection closed." << std::endl;
                // Remove client socket from kqueue
                EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                kevent(kq, &event, 1, NULL, 0, NULL);

                  // Data ready to read on a client socket
                // int client_fd = events[i].ident;
                // {
                //     std::lock_guard<std::mutex> lock(cache->getQueueMutex());
                //     cache->getClientQueue().push(client_fd);
                //     // Remove client socket from kqueue
                //     EV_SET(&event, client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                //     kevent(kq, &event, 1, NULL, 0, NULL);
                // }
                // cache->getQueueConditionVariable().notify_all();
            }
            }
    }
}

void Cache::startCacheServer() {
    this->stopServer.store(false);
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create a socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        throw std::runtime_error("Socket creation failed.");
    }

    // Allow the port to be reused
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        close(server_fd);
        throw std::runtime_error("Failed to set SO_REUSEADDR.");
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        close(server_fd);
        throw std::runtime_error("Failed to set SO_REUSEPORT.");
    }

    // Bind the socket to the IP and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip_.c_str());  // Bind to the IP provided
    address.sin_port = htons(port_);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket.");
    }

    // Start listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        close(server_fd);
        throw std::runtime_error("Listen failed.");
    }

    // Set server socket to non-blocking mode
    if (setNonBlockingSocket(server_fd) == -1) {
        std::cerr << "[Cache] Failed to set socket to non-blocking mode." << std::endl;
        close(server_fd);
        return;
    }


    // Create two kqueues
    int kq_accept = kqueue();  // For accepting connections
        if (kq_accept == -1) {
            close(server_fd);
            throw std::runtime_error("Failed to create kqueue.");
        }
    int kq_client = kqueue();  // For handling client events
        if (kq_client == -1) {
            close(server_fd);
            throw std::runtime_error("Failed to create kqueue.");
        }

    struct kevent event;
    // Register server socket in kq_accept
    EV_SET(&event, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_accept, &event, 1, NULL, 0, NULL) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to register server_fd with kqueue.");
    }
    EV_SET(&event, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to register server_fd with kqueue.");
    }

   
    if (pipe(wakeup_pipe) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to create wakeup pipe.");
    } else {
    std::cout << "[Cache] Wakeup pipe created successfully" << std::endl;
}
    // Set both ends of the pipe to non-blocking
    setNonBlockingSocket(wakeup_pipe[0]);
    setNonBlockingSocket(wakeup_pipe[1]);

    // Register the read end of the wakeup pipe with kqueue
    EV_SET(&event, wakeup_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_accept, &event, 1, NULL, 0, NULL) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to register wakeup pipe with kqueue.");
    } else {
    std::cout << "[Cache] Wakeup pipe registered with kqueue" << std::endl;
}
EV_SET(&event, wakeup_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq_client, &event, 1, NULL, 0, NULL) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to register wakeup pipe with kqueue.");
    } else {
    std::cout << "[Cache] Wakeup pipe registered with kqueue" << std::endl;
}

        std::thread acceptThread([this, kq_accept,server_fd,kq_client] {
    accept_connections(kq_accept, this,server_fd,kq_client,this->wakeup_pipe);
});
    std::thread clientEventThread([this, kq_client,server_fd] {
    handle_client_connections(kq_client,this,server_fd,this->wakeup_pipe);
});
    std::cout<<"[Cache] Cluster Manager is Active for Cluster ID: "<<this->cluster_id_<<std::endl;
    std::cout << "[Cache] Cache server started and listening on " << ip_ << ":" << port_ << std::endl;

// Join threads when shutting down
    acceptThread.join();
    clientEventThread.join();
    close(server_fd);  // Close server socket
    close(kq_accept);         // Close kqueue
    close(kq_client); 
    std::cout << "[Cache] Cache Server Shutting down..." << std::endl;
}



void Cache::handle_client(int new_socket){
    // Receive the client's request
        char buffer[1024] = {0};
        int valread = read(new_socket, buffer, 1024);
        if (valread < 0) {
            std::cerr << "[Cache] Failed to read from socket." << std::endl;
            close(new_socket);
            return;
        }

        std::string request(buffer, valread);
        std::string response;
        // std::cout<<request;
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
            
            try {
                response = routeGetRequest(request);
            } catch (const std::runtime_error& e) {
                response = std::string("-ERR: ") + e.what();
            }

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
        close(new_socket); // Close connection after handling
}

// void Cache::handle_client(int new_socket){
//     // Receive the client's request
//         char buffer[1024] = {0};
//         int valread = read(new_socket, buffer, 1024);
//         if (valread < 0) {
//             std::cerr << "[Cache] Failed to read from socket." << std::endl;
//             close(new_socket);
//             return;
//         }

//         std::string request(buffer, valread);
//         std::string response;
//         // std::cout<<request;
//         size_t pos = 0;
//         pos = request.find("\r\n");

//         if (pos != std::string::npos) {
//             pos += 6; // Move past "\r\n$_\r\n"
//         }
//         // Process the request (simple GET and SET handling)
//         try{
//             std::string s = request.substr(pos, 3);
//         }
//         catch(const std::exception& e) {
//             std::cout << "Caught a standard exception: " << e.what() << std::endl;
//             close(new_socket);
//             return;
//         }
//         if (request.substr(pos, 3) == "GET") {
            
//                 // Add to GET queue
//             {
//                 // std::lock_guard<std::mutex> lock(get_mutex);
//                 get_queue.push(std::make_tuple(new_socket, request));
//             }
//             // get_cv.notify_one(); // Notify the GET handler
//             return;

//         } else if (request.substr(pos, 3) == "SET") {

//             // Add to SET queue
//             {
//                 // std::lock_guard<std::mutex> lock(set_mutex);
//                 set_queue.push(std::make_tuple(new_socket, request));
//             }
//             // set_cv.notify_one(); // Notify the SET handler
//             return;
//         } 
//         else if (request.substr(pos, 3) == "ADD") {
//             // Handle the "ADD" case to add a new node to the cluster

//             // Move past "ADD\r\n"
//             pos += 5;
            
//             // Extract the IP address
//             size_t ip_length_start = request.find("$", pos) + 1;
//             size_t ip_length_end = request.find("\r\n", ip_length_start);
//             int ip_length = std::stoi(request.substr(ip_length_start, ip_length_end - ip_length_start));

//             pos = ip_length_end + 2; // Move past "\r\n"
//             std::string node_ip = request.substr(pos, ip_length);

//             pos += ip_length + 2; // Move past IP and "\r\n"

//             // Extract the port
//             size_t port_length_start = request.find("$", pos) + 1;
//             size_t port_length_end = request.find("\r\n", port_length_start);
//             int port_length = std::stoi(request.substr(port_length_start, port_length_end - port_length_start));

//             pos = port_length_end + 2; // Move past "\r\n"
//             int node_port = std::stoi(request.substr(pos, port_length));
//             boost::uuids::uuid uuid = boost::uuids::random_generator()();
//             std::string node_id = boost::uuids::to_string(uuid);
//             NodeConnectionDetails node{node_id, node_ip, node_port};
//             // Call the addNode method with the extracted IP and port
//             addNode(node);

//             response = "+OK Node Added\r\n";  // Response indicating the node was successfully added
//             response += this->cluster_id_;
//         }
//         else {
//             response = "-ERROR Unknown command\r\n";
//         }

//         // Send the response back to the client
//         ssize_t total_sent = 0;
//         ssize_t bytes_to_send = response.size();
//         const char* data = response.c_str();

//         while (total_sent < bytes_to_send) {
//             ssize_t sent = send(new_socket, data + total_sent, bytes_to_send - total_sent, 0);
            
//             if (sent < 0) {
//                 if (errno == EINTR) {
//                     continue;  // Interrupted by a signal, retry
//                 }
//                 else if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                     // Socket temporarily unavailable, wait and retry
//                     usleep(1000);  // sleep briefly and retry
//                     continue;
//                 }
//                 else {
//                     // Some other error occurred
//                     throw std::runtime_error("Send failed");
//                 }
//             }
//             total_sent += sent;
//         }
//         close(new_socket); // Close connection after handling
// }

void Cache::get_handler() {
    while (!this->stopServer.load()) {
         std::tuple<int, std::string> task;
        //  std::cout<<"Running get handler"<<std::endl;

        {
            // std::unique_lock<std::mutex> lock(get_mutex);
            // get_cv.wait(lock, [this] { return !get_queue.empty() || this->stopServer.load(); });
            if(this->get_queue.empty()){
                continue;
            }
            if (this->stopServer.load()) {
                return; // Exit the worker thread gracefully
            }
            task = get_queue.front();
            get_queue.pop();
        }
        int client_socket = std::get<0>(task);
        std::string request = std::get<1>(task);
        // Process the GET request
        std::string response;
        try {
                response = routeGetRequest(request);
            } catch (const std::runtime_error& e) {
                response = std::string("-ERR: ") + e.what();
            }

        // Optionally, send the response back to the client or handle it accordingly
        // Send the response back to the client
        ssize_t total_sent = 0;
        ssize_t bytes_to_send = response.size();
        const char* data = response.c_str();

        while (total_sent < bytes_to_send) {
            ssize_t sent = send(client_socket, data + total_sent, bytes_to_send - total_sent, 0);
            
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
        close(client_socket); // Close connection after handling
    }
}

void Cache::set_handler() {
    while (!this->stopServer.load()) {
         std::tuple<int, std::string> task;
        //  std::cout<<"Running set handler"<<std::endl;

        {
            // std::unique_lock<std::mutex> lock(set_mutex);
            // set_cv.wait(lock, [this] { return !set_queue.empty() || this->stopServer.load(); });
            if(this->set_queue.empty()){
                continue;
            }
            if (this->stopServer.load()) {
                return; // Exit the worker thread gracefully
            }
            task = set_queue.front();
            set_queue.pop();
        }
        int client_socket = std::get<0>(task);
        std::string request = std::get<1>(task);
        // Process the GET request
        std::string response;
        std::string result = routeSetRequest(request);
        std::cout<<"set result:"<<result<<std::endl;
            if (result == "Failed:No nodes available to route request.") {
                response = "-ERR No nodes available to route request.\r\n";
            } else if (result == "Failed:All nodes are currently being deleted or no nodes available.") {
                response = "-ERR All nodes are currently being deleted or no nodes available.\r\n";
            }
            else {
                // If it's not a failure, respond with OK
                response = "+OK\r\n";
            }
        std::cout<<"set Response:"<<response<<std::endl;
        // Optionally, send the response back to the client or handle it accordingly
        // Send the response back to the client
        ssize_t total_sent = 0;
        ssize_t bytes_to_send = response.size();
        const char* data = response.c_str();

        while (total_sent < bytes_to_send) {
            ssize_t sent = send(client_socket, data + total_sent, bytes_to_send - total_sent, 0);
            
            if (sent < 0) {
                if (errno == EINTR) {
                    std::cout<<"1"<<std::endl;
                    continue;  // Interrupted by a signal, retry
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket temporarily unavailable, wait and retry
                    usleep(1000);  // sleep briefly and retry
                    std::cout<<"2"<<std::endl;
                    continue;
                }
                else {
                    // Some other error occurred
                    std::cout<<"3"<<std::endl;
                    std::cerr << "Send failed: " << strerror(errno) << " (errno: " << errno << ")" << std::endl;
                    throw std::runtime_error("Send failed");
                }
            }
            total_sent += sent;
        }
        close(client_socket); // Close connection after handling
    }
}