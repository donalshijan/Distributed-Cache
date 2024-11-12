#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <stdexcept>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "connection_pool.h"
#include "cache_node.h"
#include <netinet/tcp.h>

ConnectionPool::ConnectionPool() {
    
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex);
    // Close all connections in the pool
    for (const auto& pair : connections) {
        close(pair.second);
    }
}

// Method to add a connection for a new node
bool ConnectionPool::addConnection(const NodeConnectionDetails& node) {
    std::lock_guard<std::mutex> lock(mutex);
    try {
        int sockfd = createConnection(node);
        connections[node.node_id] = sockfd;
        return true;  // Connection successful
    } catch (const std::runtime_error& e) {
        std::cerr << "Error adding connection for node " << node.node_id << ": " << e.what() << std::endl;
        return false;  // Connection failed
    }
}

int ConnectionPool::createConnection(const NodeConnectionDetails& node) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        throw std::runtime_error("Socket creation failed for node " + node.node_id);
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(node.port);
    if (inet_pton(AF_INET, node.ip.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sockfd);
        throw std::runtime_error("Invalid address for node " + node.node_id);
    }

    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    int idle = 10;      // Time before sending keepalive probes (in seconds)
    int interval = 5;   // Interval between keepalive probes (in seconds)
    int max_pkt = 3;    // Maximum keepalive probes before the connection is considered down

    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &max_pkt, sizeof(max_pkt));
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        throw std::runtime_error("Connection failed for node " + node.node_id);
    }

    return sockfd;
}

int ConnectionPool::getConnection(const std::string& node_id) {
    std::unique_lock<std::mutex> lock(mutex);
    auto it = connections.find(node_id);
    if (it == connections.end() || it->second < 0) {
        throw std::runtime_error("Connection not found for node " + node_id);
    }
    return it->second;
}

void ConnectionPool::returnConnection(const std::string& node_id, int sockfd) {
    std::lock_guard<std::mutex> lock(mutex);
    connections[node_id] = sockfd;  // Re-assign the connection back to the pool
}

void ConnectionPool::removeConnection(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex);  // Ensure thread safety

    auto it = connections.find(node_id);
    if (it != connections.end()) {
        close(it->second);  // Close the socket associated with this node_id
        connections.erase(it);  // Remove the entry from the map
    }
}