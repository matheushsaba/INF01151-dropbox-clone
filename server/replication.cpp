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
#include "server_tcp.h"

// global variables
std::vector<PeerReplicationInfo> g_replication_peers;
std::mutex g_replication_peers_mtx;

// private helper functions:

// handles an incoming connection from a backup server on the primary
void handle_backup_initial_sync_request(int backup_connected_socket) {
    Packet pkt;
    if (!recv_packet(backup_connected_socket, pkt)) {
        std::cerr << "[Primary Replication] Error receiving initial sync request packet. Closing socket.\n";
        close(backup_connected_socket);
        return;
    }

    // loop to receive requests from the backup like "FULL_SYNC_REQUEST"
        if (pkt.type == PACKET_TYPE_CMD) {
            std::string cmd_str(pkt.payload, pkt.length);
            if (cmd_str == "FULL_SYNC_REQUEST") {
                std::cout << "[Primary Replication] Received FULL_SYNC_REQUEST from backup (fd=" << backup_connected_socket << ").\n";

                // file_mutex to ensure consistent read of server_storage
                std::lock_guard<std::mutex> lock(file_mutex);

                std::string base_dir = "server_storage";
                bool files_to_send = false;

                // iterate through all user directories in server_storage
                for (const auto& entry : std::filesystem::directory_iterator(base_dir)) {
                    // check if it's a directory and starts with "sync_dir_"
                    if (entry.is_directory() && entry.path().filename().string().rfind("sync_dir_", 0) == 0) {
                        std::string username_dir_name = entry.path().filename().string(); // ex. sync_dir_alice
                        std::string username = username_dir_name.substr(username_dir_name.find('_') + 1); // get "alice"

                        // iterate through all files within this user's sync_dir
                        for (const auto& file_entry : std::filesystem::directory_iterator(entry.path())) {
                            if (file_entry.is_regular_file()) { // only replicate regular files (contain data)
                                files_to_send = true;
                                std::string filename = file_entry.path().filename().string();
                                std::string full_filepath = file_entry.path().string();

                                // 1. send file header (a replication-specific command)
                                Packet file_header_pkt{};
                                file_header_pkt.type = PACKET_TYPE_CMD; 
                                std::string header_msg = "replicate_upload|" + username + "|" + filename;
                                file_header_pkt.length = std::min((int)header_msg.size(), MAX_PAYLOAD_SIZE);
                                std::memcpy(file_header_pkt.payload, header_msg.c_str(), file_header_pkt.length);
                                if (!send_packet(backup_connected_socket, file_header_pkt)) {
                                    std::cerr << "[Primary Sync] Error sending file header to backup (fd=" << backup_connected_socket << ").\n";
                                    // Handle error (e.g., close socket, log, skip file)
                                    goto next_file;
                                }

                                // 2. send file content using PACKET_TYPE_DATA
                                std::ifstream file(full_filepath, std::ios::binary);
                                if (file.is_open()) {
                                    char buffer[MAX_PAYLOAD_SIZE];
                                    int seqn = 1;
                                    Packet data_pkt{};
                                    while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
                                        data_pkt.type = PACKET_TYPE_DATA;
                                        data_pkt.seqn = seqn++;
                                        data_pkt.length = file.gcount();
                                        std::memcpy(data_pkt.payload, buffer, data_pkt.length);
                                        if (!send_packet(backup_connected_socket, data_pkt)) {
                                            std::cerr << "[Primary Sync] Error sending file data to backup (fd=" << backup_connected_socket << ").\n";
                                            file.close();
                                            goto next_file;
                                        }
                                    }
                                    // 3. send end-of-file marker
                                    Packet end_pkt{};
                                    end_pkt.type = PACKET_TYPE_END;
                                    end_pkt.seqn = seqn; 
                                    end_pkt.length = 0; // end
                                    if (!send_packet(backup_connected_socket, end_pkt)) {
                                        std::cerr << "[Primary Sync] Error sending END marker to backup (fd=" << backup_connected_socket << ").\n";
                                    }
                                    file.close(); // close the file stream
                                    std::cout << "[Primary Sync] Sent " << filename << " for user " << username << " to backup.\n";
                                } else {
                                    std::cerr << "[Primary Sync] Could not open file " << full_filepath << " for sync.\n";
                                }
                            }
                            next_file:; // label for goto
                        }
                    }
                }

                if (!files_to_send) {
                    std::cout << "[Primary Replication] No files found in server_storage to send for full sync.\n";
                }

                // send ACK for FULL_SYNC_COMPLETE, even if no files were sent
                Packet final_ack_pkt;
                final_ack_pkt.type = PACKET_TYPE_ACK;
                std::string msg = "FULL_SYNC_COMPLETE";
                final_ack_pkt.length = std::min((int)msg.size(), MAX_PAYLOAD_SIZE);
                std::memcpy(final_ack_pkt.payload, msg.c_str(), final_ack_pkt.length);
                if (!send_packet(backup_connected_socket, final_ack_pkt)) {
                     std::cerr << "[Primary Replication] Error sending FULL_SYNC_COMPLETE ACK to backup (fd=" << backup_connected_socket << ").\n";
                }
                std::cout << "[Primary Replication] Sent FULL_SYNC_COMPLETE to backup (fd=" << backup_connected_socket << ").\n";

            } else {
                std::cerr << "[Primary Replication Listener] Received unknown CMD: " << cmd_str << " from backup (fd=" << backup_connected_socket << ").\n";
            }
        } else {
            std::cerr << "[Primary Replication Listener] Received unexpected packet type " << pkt.type << " from backup (fd=" << backup_connected_socket << ").\n";
        }
    std::cout << "[Primary Replication Listener] Backup disconnected (fd=" << backup_connected_socket << ").\n";
    close(backup_connected_socket);
}

// handles incoming push replication messages from the primary.
void handle_primary_replication_push(int primary_connected_socket) {
    Packet pkt;
    std::string current_username;
    std::string current_filename;
    std::ofstream outfile;
    bool expecting_file_data = false;

    while (recv_packet(primary_connected_socket, pkt)) {
        if (pkt.type == PACKET_TYPE_CMD) {
            std::string header(pkt.payload, pkt.length);
            // start of a file transfer
            if (header.rfind("replicate_upload|", 0) == 0) {
                size_t p1 = header.find('|');
                size_t p2 = header.find('|', p1 + 1);
                if (p1 == std::string::npos || p2 == std::string::npos) {
                    std::cerr << "[Backup Replication] Malformed upload replication header: " << header << '\n';
                    continue; // skip malformed packet
                }
                current_username = header.substr(p1 + 1, p2 - p1 - 1);
                current_filename = header.substr(p2 + 1);

                std::string full_path = get_sync_dir(current_username) + "/" + current_filename;
                
                // file_mutex before opening/creating the file
                std::lock_guard<std::mutex> lock(file_mutex);
                outfile.open(full_path, std::ios::binary);
                if (!outfile.is_open()) {
                    std::cerr << "[Backup Replication] Failed to open file for writing: " << full_path << '\n';
                    continue;
                }
                expecting_file_data = true; 
                std::cout << "[Backup Replication] Preparing to receive file push: " << full_path << '\n';

            }
            // handle "replicate_delete" command
            else if (header.rfind("replicate_delete|", 0) == 0) {
                size_t p1 = header.find('|');
                size_t p2 = header.find('|', p1 + 1);
                if (p1 == std::string::npos || p2 == std::string::npos) {
                    std::cerr << "[Backup Replication] Malformed delete replication header: " << header << '\n';
                    continue; // skip malformed packet
                }
                current_username = header.substr(p1 + 1, p2 - p1 - 1);
                current_filename = header.substr(p2 + 1);

                std::string full_path = get_sync_dir(current_username) + "/" + current_filename;
                
                // file_mutex before deleting the file
                std::lock_guard<std::mutex> lock(file_mutex);
                if (std::filesystem::exists(full_path) && std::filesystem::remove(full_path)) {
                    std::cout << "[Backup Replication] Deleted file push: " << full_path << '\n';
                    // send ACK back to primary for successful deletion
                    Packet ack_pkt{}; ack_pkt.type = PACKET_TYPE_ACK;
                    std::string ack_msg = "DELETE_ACK_OK";
                    ack_pkt.length = std::min((int)ack_msg.size(), MAX_PAYLOAD_SIZE);
                    std::memcpy(ack_pkt.payload, ack_msg.c_str(), ack_pkt.length);
                    send_packet(primary_connected_socket, ack_pkt);
                } else {
                    std::cerr << "[Backup Replication] Error deleting file push or file not found: " << full_path << '\n';
                }
                expecting_file_data = false; // no data expected after a delete command
            } else {
                 std::cerr << "[Backup Replication] Received unknown replication CMD: " << header << '\n';
            }
        }
        // handle DATA packets (file content)
        else if (pkt.type == PACKET_TYPE_DATA && expecting_file_data) {
            if (outfile.is_open()) {
                outfile.write(pkt.payload, pkt.length);
                if (!outfile) {
                    std::cerr << "[Backup Replication] Error writing file data to disk. Closing file: " << current_filename << '\n';
                    outfile.close(); // close file to prevent further corruption
                    expecting_file_data = false;
                }
            } else {
                std::cerr << "[Backup Replication] Received DATA packet but no file is open for writing.\n";
                // protocol mismatch or error from primary
            }
        }
        // handle END packet (end of file transfer)
        else if (pkt.type == PACKET_TYPE_END && expecting_file_data) {
            if (outfile.is_open()) {
                outfile.close(); // close the file stream
                std::cout << "[Backup Replication] File push received and saved: " << current_filename << '\n';
                // send ACK to primary for successful file transfer completion
                Packet ack_pkt{}; ack_pkt.type = PACKET_TYPE_ACK;
                std::string ack_msg = "UPLOAD_ACK_OK";
                ack_pkt.length = std::min((int)ack_msg.size(), MAX_PAYLOAD_SIZE);
                std::memcpy(ack_pkt.payload, ack_msg.c_str(), ack_pkt.length);
                send_packet(primary_connected_socket, ack_pkt);
            } else {
                std::cerr << "[Backup Replication] Received END packet but no file was open. Possible protocol error.\n";
            }
            expecting_file_data = false; // reset state
        }
        // handle any other unexpected packet types
        else {
            std::cerr << "[Backup Replication] Received unexpected packet type " << pkt.type << " during replication push.\n";
        }
    }
    std::cout << "[Backup Replication Listener] Primary disconnected from push channel (fd=" << primary_connected_socket << ").\n";
    if (outfile.is_open()) outfile.close(); // clean up if file was partially open
    close(primary_connected_socket); // close socket
}

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
            std::thread(handle_backup_initial_sync_request, backup_connected_socket).detach();
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

void replicate_file_change(const std::string& username, const std::string& filename, PacketType change_type){
    // mutex to ensure exclusive access to the list of peers and to ensure
    // each replication happens at a time
    std::lock_guard<std::mutex> lock(g_replication_peers_mtx);

    std::string full_path;
    if (change_type == PACKET_TYPE_DATA) { //for uploads/modifies
        full_path = get_sync_dir(username) + "/" + filename;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "[Primary] Cannot replicate upload: file " << full_path << " does not exist locally.\n";
            return;
        }
    }

    // header packet: 
    Packet header_pkt{};
    header_pkt.type = PACKET_TYPE_CMD;
    std::string header_msg;

    if (change_type == PACKET_TYPE_DATA) { // upload or modify
        header_msg = "replicate_upload|" + username + "|" + filename;
    } else if (change_type == PACKET_TYPE_DELETE) { // delete
        header_msg = "replicate_delete|" + username + "|" + filename;
    } else {
        std::cerr << "[Primary] Unsupported replication change type: " << change_type << ". Aborting replication.\n";
        return;
    }

    header_pkt.length = std::min((int)header_msg.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(header_pkt.payload, header_msg.c_str(), header_pkt.length);

    // iterating to push the changes to each backup
    for (auto& peer_info : g_replication_peers) {
        // try to establish connection if not already connected
        if (peer_info.push_socket_fd == -1) {
            std::cerr << "[Primary] Attempting to reconnect to backup " << peer_info.ip << ":" << peer_info.port << " for push replication.\n";
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) { 
                perror("socket reconnect for replication"); 
                continue; 
            }

            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(peer_info.port);
            if (inet_pton(AF_INET, peer_info.ip.c_str(), &(sa.sin_addr)) != 1){
                std::cerr << "[Primary] Invalid IP address for peer " << peer_info.ip << " for push replication.\n";
                close(s);
                continue;
            }
            if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
                std::cerr << "[Primary] reconnect failed to " << peer_info.ip << ":" << peer_info.port << " for push replication. Skipping.\n";
                close(s); // skip this peer for this replication round
                continue;
            }
            peer_info.push_socket_fd = s; // store the established socket
            std::cout << "[Primary] Reconnected to backup " << peer_info.ip << ":" << peer_info.port << " for push replication (fd=" << s << ").\n";
        }

        std::cout << "[Primary] Replicating " << filename << " (" << header_msg << ") to backup " << peer_info.ip << "...\n";
        
        if (!send_packet(peer_info.push_socket_fd, header_pkt)) {
            std::cerr << "[Primary] Error sending replication header to " << peer_info.ip << ". Closing socket.\n";
            close(peer_info.push_socket_fd); 
            peer_info.push_socket_fd = -1; // mark as disconnected
            continue;
        }

        if (change_type == PACKET_TYPE_DATA) { // upload or modify
            std::ifstream file(full_path, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "[Primary] Error opening file " << full_path << " for replication.\n"; // why do we need to open it??
                continue;
            }
            char buffer[MAX_PAYLOAD_SIZE];
            int seqn = 1;
            Packet data_pkt{};
            while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
                data_pkt.type = PACKET_TYPE_DATA;
                data_pkt.seqn = seqn++;
                data_pkt.length = file.gcount();
                std::memcpy(data_pkt.payload, buffer, data_pkt.length);
                if (!send_packet(peer_info.push_socket_fd, data_pkt)) {
                    std::cerr << "[Primary] Error sending data to " << peer_info.ip << ". Closing socket.\n";
                    close(peer_info.push_socket_fd);
                    peer_info.push_socket_fd = -1; // mark as disconnected
                    file.close();
                    break;
                }
            }

            // send EOF marker
            data_pkt.type = PACKET_TYPE_END;
            data_pkt.seqn = seqn;
            data_pkt.length = 0; // end of data stream
            if (!send_packet(peer_info.push_socket_fd, data_pkt)) {
                std::cerr << "[Primary] Error sending EOF to " << peer_info.ip << ". Closing socket.\n";
                close(peer_info.push_socket_fd);
                peer_info.push_socket_fd = -1;
                continue;
            }

            file.close();
        }
        // wait for ACK from the backup server
        Packet ack_pkt;
        if (!recv_packet(peer_info.push_socket_fd, ack_pkt)) { // blocking recv_packet!!
            std::cerr << "[Primary] Error receiving ACK from " << peer_info.ip << ". Closing socket.\n";
            close(peer_info.push_socket_fd);
            peer_info.push_socket_fd = -1;
            continue; // move to the next peer
        }
        std::cout << "[Primary] Replication of " << filename << " to " << peer_info.ip << " successful (ACK received).\n";
    }
}

// backups:

void start_backup_replication_listener() {
    int listener_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_socket < 0) {
        perror("error opening backup replication listener socket");
        return; // won't receive pushes but still can participate in elections
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(REPLICATION_PORT);

    int optval = 1;
    if (setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_REUSEADDR failed for backup replication listener");
        close(listener_socket);
        return;
    }

    if (bind(listener_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("Error binding backup replication listener socket");
        close(listener_socket);
        return;
    }
    if (listen(listener_socket, 1) < 0) { // 1 connection (with the primary)
        perror("Error listening on backup replication listener socket");
        close(listener_socket);
        return;
    }

    std::cout << "[Backup] Replication listener active on port " << REPLICATION_PORT << " (for primary pushes)...\n";

    // the following thread waits for a connection from the primary
    // and if a new one is elected, it accepts the new conection
    std::thread([listener_socket]() {
        while (true) {
            sockaddr_in primary_addr{};
            socklen_t primary_len = sizeof(primary_addr);
            int primary_connected_socket = accept(listener_socket, reinterpret_cast<sockaddr*>(&primary_addr), &primary_len);
            if (primary_connected_socket < 0) {
                perror("accept failed on backup replication listener");
                continue;
            }
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(primary_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
            std::cout << "[Backup] Connected to primary " << ip_str << ':' << ntohs(primary_addr.sin_port) << '\n';
            
            // detatch a thread to continuously handle incoming replication data from the primary
            std::thread(handle_primary_replication_push, primary_connected_socket).detach();
        }
    }).detach();
}

void request_full_sync_from_primary(const std::string& primary_ip) {
    std::cout << "[Backup] Requesting full sync from primary " << primary_ip << ":" << REPLICATION_PORT << '\n';
    int s = -1;
    int max_retries = 10;
    int current_retry = 0;
    bool connected = false;
    while (!connected && current_retry < max_retries) {

        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { 
            perror("[Backup sync] socket creation failed");
            return; 
        }

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(REPLICATION_PORT);
        if (inet_pton(AF_INET, primary_ip.c_str(), &(sa.sin_addr)) != 1) {
            std::cerr << "[Backup sync] Invalid IP address for primary " << primary_ip << '\n';
            close(s);
            return;
        }
        if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
            std::cerr << "[Backup sync] Failed to connect to primary " << primary_ip << ":" << REPLICATION_PORT << " (attempt " << (current_retry + 1) << "/" << max_retries << "). Retrying...\n";
            close(s); // close failed socket
            // exponential backoff: sleep for 1s, then 2s, 4s, etc., up to a max
            std::this_thread::sleep_for(std::chrono::seconds(1 << std::min(current_retry, 4))); // Cap sleep at 16s (2^4)
            current_retry++;
        } else {
            connected = true;
            std::cout << "[Backup sync] Successfully connected to primary for full sync.\n";
        }
    }

    // send full sync request command:
    Packet req_pkt{};
    req_pkt.type = PACKET_TYPE_CMD;
    std::string req_msg = "FULL_SYNC_REQUEST";
    req_pkt.length = std::min((int)req_msg.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(req_pkt.payload, req_msg.c_str(), req_pkt.length);
    if (!send_packet(s, req_pkt)) {
        std::cerr << "[Backup sync] Error sending sync request to the primary. Closing socket.\n";
        close(s);
        return;
    }
    std::cout << "[Backup sync] sent FULL_SYNC_REQUEST to primary. \n";

    // receive files/commands from primary
    Packet pkt;
    std::string current_username;
    std::string current_filename; 
    std::ofstream outfile;
    bool expecting_file_data = false;
    while (recv_packet(s, pkt)) {
        if (pkt.type == PACKET_TYPE_CMD) {
            std::string header(pkt.payload, pkt.length);
            if (header.rfind("replicate_upload|", 0) == 0) {
                if (outfile.is_open()) outfile.close(); // closes any file opened previously
                size_t p1 = header.find('|');
                size_t p2 = header.find('|', p1 + 1);
                current_username = header.substr(p1 + 1, p2 - p1 - 1);
                current_filename = header.substr(p2 + 1);
                std::string full_path = get_sync_dir(current_username) + "/" + current_filename;

                // protect file operations:
                std::lock_guard<std::mutex> lock(file_mutex);
                outfile.open(full_path, std::ios::binary);
                if (!outfile.is_open()) {
                    std::cerr << "[Backup sync] failed to open file for writing: " << full_path << '\n';
                    continue;
                }
                expecting_file_data = true;
                std::cout << "[Backup Sync] Receiving file: " << full_path << '\n';

            } else if (header.rfind("replicate_delete|", 0) == 0) {
                if (outfile.is_open()) outfile.close(); // close any previously open file
                size_t p1 = header.find('|');
                size_t p2 = header.find('|', p1 + 1);
                current_username = header.substr(p1 + 1, p2 - p1 - 1);
                current_filename = header.substr(p2 + 1);
                std::string full_path = get_sync_dir(current_username) + "/" + current_filename;

                std::lock_guard<std::mutex> lock(file_mutex); // protect file operation
                if (std::filesystem::remove(full_path)) {
                    std::cout << "[Backup Sync] Deleted file: " << full_path << '\n';
                } else {
                    std::cerr << "[Backup Sync] Error deleting file: " << full_path << '\n';
                }
                expecting_file_data = false; // No data expected after delete
            }
        } else if (pkt.type == PACKET_TYPE_DATA && expecting_file_data) {
            if (outfile.is_open()) {
                outfile.write(pkt.payload, pkt.length);
                if (!outfile) {
                    std::cerr << "[Backup Sync] error writing file data during sync. Closing file: " << current_filename << '\n';
                    outfile.close();
                    expecting_file_data = false;
                }
            }
        } else if (pkt.type == PACKET_TYPE_END && expecting_file_data) {
            if (outfile.is_open()) {
                outfile.close();
                std::cout << "[Backup Sync] File received and saved: " << current_filename << '\n';
            }
            expecting_file_data = false;
        } else if (pkt.type == PACKET_TYPE_ACK) {
            std::string ack_msg(pkt.payload, pkt.length);
            if (ack_msg == "FULL_SYNC_COMPLETE") {
                std::cout << "[Backup Sync] Full synchronization complete.\n";
                break; // exit the loop when full sync is confirmed
            } else {
                std::cout << "[Backup Sync] Received unexpected ACK: " << ack_msg << '\n';
            }
        } else {
            std::cerr << "[Backup Sync] Received unexpected packet type " << pkt.type << ".\n";
        }
    }
    if (outfile.is_open()) outfile.close();
    close(s);
}




