// client_tcp.cpp
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

constexpr int PORT = 4000;

void send_command(const std::string& hostname, const std::string& cmd) {
    int sockfd;
    sockaddr_in serv_addr{};
    hostent* server;

    server = gethostbyname(hostname.c_str());
    if (!server) {
        std::cerr << "ERROR: No such host\n";
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr = *reinterpret_cast<in_addr*>(server->h_addr);
    memset(&(serv_addr.sin_zero), 0, 8);

    if (connect(sockfd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(sockfd);
        return;
    }

    if (write(sockfd, cmd.c_str(), cmd.length()) < 0) {
        perror("ERROR writing to socket");
        close(sockfd);
        return;
    }

    char buffer[256]{};
    if (read(sockfd, buffer, 255) < 0) {
        perror("ERROR reading from socket");
        close(sockfd);
        return;
    }

    std::cout << "Resposta do servidor: " << buffer << std::endl;
    close(sockfd);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ip>\n";
        return 1;
    }

    std::string hostname = argv[1];
    std::string input;

    while (true) {
        std::cout << "Comando ('sair' para encerrar): ";
        std::getline(std::cin, input);

        if (input == "sair")
            break;

        send_command(hostname, input);
    }

    return 0;
}
