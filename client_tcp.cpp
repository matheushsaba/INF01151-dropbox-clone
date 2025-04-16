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

    // Pega o host pelo ip e verifica se ele existe
    server = gethostbyname(hostname.c_str());
    if (!server) {
        std::cerr << "ERROR: No such host\n";
        exit(1);
    }

    // Cria um socket TCP
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    // Preenche os dados do servidor
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr = *reinterpret_cast<in_addr*>(server->h_addr);
    memset(&(serv_addr.sin_zero), 0, 8);

    // Tenta uma conexão com o servidor
    if (connect(sockfd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(sockfd);
        return;
    }

    // Tenta enviar o input digitado pelo usuário através do socket
    if (write(sockfd, cmd.c_str(), cmd.length()) < 0) {
        perror("ERROR writing to socket");
        close(sockfd);
        return;
    }

    // Tenta ler a resposta do servidor
    char buffer[256]{};
    if (read(sockfd, buffer, 255) < 0) {
        perror("ERROR reading from socket");
        close(sockfd);
        return;
    }

    // Exibe a resposta
    std::cout << "Resposta do servidor: " << buffer << std::endl;
    close(sockfd);
}

int main(int argc, char* argv[]) {
    // Solicita ao usuário o ip do servidor para se conectar como argumento
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ip>\n";
        return 1;
    }

    std::string hostname = argv[1];
    std::string input;

    // Entra num loop de envio de comandos que só é encerrado quando o usuário digitar sair
    while (true) {
        std::cout << "Comando ('sair' para encerrar): ";
        std::getline(std::cin, input);

        if (input == "sair")
            break;
        
        // Estabelece uma conexão TCP com o servidor e envia um comando
        send_command(hostname, input);
    }

    return 0;
}
