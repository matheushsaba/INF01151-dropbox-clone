# pragma once 

#include <string>
#include <vector>
#include <mutex>
#include "../common/packet.h"
#include "heartbeat.h"

// dedicated port for inter-server replication communication
constexpr int REPLICATION_PORT = 5003;

// Primary:

struct PeerReplicationInfo { //maybe we could put this in a separate file for heartbeat, election bully and replication to use?
    std::string ip;
    int port;
    int push_socket_fd = -1; // -1 indicates no active connection to the primary
};

// global:
// connections between primary and backups will be stored here
// std::vector<PeerReplicationInfo> g_replication_peers; // list of backup servers

// protect access to the vector, as it might be accessible by multiple threads
// when connecting/promoting, etc.
extern std::mutex g_replication_peers_mtx; 

// functions:
// start listener thread on the replication port and accepts connections
// from backup servers that will pull data from the primary server
// each accepted connection will be handled in a new detached thread
void start_primary_replication_listener(const std::string& server_id);

// maintains TCP connections from the primary to the backup servers,
// (on their replication ports) that will be used by the primary to push
// file changes to the backups 
void connect_to_all_backup_replication_ports_for_push();

// called whenever a file in sync_dir changes due to a client operation
// pushes changes and waits for acks from the backups
void replicate_file_change(const std::string& username, const std::string& filename, PacketType change_type, const std::string& server_id);

// Backups:

// starts a listener thread to accept incoming connections from the 
// primary server to this backup - accepts one connection and
// should restart if the primry disconnects, so a new primary can connect 
void start_backup_replication_listener(const std::string& server_id);

// called when a backup server starts or becomes the new primary
// to ensure its local sync_dir is consistent with the primary 
// it requests all data
void request_full_sync_from_primary(const std::string& primary_ip, const std::string& server_id);

void replication_add_peer(const std::string& ip, int port = REPLICATION_PORT);
void replication_remove_peer(const std::string& ip);



