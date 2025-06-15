#pragma once
#include <cstdint>
#include <string>
#ifndef PACKET_H
#define PACKET_H

#include <cstdint>

#define MAX_PAYLOAD_SIZE 1024

enum PacketType {
    PACKET_TYPE_CMD = 1,
    PACKET_TYPE_DATA = 2,
    PACKET_TYPE_ACK = 3,
    PACKET_TYPE_END = 4,
    PACKET_TYPE_NOTIFY = 5,
    PACKET_TYPE_DELETE = 6,
    PACKET_TYPE_HB = 7,
    PACKET_TYPE_ELECT = 8,   // <— election request
    PACKET_TYPE_COORD = 9,   // <— coordinator announcement
    PACKET_TYPE_OK = 10,   // reply: “I‘m alive and stronger”
    PACKET_TYPE_PEERLIST = 11   // (comma-separated "ip1,ip2,…")
};

struct Packet {
    uint16_t type;
    uint16_t seqn;
    uint32_t total_size;
    uint16_t length;
    char payload[MAX_PAYLOAD_SIZE];
};

// Serialização / Desserialização
int serialize_packet(const Packet& pkt, char* buffer, size_t buffer_size);
int deserialize_packet(const char* buffer, size_t buffer_size, Packet& pkt);

// Envio / Recebimento via socket
bool send_packet(int sockfd, const Packet& pkt);
bool recv_packet(int sockfd, Packet& pkt);

#endif

