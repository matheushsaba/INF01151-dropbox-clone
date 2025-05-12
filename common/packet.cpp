#include "packet.h"
#include <cstring>
#include <sys/socket.h>

int serialize_packet(const Packet& pkt, char* buffer, size_t buffer_size) {
    if (buffer_size < sizeof(pkt.type) + sizeof(pkt.seqn) + sizeof(pkt.total_size) + sizeof(pkt.length) + pkt.length) {
        return -1;
    }

    size_t offset = 0;
    std::memcpy(buffer + offset, &pkt.type, sizeof(pkt.type)); offset += sizeof(pkt.type);
    std::memcpy(buffer + offset, &pkt.seqn, sizeof(pkt.seqn)); offset += sizeof(pkt.seqn);
    std::memcpy(buffer + offset, &pkt.total_size, sizeof(pkt.total_size)); offset += sizeof(pkt.total_size);
    std::memcpy(buffer + offset, &pkt.length, sizeof(pkt.length)); offset += sizeof(pkt.length);
    std::memcpy(buffer + offset, pkt.payload, pkt.length);

    return offset + pkt.length;
}

int deserialize_packet(const char* buffer, size_t buffer_size, Packet& pkt) {
    size_t offset = 0;
    if (buffer_size < sizeof(pkt.type) + sizeof(pkt.seqn) + sizeof(pkt.total_size) + sizeof(pkt.length)) return -1;

    std::memcpy(&pkt.type, buffer + offset, sizeof(pkt.type)); offset += sizeof(pkt.type);
    std::memcpy(&pkt.seqn, buffer + offset, sizeof(pkt.seqn)); offset += sizeof(pkt.seqn);
    std::memcpy(&pkt.total_size, buffer + offset, sizeof(pkt.total_size)); offset += sizeof(pkt.total_size);
    std::memcpy(&pkt.length, buffer + offset, sizeof(pkt.length)); offset += sizeof(pkt.length);

    if (pkt.length > MAX_PAYLOAD_SIZE || buffer_size < offset + pkt.length) return -1;

    std::memcpy(pkt.payload, buffer + offset, pkt.length);
    return offset + pkt.length;
}

bool send_packet(int sockfd, const Packet& pkt) {
    char buffer[1500];
    int size = serialize_packet(pkt, buffer, sizeof(buffer));
    if (size < 0) return false;
    return send(sockfd, buffer, size, 0) == size;
}

bool recv_packet(int sockfd, Packet& pkt) {
    char header[10];
    int header_size = sizeof(pkt.type) + sizeof(pkt.seqn) + sizeof(pkt.total_size) + sizeof(pkt.length);
    if (recv(sockfd, header, header_size, MSG_WAITALL) != header_size) return false;

    uint16_t length;
    std::memcpy(&length, header + header_size - sizeof(length), sizeof(length));

    if (length > MAX_PAYLOAD_SIZE) return false;

    char buffer[1500];
    std::memcpy(buffer, header, header_size);
    if (recv(sockfd, buffer + header_size, length, MSG_WAITALL) != length) return false;

    return deserialize_packet(buffer, header_size + length, pkt) > 0;
}
