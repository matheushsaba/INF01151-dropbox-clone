// server_tcp.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

constexpr int PORT = 4000;
std::mutex file_mutex; // Usamos mutex para sincronizar threads em processos diferentes

void handle_client(int client_socket) {
    char buffer[256]{};

    // Tenta ler a mensagem do cliente
    int n = read(client_socket, buffer, 255);
    if (n <= 0) {
        perror("ERROR reading from socket");
        close(client_socket);
        return;
    }

    // Exibe a mensagem do cliente
    std::cout << "Received: " << buffer << std::endl;

    {
        // Protege a escrita com mutex
        std::lock_guard<std::mutex> lock(file_mutex);

        // Envia uma resposta ao cliente
        const char* reply = "Servidor recebeu sua mensagem.\n";
        if (write(client_socket, reply, strlen(reply)) < 0) {
            perror("ERROR writing to socket");
        }
    }

    // Encerra a conexão com o cliente
    close(client_socket);
}

int main() {
    int server_fd, client_fd;
    sockaddr_in serv_addr{}, cli_addr{};
    socklen_t clilen = sizeof(cli_addr);

    // Cria um socket TCP no servidor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR opening socket");
        return 1;
    }

    // Preenche os dados do servidor
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);
    memset(&(serv_addr.sin_zero), 0, 8);

    // Associa o socket à porta e endereço
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        return 1;
    }

    // Escuta qualquer solicitação de conexão
    listen(server_fd, 5);
    std::cout << "Servidor aguardando conexões na porta " << PORT << "...\n";

    while (true) {
        // Aceita uma nova conexão com um cliente
        client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&cli_addr), &clilen);
        if (client_fd < 0) {
            perror("ERROR on accept");
            continue;
        }

        // Aloca uma thread para esse cliente, passando a função handle_client para essa thread
        std::thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}
