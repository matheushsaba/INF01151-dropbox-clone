#include "replication.h"
#include <thread>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <filesystem>
#include "../common/packet.h" 
#include <fstream> 
#include <iostream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>

// cstring, algorithm, + from server_tcp.cpp

// global variables declared as extern in .h
std::vector<PeerReplicationInfo> g_replication_peers;
std::mutex g_replication_peers_mtx;

// private helper functions:
void handle_backup_initial_sync_request(int backup_connected_socket);

void handle_primary_replication_push(int primary_connected_socket);

// implementation of the functions in replication.h:

// Primary: 
void start_primary_replication_listener() {
    int listener_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_socket < 0) {
        perror("error opening primary replication listener socket");
        std::exit(EXIT_FAILURE); // primary cannot start without opening the replication listener
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(REPLICATION_PORT);

    int optval = 1;
    // allow reuse of local addresses
    if (setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_REUSEADDR failed for primary replication listener");
        close(listener_socket);
        std::exit(EXIT_FAILURE);
    }

    // bind primary replication listener socket
    if (bind(listener_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("Error binding primary replication listener socket");
        close(listener_socket);
        std::exit(EXIT_FAILURE);
    }

    if (listen(listener_socket, 10) < 0) { // max 10 pending connections in the queue - I think it could be less
        perror("Error listening on primary replication listener socket");
        close(listener_socket);
        std::exit(EXIT_FAILURE);
    }

    std::cout << "[Primary] Replication listener active on port " << REPLICATION_PORT << " (for backups pulling sync data)...\n";

    // thread that will continue accepting incoming connections from backup servers 
    // each accepted connection will be handled in its own thread 

    std::thread([listener_socket]() {
        while (true) {
            sockaddr_in backup_addr{};
            socklen_t backup_len = sizeof(backup_addr);
            // accept a new connection from a backup:
            int backup_connected_socket = accept(listener_socket, reinterpret_cast<sockaddr*>(&backup_addr), &backup_len);
            if (backup_connected_socket < 0) {
                perror("accept failed on primary replication listener socket");
                continue;
            }
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(backup_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
            std::cout << "[Primary] new backup connected to replication listener: " << ip_str << ':' << ntohs(backup_addr.sin_port) << '\n';
            // choose which output to keep
            std::cout << "[Primary] new backup connected to replication listener from " << ip_str << " (fd=" << backup_connected_socket << ")\n";
            
            // new detatched thread to handle this specific backup requests (like initial sync)
            //COMMENTING BELOW SO I CAN COMPILE BEFORE IMPLEMENTING IT!!!
            //std::thread(handle_backup_initial_sync_request, backup_connected_socket).detach();
        }
    }).detach();
}

void connect_to_all_backup_replication_ports_for_push() {
    std::lock_guard<std::mutex> lock(g_replication_peers_mtx); // protect access to g_replication_peers
    for (auto& peer_info : g_replication_peers) {

        if (peer_info.push_socket_fd != -1) {
            std::cout << "[Primary] Already connected to " << peer_info.ip << ":" << peer_info.port << " for push replication.\n";
            continue;
        }

        // new TCP socket for each peer:
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            perror("error creating socket for primary push replication");
            continue;
        }

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(peer_info.port);
        if (inet_pton(AF_INET, peer_info.ip.c_str(), &(sa.sin_addr)) != 1) {
            std::cerr << "[Primary] Invalid IP address for peer " << peer_info.ip << " for push replication.\n";
            close(s);
            continue;
        }

        // try to connect to the backup server: if it fails, push_socket_fd remains -1
        // might fail if the backup is not yet online or its listener isn't ready.
        // it will be retried eventually, like when replicate_file_change needs to push
        if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            std::cerr << "[Primary] Failed to connect to backup " << peer_info.ip << ":" << peer_info.port << " for push replication at startup.\n";
            close(s);
            continue;
        }

        peer_info.push_socket_fd = s;
        std::cout << "[Primary] Connected to backup " << peer_info.ip << ":" << peer_info.port << " for push replication (fd=" << s << ").\n";
    }
}

void replicate_file_change(const std::string& username, const std::string& filename, PacketType change_type);

