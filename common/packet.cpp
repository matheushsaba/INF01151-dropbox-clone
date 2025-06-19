#include "packet.h"
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <filesystem>

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
    int n = recv(sockfd, header, header_size, MSG_WAITALL);
    if (n != header_size) {
        if (n == 0) {
            // Conexão fechada pelo outro lado. Isso é esperado quando um servidor cai.
            // Não imprimimos nada para manter o terminal do cliente limpo.
        } else if (n > 0) {
            // Recebeu menos dados que o esperado, um erro de protocolo.
            std::cerr << "recv: Incomplete packet header received.\n";
        } else {
            // n < 0, um erro real de socket. Aqui o perror é útil.
            perror("recv_packet header");
        }
        return false;
    }

    uint16_t length;
    std::memcpy(&length, header + header_size - sizeof(length), sizeof(length));

    if (length > MAX_PAYLOAD_SIZE) {
        return false;
    }

    char buffer[1500];
    std::memcpy(buffer, header, header_size);
    if (length > 0) {
        n = recv(sockfd, buffer + header_size, length, MSG_WAITALL);

        // A verificação de erro agora está aqui dentro, aplicando a mesma lógica do cabeçalho
        if (n != length) {
            if (n >= 0) {
                // Caso n=0 (conexão fechada) ou 0<n<length (pacote incompleto).
                // Ambos indicam que a conexão foi perdida durante a transferência do payload.
                // Mantemos silencioso para o usuário.
            } else {
                // n<0 indica um erro real de sistema.
                perror("recv_packet (payload)");
            }
            return false; // Falha na recepção do pacote
        }
    }

    int dsz = deserialize_packet(buffer, header_size + length, pkt);
    if (dsz <= 0) {
        return false;
    }
    return true;
}
