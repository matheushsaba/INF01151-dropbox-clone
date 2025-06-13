#include "heartbeat.h"
#include "../common/packet.h"
#include "../common/common.hpp"
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>

constexpr int HB_PORT        = 5001;      // single well-known port
constexpr int HB_INTERVAL_MS = 250;       // send every 250 ms
constexpr int HB_TIMEOUT_MS  = 1500;      // 1.5 s → primary presumed dead

std::vector<int> hb_clients;
std::mutex       hb_mtx;

void primary_heartbeat_accept_loop()
{
    // Opens a socket with the default ip to receive heartbeats
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    { 
        perror("heartbeat socket"); 
        std::exit(2); 
    }

    sockaddr_in sa{};  
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(HB_PORT);

    int on = 1;  
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0 || listen(s, 8) < 0) 
    {
        perror("heartbeat bind/listen");
        std::exit(2);
    }
    std::cout << "[HB] listening on :" << HB_PORT << '\n';

    // Starts a loop that accepts backup servers that will listen to the heartbeat
    while (true) 
    {
        int cli = accept(s, nullptr, nullptr);
        if (cli < 0) 
        { 
            perror("accept"); 
            continue; 
        }
        std::lock_guard<std::mutex> lk(hb_mtx);
        hb_clients.push_back(cli);
        std::cout << "[HB] backup joined (" << cli << ")\n";
    }
}

void primary_heartbeat_ping_loop()
{
    Packet hb{}; 
    hb.type = PACKET_TYPE_HB; 
    hb.length = 0;

    // Sends the ping in a interval of time to state that the primary server is alive
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(HB_INTERVAL_MS));

        std::lock_guard<std::mutex> lk(hb_mtx);
        for (auto it = hb_clients.begin(); it != hb_clients.end(); ) 
        {
            if (!send_packet(*it, hb)) 
            {         
                // peer vanished
                std::cerr << "[HB] drop backup fd=" << *it << '\n';
                close(*it);
                it = hb_clients.erase(it);
            } else ++it;
        }
    }
}

int backup_heartbeat_connect(const std::string& ip)
{
    // Creates a socket to connect to the primary server
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    { 
        perror("socket"); return -1; 
    }

    // Connects to the server with the given ip
    sockaddr_in sa{}; 
    sa.sin_family = AF_INET;
    sa.sin_port = htons(HB_PORT);
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) 
    {
        std::cerr << "Invalid IP " << ip << '\n'; return -1;
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) 
    {
        perror("connect"); 
        return -1;
    }

    std::cout << "[HB] connected to primary " << ip << ":" << HB_PORT << '\n';

    return s;
}

void backup_heartbeat_watch_loop(int sock)
{
    using clk = std::chrono::steady_clock;
    auto last = clk::now(); // Remember the last time a ping was seen.

    while (true) 
    {
        fd_set rd{}; 
        FD_ZERO(&rd); 
        FD_SET(sock, &rd);
        timeval tv{}; 
        tv.tv_sec  = 0;
        tv.tv_usec = 200 * 1000; // 200 ms

        int rv = select(sock+1, &rd, nullptr, nullptr, &tv);
        if (rv > 0 && FD_ISSET(sock, &rd)) 
        {
            Packet pkt;
            // If recv_packet returns false, the connection was closed by the primary
            if (!recv_packet(sock, pkt)) 
            {
                std::cerr << "[HB] LOST - Primary TCP connection closed. Presumed down.\n";
                std::cerr << "[HB] LOST - Starting election\n";
                // TODO: start a new leader election
                close(sock);
                return; // Exit the function and the thread.
            }

            // Check if it's a heartbeat
            if (pkt.type == PACKET_TYPE_HB) 
            {
                // Reset the timeout timer
                last = clk::now();
            }
        }

        // This check handles the case where the primary is still connected but has become unresponsive (not sending pings)
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - last).count();
        if (age > HB_TIMEOUT_MS) 
        {
            std::cerr << "[HB] LOST - Primary unresponsive\n";
            std::cerr << "[HB] LOST - Starting election\n";
            // TODO: start a new leader election
            close(sock);
            return;
        }
    }
}

void start_primary_heartbeat_ping()
{
    // Starts a loop that accepts new connections from backups
    std::thread(primary_heartbeat_accept_loop).detach();

    // Starts a loop that pings the connected backups
    std::thread(primary_heartbeat_ping_loop).detach();
}

void start_backup_heartbeat_listener(const std::string& primary_ip)
{
    // Tries to connect to the primary server with the given ip
    int s = backup_heartbeat_connect(primary_ip);
    if (s < 0) 
    { 
        std::cerr << "Cannot start heartbeat listener\n"; 
        return; 
    }

    // Starts a watcher which will monitor the heartbeat of the primary server
    std::thread(backup_heartbeat_watch_loop, s).detach();
}
