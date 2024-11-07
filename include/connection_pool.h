#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H
#include <iostream>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <queue>
#include <string>
#include <stdexcept>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "node_connection_details.h"


class ConnectionPool {
public:
    ConnectionPool();
    ~ConnectionPool();
    bool addConnection(const NodeConnectionDetails& node);  // Adds connection for a new node
    int getConnection(const std::string& node_id);
    void returnConnection(const std::string& node_id, int sockfd);
    void removeConnection(const std::string& node_id);

private:
    std::unordered_map<std::string, int> connections;  // Node ID to socket file descriptor map
    std::mutex mutex;
    std::condition_variable condVar;

    int createConnection(const NodeConnectionDetails& node);
};

#endif 