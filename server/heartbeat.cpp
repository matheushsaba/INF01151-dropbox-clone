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
#include "election_bully.h"
#include <algorithm>

constexpr int HEARTBEAT_PORT        = 3002;      // single well-known port
constexpr int HB_INTERVAL_MS = 250;       // send every 250 ms
constexpr int HB_TIMEOUT_MS  = 1500;      // 1.5 s → primary presumed dead

std::vector<int> hb_clients;
std::mutex       hb_mtx;

static std::string local_ip;      // set once in main()
static std::vector<std::string> peer_ips;   // backups we know

void hb_broadcast_peerlist()
{
    // Create an empty string that will store the comma-separated list of peer IP addresses
    std::string csv;
    // Format the csv
    for (auto& ip : peer_ips) 
    {
        if (!csv.empty()) csv += ',';
        csv += ip;
    }

    Packet pl{}; 
    pl.type = PACKET_TYPE_PEERLIST;
    pl.length = std::min((int)csv.size(), MAX_PAYLOAD_SIZE); // Ensure payload fits within MAX_PAYLOAD_SIZE
    memcpy(pl.payload, csv.data(), pl.length);

    // Acquire a lock on the heartbeat mutex to avoid race conditions on hb_clients vector
    std::lock_guard<std::mutex> lk(hb_mtx);
    // Sends packets with the peer list to all backup servers
    for (int fd : hb_clients) 
    {
        send_packet(fd, pl);
    }
}

// Creates a socket for accepting connections from backup servers
void primary_heartbeat_accept_loop()
{
    // Opens a TCP socket where the primary will accept connections
    // and send heartbeats to the backups
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    { 
        perror("heartbeat socket"); 
        std::exit(2); 
    }

    sockaddr_in sa{};  
    sa.sin_family = AF_INET;
    // INADDR_ANY binds the socket to 0.0.0.0, which is a wildcard to 
    // accept connections on any IP address
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(HEARTBEAT_PORT);

    // Set a socket option to allow reusing the address
    int on = 1;  
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // Binds the socket to the specified address and port, and then start listening for connections.
    if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0 || listen(s, 8) < 0) 
    {
        perror("heartbeat bind/listen");
        std::exit(2);
    }
    
    std::cout << "[HB] listening on :" << HEARTBEAT_PORT << '\n';

    // Starts a loop that accepts backup servers that will listen to the heartbeat
    while (true) 
    {
        sockaddr_in addr{};
        socklen_t   alen = sizeof(addr);

        // Accepts a backup server who wants to listen to the heartbeat
        int cli = accept(s, reinterpret_cast<sockaddr*>(&addr), &alen);
        if (cli < 0) 
        { 
            perror("accept"); 
            continue; 
        }
        
        // Get the IP address of the connected backup server
        // The address contains the real ip of the backu´p server connecting to this one
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof ipbuf);
        std::string ip(ipbuf);

        // Flag to determine if the list of peers needs to be sent to all backup servers
        bool should_broadcast = false;
        
        // Critical section: Access shared data (hb_clients, peer_ips) in a thread-safe manner
        {
            std::lock_guard<std::mutex> lk(hb_mtx);
            hb_clients.push_back(cli);

            // Check if the peer is new to the list
            if (std::find(peer_ips.begin(), peer_ips.end(), ip) == peer_ips.end()) 
            {
                peer_ips.push_back(ip);
                should_broadcast = true; // Set a flag to broadcast after releasing the lock
            }
        } // The lock is released here

        std::cout << "[HB] backup joined " << ip << " (fd=" << cli << ")\n";

        // Call the broadcast function outside the lock to prevent deadlock
        // This will send the updated peerlist to all backups
        if (should_broadcast) 
        {
            hb_broadcast_peerlist();
        }
    }
}

// Sends heartbeats to state that the primary server is still alive
void primary_heartbeat_ping_loop()
{
    Packet hb{}; 
    hb.type = PACKET_TYPE_HB; 
    hb.length = 0;

    // Sends the ping in a interval of time to state that the primary server is alive
    while (true) 
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(HB_INTERVAL_MS));

        // hb_clients is a shared resource and needs to be protected through a lock
        // otherwise it may be accessed at the same time through hb_broadcast_peerlist 
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

// Connects to the primary server socket that sends heartbeats
int backup_heartbeat_connect(const std::string& primary_server_ip)
{
    // Creates a TCP socket to connect to the primary server
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    { 
        perror("socket"); return -1; 
    }

    sockaddr_in server_address{}; 
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(HEARTBEAT_PORT);
    // Attemps to convert the given ip to binary network format
    if (inet_pton(AF_INET, primary_server_ip.c_str(), &server_address.sin_addr) != 1) 
    {
        std::cerr << "Invalid IP " << primary_server_ip << '\n'; 
        return -1;
    }
    
    // Connects to the primary server with the given ip
    if (connect(s, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0) 
    {
        perror("connect"); 
        return -1;
    }

    std::cout << "[HB] connected to primary " << primary_server_ip << ":" << HEARTBEAT_PORT << '\n';

    return s;
}

// 
void backup_heartbeat_watch_loop(int sock)
{
    using clk = std::chrono::steady_clock;
    auto last = clk::now(); // Record the time when the last heartbeat was received

    // Start an infinite loop to monitor the primary server heartbeat
        while (g_role.load() == ROLE_BACKUP)     {
        fd_set rd{}; // Initialize a file descriptor set for the select() call
        FD_ZERO(&rd); // Clear the set
        FD_SET(sock, &rd); // Add the heartbeat socket to the set to monitor for readability
        timeval tv{}; // Initialize a timeval structure for the select() timeout
        tv.tv_sec  = 0; // timeout == 0 s
        tv.tv_usec = 200 * 1000; // timeout == 200 ms

        // Wait for activity on the socket or until the timeout expires
        // rv > 0 means data is available, rv = 0 means timeout, rv < 0 means error
        int rv = select(sock+1, &rd, nullptr, nullptr, &tv);
        if (rv > 0 && FD_ISSET(sock, &rd)) 
        {
            Packet pkt;
            // If recv_packet returns false, the connection was closed by the primary
            if (!recv_packet(sock, pkt)) 
            {
                std::cout << "[HB] current role :" << g_role.load() << '\n';
                bully_start();
                return; // Exit the function and the thread.
            }

            // Check if it's a heartbeat
            if (pkt.type == PACKET_TYPE_HB) 
            {
                // Reset the last seen timestamp
                last = clk::now();
            }

            if (pkt.type == PACKET_TYPE_PEERLIST) 
            {
                // Convert the csv to a string
                std::string csv(pkt.payload, pkt.length);
                std::vector<std::string> lst;
                size_t pos;
                // Loop through the csv, splitting it on the commas
                while ((pos = csv.find(',')) != std::string::npos) 
                {
                    lst.push_back(csv.substr(0,pos));
                    csv.erase(0,pos+1);
                }
                // Add the last IP address in the CSV string
                if (!csv.empty()) 
                {
                    lst.push_back(csv);
                }
                // Update the election module with the new list of peers
                bully_set_peer_list(lst);

                // Skip updating 'last' for peerlist packets, as they aren't heartbeats
                continue;
            }
        }

        // Calculate the time elapsed since the last heartbeat was received
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - last).count();
        if (age > HB_TIMEOUT_MS) 
        {
            std::cerr << "[HB] LOST - Primary unresponsive\n";
            bully_start();
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
    constexpr int MAX_RETRIES = 5;
    constexpr int RETRY_DELAY_S = 4;
    int s = -1;

    // The primary might have just been elected and needs a moment to set up its listener.
    // We'll retry connecting a few times before giving up.
    for (int i = 0; i < MAX_RETRIES; ++i) {
        s = backup_heartbeat_connect(primary_ip);
        if (s >= 0) 
        {
            break; // Success!
        }

        std::cerr << "[HB] Failed to connect to new primary. Retrying in " 
                  << RETRY_DELAY_S << "s... (" << i + 1 << "/" << MAX_RETRIES << ")\n";
        std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY_S));
    }

    if (s < 0) 
    { 
        std::cerr << "[HB] Cannot start heartbeat listener after " << MAX_RETRIES << " retries. Assuming primary is down.\n";
        bully_start(); // The announced primary is unreachable, so start a new election.
        return; 
    }

    // Starts a watcher which will monitor the heartbeat of the primary server
    std::thread(backup_heartbeat_watch_loop, s).detach();
}
