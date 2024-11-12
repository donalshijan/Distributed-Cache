#ifndef NODE_CONNECTION_DETAILS_H
#define NODE_CONNECTION_DETAILS_H

#include <string>
struct NodeConnectionDetails {
    std::string node_id;
    std::string ip;
    int port;
};
#endif