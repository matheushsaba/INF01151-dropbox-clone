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
    PACKET_TYPE_ACK = 3
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

