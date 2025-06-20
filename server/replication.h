# pragma once 

#include <string>
#include <vector>
#include <mutex>
#include "../common/packet.h"

// dedicated port for inter-server replication communication
constexpr int REPLICATION_PORT = 5003;

// Primary:

struct PeerReplicationInfo { //maybe we could put this in a separate file for heartbeat, election bully and replication to use?
    std::string ip;
    int port;
};

// connections between primary and backups will be stored here
extern std::vector<std::string> g_replication_peers;  // runtime list of backup servers

// protect access to the vector, as it might be accessible by multiple threads
// when connecting/promoting, etc.
extern std::mutex g_replication_peers_mtx; 

// start listener thread on the replication port and accepts connections
// from backup servers that will pull data from the primary server
// each accepted connection will be handled in a new detached thread
void start_primary_replication_listener();

// maintains TCP connections from the primary to the backup servers,
// (on their replication ports) that will be used by the primary to push
// file changes to the backups 
void connect_to_all_backup_replication_ports_for_push();

void replicate_file_change(const std::string& username, const std::string& filename, PacketType change_type);

// Backups:




