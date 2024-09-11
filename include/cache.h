#ifndef CACHE_H
#define CACHE_H

#include <vector>
#include <string>
#include <mutex>

struct NodeConnectionDetails {
    std::string node_id;
    std::string ip;
    int port;
};

class Cache {
public:
    Cache(const std::string& ip, const int& port);

    void addNode(const NodeConnectionDetails& node_connection_details);
    void removeNode(const std::string& node_id);
    void migrateData(const std::string& node_id, const std::vector<std::string>& target_nodes);
    std::string getClusterId() const;

    void routeGetRequest(const std::string& request);
    void routeSetRequest(const std::string& request);
    void get(const std::string& key);
    void set(const std::string& key, const std::string& value);

private:
    std::vector<NodeConnectionDetails> nodes_;
    size_t next_node_ = 0;
    std::hash<std::string> hasher_;
    std::mutex mutex_;
    std::string cluster_id_;
    std::string ip_;
    int port_;
    std::string node_id_being_deleted_;

    // Helper function to send data over TCP/IP
    std::string sendToNode(const std::string& ip, int port, const std::string& request);
};

#endif // CACHE_H
