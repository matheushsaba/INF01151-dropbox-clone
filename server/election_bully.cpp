#include "election_bully.h"
#include "heartbeat.h"
#include "../common/packet.h"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <cstring>
#include <iostream>

namespace {

constexpr int ELECTION_PORT = 5002;       // UDP port for election messages
constexpr int OK_WAIT_MS = 1000;          // wait for OK from better candidate

static uint32_t     g_my_pid;             // my numeric priority
static std::string  g_my_ip;              // dotted quad
static std::vector<std::string> g_peers;  // runtime list of backup servers
static int          g_sock;               // UDP socket

// Helper function to send a packet to a specific IP address
void send_election_packet(const std::string& ip, uint8_t type, uint32_t pid_payload)
{
    // Create a structure to hold the destination address information
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port   = htons(ELECTION_PORT);
    inet_pton(AF_INET, ip.c_str(), &to.sin_addr);

    // Create a new packet to be sent. It can be "Election", "Ok" or "Coordinator"
    Packet p{};
    p.type = type;
    p.length = sizeof(pid_payload);
    std::memcpy(p.payload, &pid_payload, sizeof(pid_payload));

    // Send the packet over the global UDP socket
    sendto(g_sock, &p, sizeof(p.type)+sizeof(p.length)+p.length, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

// This function runs in a dedicated thread to listen for and handle incoming election-related messages.
void listener()
{
    Packet pkt;                              
    sockaddr_in sender_address{};                       
    socklen_t len = sizeof(sender_address);             
    uint32_t sender_pid;                     

    // Start an infinite loop to continuously listen for messages on the election socket
    while (true) 
    {
        // Block and wait to receive a UDP packet, storing the sender's address
        int n = recvfrom(g_sock, &pkt, sizeof(pkt), 0, reinterpret_cast<sockaddr*>(&sender_address), &len);
        if (n < 4) 
        {
            // If the received data is too small to be a valid packet, ignore it and continue
            continue;
        }

        char ipbuf[INET_ADDRSTRLEN];                 // Declare a buffer to hold the sender's IP address in string format.
        inet_ntop(AF_INET, &sender_address.sin_addr, ipbuf, sizeof ipbuf); // Convert the sender's binary IP address to a human-readable string.

        std::memcpy(&sender_pid, pkt.payload, sizeof(sender_pid)); // Copy the sender's PID from the packet's payload into the sender_pid variable.

        if (pkt.type == PACKET_TYPE_ELECT) 
        {
            // If current PID is greater than the sender's, this server should be elected
            // as the new leader
            if (g_my_pid > sender_pid) 
            {
                // Send an "OK" message back to the sender to stop their election attempt
                send_election_packet(ipbuf, PACKET_TYPE_OK, g_my_pid);
                // Start this process own election process since it is a better candidate
                bully_start();                       
            }
        }
        else if (pkt.type == PACKET_TYPE_COORD) 
        {
            // Print a message announcing the new coordinator (leader) and its PID/IP.
            std::cout << "[ELECT] New coordinator: PID " << sender_pid << " (" << ipbuf << ")\n";
            // The election is over; this server will remain a backup.
        }
        else if (pkt.type == PACKET_TYPE_PEERLIST) 
        {
            std::string csv(pkt.payload, pkt.length); 
            std::vector<std::string> lst;             
            size_t pos = 0;                           

            while ((pos = csv.find(',')) != std::string::npos) 
            { 
                // Loop through the CSV string, splitting it by commas to extract each IP
                lst.push_back(csv.substr(0, pos));
                csv.erase(0, pos + 1);
            }

            if (!csv.empty()) 
            {
                // Add the last remaining IP to the list
                lst.push_back(csv);
            }

            // Update the global list of peers with the new list
            bully_set_peer_list(lst);                 

            std::cout << "[ELECT] Updated peer list (" << lst.size() << ")\n";
        }
    }
}

// This function implements the core logic for a server to start and potentially win a leader election
void do_election()
{
    std::cout << "[ELECT] Starting election, my PID=" << g_my_pid << '\n';

    // Iterate through all known peer IP addresses to send them an election message.
    for (auto& ip : g_peers)
    {
        // Avoid sending messages to itself
        if (ip != g_my_ip)
        {
            // Send an ELECTION packet containing this server's PID to the peer
            send_election_packet(ip, PACKET_TYPE_ELECT, g_my_pid);
        }
    }

    // Wait OK from any bigger pid
    // Initialize a file descriptor set to monitor the election socket for incoming data
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(g_sock,&rd);
    // Define the maximum time to wait for a response from a peer with a bigger pid
    timeval tv{ OK_WAIT_MS/1000, (OK_WAIT_MS%1000)*1000 };

    // Wait on the socket for an incoming message, with a timeout.
    if (select(g_sock+1, &rd, nullptr, nullptr, &tv) <= 0) 
    {
        // If select() times out (returns 0) or errors (returns <0), no peer with a bigger pid
        // has replied, so this server wins
        std::cout << "[ELECT] Won election, broadcasting coordinator\n";

        // Iterate through all peers to announce the new leader
        for (auto& ip : g_peers)
        {
            // Do not send the coordinator message to itself
            if (ip != g_my_ip)
            {
                // Send a COORDINATOR packet to inform the peer that this server is the new leader.
                send_election_packet(ip, PACKET_TYPE_COORD, g_my_pid);
            }
        }

        // Transition this server's role from backup to primary
        promote_to_primary();
    }

    // If select() returns > 0, an "OK" was received, so this server backs down and waits for a new COORDINATOR.
}


} // unnamed namespace

// This function is called only when the first server is initialized via command terminal on server_tcp
// Initializes the Bully election module with the server's identity and network configuration
void bully_init(const std::string& my_ip)
{
    // Set this server's pid on a global variable to be used in the election
    g_my_pid = static_cast<uint32_t>(::getpid());
    // Store this server's own IP address, passed from the command line
    g_my_ip = my_ip;

    // Create a UDP socket for sending and receiving all election-related messages
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in this_server_address{};
    this_server_address.sin_family=AF_INET;
    this_server_address.sin_port=htons(ELECTION_PORT);
    this_server_address.sin_addr.s_addr = INADDR_ANY;
    bind(g_sock, reinterpret_cast<sockaddr*>(&this_server_address), sizeof(this_server_address));

    // Start the listener function in a new, detached thread
    std::thread(listener).detach();
}


// This function is called inside the listener() function on this file
// Triggers a new leader election process to run in a background thread
void bully_start()
{
    std::thread(do_election).detach();
}

// Updates the internal list of peer IPs to run elections
void bully_set_peer_list(const std::vector<std::string>& peers)
{
    g_peers = peers;
}
