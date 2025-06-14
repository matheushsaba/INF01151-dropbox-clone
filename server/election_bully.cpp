#include "election_bully.h"
#include "heartbeat.h"          // promote_to_primary()
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

// ---------- constants -------------------------------------------------------
constexpr int E_PORT     = 5002;          // UDP port for election messages
constexpr int OK_WAIT_MS = 1000;          // wait for OK from stronger peer

// ---------- globals ---------------------------------------------------------
static uint32_t     g_my_pid;             // my numeric priority
static std::string  g_my_ip;              // dotted quad
static std::vector<std::string> g_peers;  // runtime list of backup servers
static int          g_sock;               // UDP socket

// ---------- wire helpers ----------------------------------------------------
void tx(const std::string& ip, uint8_t type, uint32_t pid_payload)
{
    sockaddr_in to{}; to.sin_family = AF_INET;
    to.sin_port   = htons(E_PORT);
    inet_pton(AF_INET, ip.c_str(), &to.sin_addr);

    Packet p{};  p.type   = type;
    p.length = sizeof(pid_payload);
    std::memcpy(p.payload, &pid_payload, sizeof(pid_payload));

    sendto(g_sock, &p, sizeof(p.type)+sizeof(p.length)+p.length,
           0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
}

// ---------- listener thread -------------------------------------------------
void listener()
{
    Packet pkt;
    sockaddr_in frm{};
    socklen_t    len = sizeof(frm);
    uint32_t     sender_pid;

    while (true) {
        int n = recvfrom(g_sock, &pkt, sizeof(pkt), 0,
                         reinterpret_cast<sockaddr*>(&frm), &len);
        if (n < 4) continue;                         // ignore junk

        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &frm.sin_addr, ipbuf, sizeof ipbuf);

        std::memcpy(&sender_pid, pkt.payload, sizeof(sender_pid));

        if (pkt.type == PACKET_TYPE_ELECT) {
            if (g_my_pid > sender_pid) {             // I'm “stronger”
                tx(ipbuf, PACKET_TYPE_OK, g_my_pid); // send OK
                bully_start();                       // start my own election
            }
        }
        else if (pkt.type == PACKET_TYPE_COORD) {
            std::cout << "[ELECT] New coordinator: PID " << sender_pid
                      << " (" << ipbuf << ")\n";
            // Election finished — remain backup if not me
        }
        else if (pkt.type == PACKET_TYPE_PEERLIST) {
            std::string csv(pkt.payload, pkt.length);
            std::vector<std::string> lst;
            size_t pos = 0;
            while ((pos = csv.find(',')) != std::string::npos) {
                lst.push_back(csv.substr(0, pos));
                csv.erase(0, pos + 1);
            }
            if (!csv.empty()) lst.push_back(csv);
            bully_set_peer_list(lst);
            std::cout << "[ELECT] Updated peer list (" << lst.size() << ")\n";
        }
    }
}

// ---------- internal election procedure ------------------------------------
void do_election()
{
    std::cout << "[ELECT] Starting election, my PID=" << g_my_pid << '\n';

    // (1) broadcast ELECTION
    for (auto& ip : g_peers)
        if (ip != g_my_ip)
            tx(ip, PACKET_TYPE_ELECT, g_my_pid);

    // (2) wait OK from any stronger pid
    fd_set rd; FD_ZERO(&rd); FD_SET(g_sock,&rd);
    timeval tv{ OK_WAIT_MS/1000, (OK_WAIT_MS%1000)*1000 };

    if (select(g_sock+1, &rd, nullptr, nullptr, &tv) <= 0) {
        // no OK → I'm the highest → broadcast COORD
        std::cout << "[ELECT] Won election, broadcasting coordinator\n";
        for (auto& ip : g_peers)
            if (ip != g_my_ip)
                tx(ip, PACKET_TYPE_COORD, g_my_pid);

        promote_to_primary();               // switch roles
    }
    // else some stronger process will finish and send COORD
}

} // unnamed namespace

/* ---------- public API ---------------------------------------------------- */

void bully_init(const std::string& my_ip)
{
    g_my_pid = static_cast<uint32_t>(::getpid());
    g_my_ip  = my_ip;

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{}; me.sin_family=AF_INET; me.sin_port=htons(E_PORT);
    me.sin_addr.s_addr = INADDR_ANY;
    bind(g_sock, reinterpret_cast<sockaddr*>(&me), sizeof(me));

    std::thread(listener).detach();
}

void bully_start()
{
    std::thread(do_election).detach();   // run asynchronously
}

void bully_set_peer_list(const std::vector<std::string>& peers)    // <── NEW
{
    g_peers = peers;
}
