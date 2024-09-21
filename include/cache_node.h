#ifndef CACHE_NODE_H
#define CACHE_NODE_H

#include <string>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <map>
#include <list>
#include <vector>
#include <algorithm>
#include <functional>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <future>

#define DEFAULT_CACHE_NODE_PORT 8069

#define DEFAULT_CACHE_PORT 7069

#define DEFAULT_IP "127.0.0.1" // Localhost as default IP

struct NodeConnectionDetails {
    std::string node_id;
    std::string ip;
    int port;
};

class Cache {
public:
    Cache(const std::string& ip=DEFAULT_IP, const int& port=DEFAULT_CACHE_PORT);

    void addNode(const NodeConnectionDetails& node_connection_details);
    void removeNode(const std::string& node_id);
    void migrateData(const std::string& node_id, const std::vector<std::string>& target_nodes);
    std::string getClusterId() const;

    std::string routeGetRequest(const std::string& request);
    std::string routeSetRequest(const std::string& request);
    std::string get(const std::string& key);
    std::string set(const std::string& key, const std::string& value);
    void startCacheServer();
    std::string getPublicIp();

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

enum EvictionStrategy {
    NoEviction,
    LRU,
    LFU
};
struct CacheClusterManagerConnectionDetail {
    std::string cluster_id_; // Cluster ID
    std::string ip_;
    int port_;
};

class CacheNode {
public:
    CacheNode(size_t max_memory, std::chrono::seconds ttl,EvictionStrategy strategy,std::string ip=DEFAULT_IP,int port = DEFAULT_CACHE_NODE_PORT);
    // Set a key-value pair
    std::string set(const std::string& key, const std::string& value);

    // Get a value by key
    std::string get(const std::string& key) ;
    std::string getNodeId() const;
    void assignToCluster(const std::string& cluster_id, std::string& ip, int& port);
    size_t getMaxMemory() const;
    size_t getUsedMemory() const;
    void handle_client(int client_socket);
    std::string processRequest(const std::string& request);
    void start_node_server();
    std::future<void> remove_future_;

private:
    struct LFUComparator {
        bool operator()(const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
            return a.first > b.first; // Min-heap: smaller frequency has higher priority
        }
    };
    std::string node_id_;
    int port_;
    std::string ip_;
    // Internal data storage
    std::unordered_map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> store_;
    // For LFU
    std::unordered_map<std::string, size_t> frequency_; 
    std::priority_queue<std::pair<int, std::string>, std::vector<std::pair<int, std::string>>, LFUComparator> min_heap_;
    // For LRU
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;
    void remove_expired();
    // Mutex for thread safety
    mutable std::mutex mutex_;
    EvictionStrategy strategy_;
    size_t max_memory_; // Maximum memory allowed for cache
    size_t used_memory_; 
    std::chrono::seconds ttl_;
    CacheClusterManagerConnectionDetail cluster_manager_details_;
    void wait_for_removal();
    void evict();
    void evictLRU();
    void evictLFU();
    void remove(const std::string& key);
    void trackAndUpdateLRU();
    void remove_expired_periodically();
    void lazyDeleteStaleEntry();
    std::string sendToNode(const std::string& ip, int port, const std::string& request);
    void addNodeToCluster(const std::string& cluster_manager_ip, int cluster_manager_port);
};

#endif // CACHE_NODE_H
