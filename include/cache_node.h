#ifndef CACHE_NODE_H
#define CACHE_NODE_H

#include <string>
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
    CacheNode(size_t max_memory, std::chrono::seconds ttl,EvictionStrategy strategy);
    // Set a key-value pair
    void set(const std::string& key, const std::string& value);

    // Get a value by key
    std::string get(const std::string& key) ;
    std::string getNodeId() const;
    void assignToCluster(const std::string& cluster_id, std::string& ip, int& port);
    size_t getMaxMemory() const;
    size_t getUsedMemory() const;
    void handle_client(int client_socket);
    std::string processRequest(const std::string& request);
    void start_node_server();


private:
    std::string node_id_;
    // Internal data storage
    std::unordered_map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> store_;
    
    std::unordered_map<std::string, size_t> frequency_; // For LFU
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

    void evict();
    void evictLRU();
    void evictLFU();
    void remove(const std::string& key);
    
};

#endif // CACHE_NODE_H
