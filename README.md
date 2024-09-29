# Project Overview

Project implements a high performant distributed cache system where data is stored in memory using HashMap(index) data structure with both key and value as string type. Cache implements configuration options for Eviction Strategy(NoEviction,LFU,LRU), Maximum memory limit, time to live.

It also implements built in clustering support and routing strategy where new nodes can be configured and added to existing cluster seamlessly, without worrying about managing request routing.

Also Implements  migration strategy for migrating data from one node in a cluster to other nodes if that node were to be removed.

All communications with and within the system uses a custom message protocol implementation similar to redis.

##  Architecture

Cache server will be set up and running on a machine and listening to requests from system admin for configuring the Cache cluster along with requests from clients accessing cache data.

Cache Node is to be set up and initialized and configured to be added to a particular cluster by sending a message to the cache server specifying it's own identifier across the internet which includes the ip of the machine where the node is set up and port on which the cache node server will be listening, this message is sent across internet to the cache server which is identified by the ip of the machine running the cache server and the port on which it listens. The cache server managing a cache cluster processes this request to add new node and updates it's cluster details to update the info regarding addition of new node. Successful addition and acknowledgement message is sent back to the cache node as response to the request it made upon which it starts it's server at the ip and port it specified in it's message to cache server and starts listening for requests.

We can set up cache nodes on any machine across the internet and configure it to be added to the cluster managed by a specific cache server, once it gets added, clients can access data across all nodes in that cluster without ever knowing about underlying topology of nodes and can access the data without ever worrying about routing the request to the nodes or handling responses for finding a key, instead cache server is the single endpoint for clients to access data, cache server in turn will route requests across all nodes to fetch or set data and returns appropriate response to clients.

## Message Protocol

Request format 

>*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n

* *2\r\n - The request consists of 2 parts (command and key).
* $3\r\nGET\r\n - The command is GET, which is 3 characters long.
$3\r\nkey\r\n - The key is 3 characters long (key is just an example; this can be any key length).

Similary for Set message

>*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n

and for Migrate 

>*1\r\n$7\r\nMIGRATE\r\n$7\r\nsnodeid\r\n*2\r\n$7\r\nip:port\r\n$7\r\nip:port\r\n


# How to Build

    Install cmake
    Install conan

After installing cmake and conan.
It is highly recommended to use the script mentioned in the Test section below to build and run tests.

But if you don't want to use the script and build manually here are the steps.

In the project root directry
run the command
```
conan install . --build=missing
```
This command will create a build folder in the current directory and install all packages according to the requirements mentioned in conanfile.txt

But in case you are using older version of conan, you will have to specify the name of the directory where you want conan to install.
So the command in older version is different, `conan install . --output-folder=build --build=missing`

```
cmake --preset conan-release 
```
This command will generate all the build files in the build directory.

```
cmake --build --preset conan-release   
```

This command will finally build the final executable file in build/Release folder  called distributed_cache.

# How to Run

To run the program, be it to set up the cache server or new node to be configured to existing cluster, or to interact with an existing cache server to access data, the final built executable distributed_cache can handle all those tasks.

## Usage

Example usage to create a new cache cluster and start a cache server

```
cd build/Release
./distributed_cache create_cache_cluster
```
This will start the cache server at the default ip and port which are "127.0.0.1" and 7069 respectively

Following is an example of what a successful response log can be like when running the above command in terminal.

```
Cluster Manager is Active for Cluster ID:f8f4b9f8-db41-449a-9001-b6ae83e6443e
Cache server started and listening on 127.0.0.1:7069
```

The above command can be further extended to pass specific ip and port as command line arguments

`./distributed_cache create_cache_cluster [--ip <ip>] [--port <port>]`

Example usage to add new node to an existing cache cluster

    ./distributed_cache add_new_node_to_existing_cluster --memory_limit 1048576 --time_till_eviction 50 --cluster_id f8f4b9f8-db41-449a-9001-b6ae83e6443e --cluster_ip 127.0.0.1 --cluster_port 7069

This command will set up a new node on the machine with the following configuration. Memory limit set to 1 MB, time till eviction set to 50 seconds, cluster id \<id> of the cache cluster to which we want to add this node to, Ip address of the machine running the cache server for the respective cluster and port on which it is listening.

If cache server accepts and successfully adds the new node and sends acknowledgement message back to cache node , the cache node's server will start at the default ip and port which are "127.0.0.1" and 8069 respectively.

Following is an example of what a successful response log can be like when running the above command in terminal.

```
Cache Node created with node id : 6ac52e36-723d-4f04-a20c-3e42a2af15bc
Node Id: 6ac52e36-723d-4f04-a20c-3e42a2af15bc added to cluster with id: f8f4b9f8-db41-449a-9001-b6ae83e6443e
Cache Node Server is running on  127.0.0.1:8069
```
The above command can also be extended with optional command line arguments ip and port to start the cache node server on a specific ip address and port other than default.

```
./distributed_cache add_new_node_to_existing_cluster --memory_limit <bytes> --time_till_eviction <seconds> --eviction_strategy <NoEviction|LRU|LFU> --cluster_id <id> --cluster_ip <ip> --cluster_port <port> [--ip <node_ip>] [--port <node_port>]
```
Example usage on how to make a set request 

```
./distributed_cache SET <cache_server_ip> <cache_server_port> <key> <value>
```

Following is an example of a successful response from running above command.

```
Server response: +OK
```


Example usage on how to make a get request 

```
./distributed_cache GET <cache_server_ip> <cache_server_port> <key>
```

Following is an example of a successful response from running above command.

```
Server response: $5
value

Value: value
```



# Test
cd into the project directory after cloning the repo.

To build and run tests you can simply run the following script

    chmod +x build_and_run_tests.sh
    ./build_and_run_tests.sh