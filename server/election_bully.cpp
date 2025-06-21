#include "election_bully.h"
#include "heartbeat.h"
#include "../common/packet.h"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <random>

namespace {

constexpr int ELECTION_PORT = 5002;      // UDP port for election messages
constexpr int OK_WAIT_MS    = 1000;      // wait for OK from better candidate

static std::mutex               g_election_mutex;
static std::condition_variable  g_election_cv;
static bool                     g_election_in_progress = false;
static bool                     g_received_ok = false;

static uint32_t                 g_my_pid; // my numeric priority
static std::string              g_my_ip;  // dotted quad
static std::vector<std::string> g_peers;  // runtime list of backup servers
static int          g_sock;               // TCP listening socket

// Helper to extract the last octet from an IP address string.
uint8_t get_last_ip_octet(const std::string& ip)
{
    // Find last number of the ip and convert it
    size_t last_dot = ip.find_last_of('.');
    if (last_dot != std::string::npos) 
    {
        const std::string octet_str = ip.substr(last_dot + 1);
        char* end;
        long octet = std::strtol(octet_str.c_str(), &end, 10);

        // Check if conversion was successful, the entire string was consumed, and the value is in range
        if (end != octet_str.c_str() && *end == '\0' && octet >= 0 && octet <= 255) 
        {
            return static_cast<uint8_t>(octet);
        }
    }

    // If the IP format is invalid, parsing fails, or the octet is out of range, generate a random number
    std::cerr << "[ELECT] Warning: Invalid or out-of-range octet in IP '" << ip
              << "'. Using a random number as a tie-breaker.\n";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);

    return static_cast<uint8_t>(distrib(gen));
}

// Helper function to send a packet to a specific IP address
void send_election_packet(const std::string& ip, uint8_t type, uint32_t pid_payload)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        // This can be noisy, and failure is expected if a peer is down.
        // perror("[ELECT] socket creation failed");
        return;
    }

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port   = htons(ELECTION_PORT);
    if (inet_pton(AF_INET, ip.c_str(), &to.sin_addr) <= 0) {
        // std::cerr << "[ELECT] Invalid address " << ip << '\n';
        close(sock);
        return;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0) {
        // This is expected if a peer is down, so don't print perror unless debugging.
        close(sock);
        return;
    }

    Packet p{};
    p.type = type;
    p.length = sizeof(pid_payload);
    std::memcpy(p.payload, &pid_payload, sizeof(pid_payload));

    send_packet(sock, p);
    close(sock);
}

void handle_election_connection(int sock, sockaddr_in addr)
{
    Packet pkt;
    if (!recv_packet(sock, pkt)) {
        close(sock);
        return;
    }
    close(sock); // We're done with this connection.

    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof ipbuf);

    uint32_t sender_pid;
    if (pkt.length < sizeof(sender_pid)) {
        return; // Malformed packet
    }
    std::memcpy(&sender_pid, pkt.payload, sizeof(sender_pid));

    if (pkt.type == PACKET_TYPE_ELECT) {
        if (g_my_pid > sender_pid) {
            send_election_packet(ipbuf, PACKET_TYPE_OK, g_my_pid);
            bully_start();
        }
    } else if (pkt.type == PACKET_TYPE_OK) {
        std::lock_guard<std::mutex> lock(g_election_mutex);
        g_received_ok = true;
        g_election_cv.notify_one();
    } else if (pkt.type == PACKET_TYPE_COORD) {
        std::cout << "[ELECT] New coordinator: PID " << sender_pid << " (" << ipbuf << ")\n";
        {
            std::lock_guard<std::mutex> lock(g_election_mutex);
            g_election_in_progress = false;
            g_received_ok = false;
        }
        start_backup_heartbeat_listener(ipbuf);
    }
}

// This function implements the core logic for a server to start and potentially win a leader election
void do_election()
{
    std::cout << "[ELECT] Starting election, my PID=" << g_my_pid << '\n';

    std::vector<std::string> current_peers;
    {
        // Lock the mutex to safely copy the shared g_peers vector. This prevents
        // data races if the list is being updated by another thread.
        std::lock_guard<std::mutex> lock(g_election_mutex);
        current_peers = g_peers;
    }

    // Iterate through the local copy of peer IPs to send them an election message.
    for (auto& ip : current_peers)
    {
        // Avoid sending messages to itself
        if (ip != g_my_ip)
        {
            // Send an ELECTION packet containing this server's PID to the peer
            send_election_packet(ip, PACKET_TYPE_ELECT, g_my_pid);
        }
    }

    // Wait for a potential "OK" response, which would be caught by the listener.
    // The listener will set g_received_ok and notify our condition variable.
    std::unique_lock<std::mutex> lock(g_election_mutex);
    g_received_ok = false; // Reset for this election attempt.

    // Wait for an OK from a higher-PID peer. `wait_for` returns true if notified, false on timeout.
    if (g_election_cv.wait_for(lock, std::chrono::milliseconds(OK_WAIT_MS), []{ return g_received_ok; }))
    {
        // We were woken up because g_received_ok became true. We lost the election.
        std::cout << "[ELECT] Received OK, backing down.\n";
        g_election_in_progress = false; // Allow a new election to start.
        return; // End this election thread.
    }

    // Timed out. Before declaring victory, we MUST re-check if a COORD message
    // arrived and cancelled the election while we were waiting. This fixes the race condition.
    if (!g_election_in_progress) {
        std::cout << "[ELECT] Coordinator announced during our election, backing down.\n";
        return; // A new leader was chosen by others while we were waiting.
    }

    // If we reach here, we timed out and no other coordinator was announced. We are the winner.
    lock.unlock(); // It's now safe to release the lock before broadcasting and promoting.

    std::cout << "[ELECT] Won election, broadcasting coordinator\n";

    // Iterate through all peers to announce our new leadership.
    for (auto& ip : g_peers) {
        if (ip != g_my_ip) {
            send_election_packet(ip, PACKET_TYPE_COORD, g_my_pid);
        }
    }

    // Transition this server's role from backup to primary.
    promote_to_primary();
}

// This function runs in a dedicated thread to listen for and handle incoming election-related messages.
void listener()
{
    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(g_sock, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_sock < 0) {
            perror("[ELECT] accept failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        // Pass client_addr by value to the thread to avoid data races on the stack variable.
        std::thread(handle_election_connection, client_sock, client_addr).detach();
    }
}

} // unnamed namespace

// This function is called only when the first server is initialized via command terminal on server_tcp
// Initializes the Bully election module with the server's identity and network configuration
void bully_init(const std::string& my_ip)
{
    // Get the process ID as the base for our priority.
    uint32_t base_pid = static_cast<uint32_t>(::getpid());
    // Get the last number of the IP address to use as a tie-breaker
    uint8_t last_octet = get_last_ip_octet(my_ip);

    // Combine the base PID and the last IP octet to create a unique id
    g_my_pid = base_pid + last_octet;
    // Store this server's own IP address, passed from the command line
    g_my_ip = my_ip;

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        perror("[ELECT] socket creation failed");
        std::exit(EXIT_FAILURE);
    }

    int on = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in this_server_address{};
    this_server_address.sin_family = AF_INET;
    this_server_address.sin_port = htons(ELECTION_PORT);
    this_server_address.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_sock, reinterpret_cast<sockaddr*>(&this_server_address), sizeof(this_server_address)) < 0) {
        perror("[ELECT] bind failed");
        close(g_sock);
        std::exit(EXIT_FAILURE);
    }

    if (listen(g_sock, 10) < 0) {
        perror("[ELECT] listen failed");
        close(g_sock);
        std::exit(EXIT_FAILURE);
    }

    std::cout << "[ELECT] Initialized with priority PID=" << g_my_pid
              << " (base_pid=" << base_pid << ", ip_octet=" << static_cast<int>(last_octet) << ")\n";
    std::cout << "[ELECT] Listening for TCP election messages on port " << ELECTION_PORT << '\n';

    // Start the listener function in a new, detached thread
    std::thread(listener).detach();
}


// This function is called inside the listener() function on this file
// Triggers a new leader election process to run in a background thread
void bully_start()
{
    std::lock_guard<std::mutex> lock(g_election_mutex);
    if (g_election_in_progress) {
        return; // An election is already in progress.
    }
    g_election_in_progress = true;
    std::thread(do_election).detach();
}

// Updates the internal list of peer IPs to run elections
void bully_set_peer_list(const std::vector<std::string>& peers)
{
    // Lock the mutex to ensure thread-safe updates to the shared peer list.
    std::lock_guard<std::mutex> lock(g_election_mutex);
    g_peers = peers;
}
