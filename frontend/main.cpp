// frontend/main.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdio> 
#include "../common/packet.h"
#include "../common/common.hpp"

struct Address {
    std::string ip;
    int port;
};

using PortMap = std::map<int, int>; // Maps <fake_port_in_FE, real_port_in_RM>

struct ClientSession {
    std::mutex mtx;
    std::string username;
    std::string current_rm_ip;
    PortMap port_map;

    ClientSession(std::string uname, std::string rm_ip) : username(std::move(uname)), current_rm_ip(std::move(rm_ip)) {}
};

// --- Global Variables ---
Address g_current_primary_address;
std::mutex g_primary_address_mutex;
std::map<std::string, std::shared_ptr<ClientSession>> g_active_sessions;
std::mutex g_sessions_mutex;
std::string g_frontend_ip;

// --- Function Prototypes ---
void session_handshake_thread(int client_handshake_sock);
void proxy_connection_thread(std::shared_ptr<ClientSession> session, int fake_listener_sock);
void listen_for_leader_updates(int notification_port);
void forward_data(int source_sock, int dest_sock);
int create_listening_socket(int port);
int get_socket_port(int sockfd);
bool recover_session_handshake(std::shared_ptr<ClientSession> session);

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <frontend_ip> <initial_primary_ip> <initial_primary_port>\n";
        std::cerr << "Example: " << argv[0] << " 127.0.0.1 127.0.0.1 4000\n";
        return 1;
    }

    g_frontend_ip = argv[1];
    g_current_primary_address = {argv[2], std::stoi(argv[3])};
    std::cout << "[FE] Frontend IP set to " << g_frontend_ip << std::endl;
    std::cout << "[FE] Initial primary configured to " << g_current_primary_address.ip << ":" << g_current_primary_address.port << std::endl;

    int notification_port = 9090;
    std::thread(listen_for_leader_updates, notification_port).detach();

    int client_listen_port = 8080;
    int client_listener_sock = create_listening_socket(client_listen_port);
    if (client_listener_sock < 0) exit(1);

    std::cout << "[FE] Listening for clients on " << g_frontend_ip << ":" << client_listen_port << std::endl;

    while (true) {
        int client_sock = accept(client_listener_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            std::thread(session_handshake_thread, client_sock).detach();
        }
    }
    return 0;
}


void session_handshake_thread(int client_handshake_sock) {
    Packet pkt;
    if (!recv_packet(client_handshake_sock, pkt)) { close(client_handshake_sock); return; }
    std::string username(pkt.payload, pkt.length);

    Address primary_addr;
    {
        std::lock_guard<std::mutex> lock(g_primary_address_mutex);
        primary_addr = g_current_primary_address;
    }

    int rm_handshake_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in rm_addr_info{};
    rm_addr_info.sin_family = AF_INET;
    rm_addr_info.sin_port = htons(primary_addr.port);
    inet_pton(AF_INET, primary_addr.ip.c_str(), &rm_addr_info.sin_addr);

    if (connect(rm_handshake_sock, (struct sockaddr*)&rm_addr_info, sizeof(rm_addr_info)) < 0) {
        Packet err_pkt;
        const char* err_msg = "DENY|FE could not connect to the primary RM";
        err_pkt.length = strlen(err_msg);
        memcpy(err_pkt.payload, err_msg, err_pkt.length);
        send_packet(client_handshake_sock, err_pkt);
        close(client_handshake_sock);
        close(rm_handshake_sock);
        return;
    }

    send_packet(rm_handshake_sock, pkt);
    recv_packet(rm_handshake_sock, pkt);
    send_packet(client_handshake_sock, pkt); // Relay OK/DENY from RM

    if (std::string(pkt.payload, pkt.length).rfind("DENY", 0) == 0) {
        close(client_handshake_sock);
        close(rm_handshake_sock);
        return;
    }

    recv_packet(rm_handshake_sock, pkt);
    std::string real_ports_str(pkt.payload, pkt.length);
    close(rm_handshake_sock);

    int real_cmd_port, real_watch_port, real_file_port;
    sscanf(real_ports_str.c_str(), "%d|%d|%d", &real_cmd_port, &real_watch_port, &real_file_port);

    auto session = std::make_shared<ClientSession>(username, primary_addr.ip);

    std::vector<int> fake_ports;
    std::vector<int> real_ports = {real_cmd_port, real_watch_port, real_file_port};

    for (int real_p : real_ports) {
        int fake_listener_sock = create_listening_socket(0);
        if (fake_listener_sock < 0) { continue; }

        int fake_port = get_socket_port(fake_listener_sock);
        session->port_map[fake_port] = real_p;
        fake_ports.push_back(fake_port);
        std::thread(proxy_connection_thread, session, fake_listener_sock).detach();
    }

    std::string fake_ports_str = std::to_string(fake_ports[0]) + "|" + std::to_string(fake_ports[1]) + "|" + std::to_string(fake_ports[2]);
    pkt.length = fake_ports_str.length();
    memcpy(pkt.payload, fake_ports_str.c_str(), pkt.length);
    send_packet(client_handshake_sock, pkt);

    close(client_handshake_sock);

    std::lock_guard<std::mutex> lock(g_sessions_mutex);
    g_active_sessions[username] = session;
}

void proxy_connection_thread(std::shared_ptr<ClientSession> session, int fake_listener_sock) {
    int fake_port = get_socket_port(fake_listener_sock);
    while (true) {
        int client_sock = accept(fake_listener_sock, nullptr, nullptr);
        if (client_sock < 0) continue;

        std::thread([=]() {
            int real_port;
            std::string rm_ip;

            { // Lock scope
                std::lock_guard<std::mutex> lock(session->mtx);
                real_port = session->port_map.at(fake_port);
                rm_ip = session->current_rm_ip;
            }

            int rm_sock = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in rm_addr{};
            rm_addr.sin_family = AF_INET;
            rm_addr.sin_port = htons(real_port);
            inet_pton(AF_INET, rm_ip.c_str(), &rm_addr.sin_addr);

            if (connect(rm_sock, (struct sockaddr*)&rm_addr, sizeof(rm_addr)) == 0) {
                // HAPPY PATH: Connection succeeded, just forward data.
                std::thread c_to_r(forward_data, client_sock, rm_sock);
                std::thread r_to_c(forward_data, rm_sock, client_sock);
                c_to_r.join();
                r_to_c.join();
            } else {
                // FAILURE PATH: Connection was refused. Attempting to recover session.
                std::cerr << "[FE] Failed to connect to RM at " << rm_ip << ":" << real_port << ". Starting recovery..." << std::endl;
                close(rm_sock);

                if (recover_session_handshake(session)) {
                    std::cout << "[FE] Session recovery successful! Trying to reconnect..." << std::endl;

                    { // Updated session info
                        std::lock_guard<std::mutex> lock(session->mtx);
                        real_port = session->port_map.at(fake_port);
                        rm_ip = session->current_rm_ip;
                    }

                    rm_sock = socket(AF_INET, SOCK_STREAM, 0);
                    rm_addr.sin_port = htons(real_port);
                    inet_pton(AF_INET, rm_ip.c_str(), &rm_addr.sin_addr);

                    if (connect(rm_sock, (struct sockaddr*)&rm_addr, sizeof(rm_addr)) == 0) {
                        std::cout << "[FE] Reconnection to the new primary established!" << std::endl;
                        std::thread c_to_r(forward_data, client_sock, rm_sock);
                        std::thread r_to_c(forward_data, rm_sock, client_sock);
                        c_to_r.join();
                        r_to_c.join();
                    } else {
                        perror("[FE] Failed to reconnect even after recovery");
                        close(client_sock);
                        close(rm_sock);
                    }
                } else {
                    std::cerr << "[FE] Session recovery routine failed. Closing client connection." << std::endl;
                    close(client_sock);
                }
            }
        }).detach();
    }
}

void listen_for_leader_updates(int notification_port) {
    int listener_sock = create_listening_socket(notification_port);
    if (listener_sock < 0) exit(1);

    std::cout << "[FE] Listening for leader updates on port " << notification_port << std::endl;

    while (true) {
        int rm_sock = accept(listener_sock, nullptr, nullptr);
        if (rm_sock < 0) continue;

        char buffer[256] = {0};
        read(rm_sock, buffer, sizeof(buffer) - 1);
        close(rm_sock);

        char new_ip_str[16];
        int new_port;
        if (sscanf(buffer, "NEW_LEADER %15s %d", new_ip_str, &new_port) == 2) {
            std::lock_guard<std::mutex> lock(g_primary_address_mutex);
            g_current_primary_address.ip = new_ip_str;
            g_current_primary_address.port = new_port;
            std::cout << "[FE] LEADER UPDATE: New primary set to " << new_ip_str << ":" << new_port << std::endl;

            std::cout << "[FE] Updating state of active client sessions..." << std::endl;

            std::lock_guard<std::mutex> sessions_lock(g_sessions_mutex);

            for (auto const& [username, session_ptr] : g_active_sessions) {
                std::lock_guard<std::mutex> session_lock(session_ptr->mtx);

                // We only update the IP of the primary that this session should use.
                // The port map (port_map) is now outdated for this session,
                // but it will be corrected automatically by the recovery logic
                // the next time the client attempts an operation.
                session_ptr->current_rm_ip = new_ip_str;
            }
            std::cout << "[FE] All active sessions have been updated to the new leader." << std::endl;
        }
    }
}

void forward_data(int source_sock, int dest_sock) {
    char buffer[4096];
    int nbytes;
    while ((nbytes = read(source_sock, buffer, sizeof(buffer))) > 0) {
        if (write(dest_sock, buffer, nbytes) <= 0) {
            break;
        }
    }
    shutdown(source_sock, SHUT_RDWR);
    shutdown(dest_sock, SHUT_RDWR);
    close(source_sock);
    close(dest_sock);
}

// --- Socket Utility Function Implementations ---
int create_listening_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("[FE Helper] socket"); return -1; }
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, g_frontend_ip.c_str(), &addr.sin_addr);
    addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("[FE Helper] bind"); close(sockfd); return -1; }
    if (listen(sockfd, 20) < 0) { perror("[FE Helper] listen"); close(sockfd); return -1; }
    return sockfd;
}

int get_socket_port(int sockfd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(sockfd, (struct sockaddr*)&addr, &len) == -1) { perror("[FE Helper] getsockname"); return -1; }
    return ntohs(addr.sin_port);
}

bool recover_session_handshake(std::shared_ptr<ClientSession> session) {
    std::lock_guard<std::mutex> lock(session->mtx);

    Address new_primary_addr;
    {
        std::lock_guard<std::mutex> global_lock(g_primary_address_mutex);
        new_primary_addr = g_current_primary_address;
    }

    if (session->current_rm_ip == new_primary_addr.ip) {
        std::cerr << "[FE-RECOVERY] Session already points to the current leader. The failure might be something else." << std::endl;
        return false;
    }

    std::cout << "[FE-RECOVERY] Trying re-handshake with the new leader: " << new_primary_addr.ip << std::endl;

    int rm_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in rm_addr{};
    rm_addr.sin_family = AF_INET;
    rm_addr.sin_port = htons(new_primary_addr.port);
    inet_pton(AF_INET, new_primary_addr.ip.c_str(), &rm_addr.sin_addr);

    if (connect(rm_sock, (struct sockaddr*)&rm_addr, sizeof(rm_addr)) < 0) {
        perror("[FE-RECOVERY] Failed to connect to the new primary");
        close(rm_sock);
        return false;
    }

    Packet pkt;
    pkt.type = PACKET_TYPE_CMD;
    pkt.length = session->username.length();
    memcpy(pkt.payload, session->username.c_str(), pkt.length);
    send_packet(rm_sock, pkt);

    recv_packet(rm_sock, pkt); // Receives "OK"
    recv_packet(rm_sock, pkt); // Receives new real ports
    close(rm_sock);

    std::string new_real_ports_str(pkt.payload, pkt.length);
    std::cout << "[FE-RECOVERY] New real ports received: " << new_real_ports_str << std::endl;

    int new_real_cmd, new_real_watch, new_real_file;
    sscanf(new_real_ports_str.c_str(), "%d|%d|%d", &new_real_cmd, &new_real_watch, &new_real_file);

    std::vector<int> new_real_ports = {new_real_cmd, new_real_watch, new_real_file};

    int i = 0;
    // The keys (fake ports) of the map don't change. Only the values (real ports) are updated.
    for (auto it = session->port_map.begin(); it != session->port_map.end(); ++it) {
        it->second = new_real_ports[i++];
    }
    session->current_rm_ip = new_primary_addr.ip;

    return true;
}