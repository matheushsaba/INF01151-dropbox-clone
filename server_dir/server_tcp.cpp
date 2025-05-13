// server_tcp.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <filesystem>
#include <sys/socket.h>
#include "../common/packet.h" 
#include <fstream>  // Para std::ofstream


// constexpr int PORT = 4000;
// std::mutex file_mutex; // Usamos mutex para sincronizar threads em processos diferentes

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;
std::mutex file_mutex;  // Global mutex used to synchronize access to shared resources (e.g., files)

// Function to deal with simple command messages
void handle_command_client(int client_socket) {
    Packet pkt;

 if (!recv_packet(client_socket, pkt)) {
        std::cerr << "Erro ao receber pacote de comando.\n";
        close(client_socket);
        return;
    }
    std::string command(pkt.payload, pkt.length);
    std::cout << "Command received: " << command << std::endl;

  Packet response;
    response.type = PACKET_TYPE_ACK;
    response.seqn = pkt.seqn;
    response.total_size = 0;

    {
        std::lock_guard<std::mutex> lock(file_mutex);

        if (command.rfind("list_server", 0) == 0) {
            std::string username;
            size_t pos = command.find('|');
            if (pos != std::string::npos) {
                username = command.substr(pos + 1);
            }

            std::string user_dir = "server_storage/" + username;
            std::string response_data;

            if (std::filesystem::exists(user_dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(user_dir)) {
                    if (std::filesystem::is_regular_file(entry)) {
                        response_data += entry.path().filename().string() + "\n";
                    }
                }
                if (response_data.empty()) {
                    response_data = "(nenhum arquivo encontrado)";
                }
            } else {
                response_data = "(diretório do usuário não encontrado)";
            }

            response.length = std::min((int)response_data.size(), MAX_PAYLOAD_SIZE);
            std::memcpy(response.payload, response_data.c_str(), response.length);
        } else {
            const char* reply = "Comando desconhecido ou não implementado.";
            response.length = strlen(reply);
            std::memcpy(response.payload, reply, response.length);
        }
    }

    send_packet(client_socket, response);
    close(client_socket);
}

// Listens for updates, could monitor for file changes
void handle_watcher_client(int client_socket) {
    char buffer[256]{};

    while (true) {
        int n = read(client_socket, buffer, 255);
        if (n <= 0) {
            perror("ERROR reading from watcher socket");
            break;
        }

        std::cout << "Watcher update: " << buffer << std::endl;
        
        // Here you would implement the logic to check for changes
        // and notify the client if there are any files to sync
    }

    close(client_socket);
}

// Deals with file transfer logic (upload)
void handle_file_client(int client_socket) {
    // Receber o nome do arquivo (primeiro pacote)
    Packet pkt;
    if (!recv_packet(client_socket, pkt)) {
        std::cerr << "Erro ao receber nome do arquivo.\n";
        close(client_socket);
        return;
    }

    std::string filename(pkt.payload, pkt.length);
    std::cout << "Recebendo arquivo: " << filename << std::endl;

    // Abrir arquivo no servidor para salvar os dados
    std::ofstream out_file("server_storage/" + filename, std::ios::binary);
    if (!out_file.is_open()) {
        std::cerr << "Erro ao criar arquivo para o upload.\n";
        close(client_socket);
        return;
    }

    // Loop para receber pacotes de dados e escrever no arquivo
    while (true) {
        if (!recv_packet(client_socket, pkt)) {
            std::cerr << "Erro ao receber pacote de dados.\n";
            break;
        }

        // Se o pacote for vazio (length == 0), significa que o upload terminou
        if (pkt.length == 0) {
            std::cout << "Upload concluído.\n";
            break;
        }

        // Escrever o conteúdo do pacote no arquivo
        out_file.write(pkt.payload, pkt.length);
        if (!out_file) {
            std::cerr << "Erro ao escrever dados no arquivo.\n";
            break;
        }

        std::cout << "Recebido pacote: seqn " << pkt.seqn << " com " << pkt.length << " bytes.\n";
    }

    // Fechar o arquivo e a conexão
    out_file.close();
    close(client_socket);
}


void start_server_socket(int port, void (*handler)(int)) {
    int server_fd, client_fd;
    sockaddr_in serv_addr{}, cli_addr{};
    socklen_t clilen = sizeof(cli_addr);

    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("ERROR opening socket");
        return;
    }

    // Set server address and port. Slide 20 Aula-11
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port); // Ensures correct byte order on the communication. Slide 26 Aula-11
    memset(&(serv_addr.sin_zero), 0, 8);

    // Bind the socket to the address and port. Slide 18 Aula-11
    int bind_result = bind(server_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr));
    if (bind_result < 0) {
        perror("ERROR on binding");
        return;
    }

    // Start listening for incoming connections (backlog = 5, max number of connections)
    // Slide 21 Aula-11
    listen(server_fd, 5);
    std::cout << "Server listening on port " << port << "...\n";

    while (true) {
        // Accept client connections. Slide 22 Aula-11
        client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&cli_addr), &clilen);
        if (client_fd < 0) {
            perror("ERROR on accept");
            continue;
        }

        // Create a new detached thread to handle each client
        std::thread(handler, client_fd).detach();
    }

    close(server_fd);
}

int main() {
    // Start one server socket per function on different ports
    std::thread command_thread(start_server_socket, COMMAND_PORT, handle_command_client);
    std::thread watcher_thread(start_server_socket, WATCHER_PORT, handle_watcher_client);
    std::thread file_thread(start_server_socket, FILE_PORT, handle_file_client);

    // Wait for each server thread to finish (keeps the main process alive)
    command_thread.join();
    watcher_thread.join();
    file_thread.join();

    return 0;
}