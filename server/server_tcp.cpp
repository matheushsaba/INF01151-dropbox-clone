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
#include <sys/stat.h>     // stat() for MAC times
#include "../common/common.hpp"


// constexpr int PORT = 4000;
// std::mutex file_mutex; // Usamos mutex para sincronizar threads em processos diferentes

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;
std::mutex file_mutex;  // Global mutex used to synchronize access to shared resources (e.g., files)
std::mutex socket_creation_mutex;

std::string get_sync_dir(const std::string& username) {
    namespace fs = std::filesystem;

    fs::path sync_path = fs::path("server_storage") / ("sync_dir_" + username);

    std::error_code ec;
    fs::create_directories(sync_path, ec);  // idempotent
    if (ec) {
        std::cerr << "Erro ao criar diretório de sincronização para " << username
                  << ": " << ec.message() << '\n';
    }

    return fs::absolute(sync_path).string();
}

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

            std::string user_dir = get_sync_dir(username);
            std::string response_data = common::list_files_with_mac(get_sync_dir(username));

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

void handle_file_client(int client_socket)
{
    Packet pkt;

    while (true) {                               // ❶ laço externo = 1-conexão / N-arquivos
        /* ---------- 1. Cabeçalho “putfile|<user>|<fname>” ---------- */
        if (!recv_packet(client_socket, pkt)) {          // EOF ou erro → fecha conexão
            std::cerr << "Conexão encerrada pelo cliente.\n";
            break;
        }
        if (pkt.type != PACKET_TYPE_CMD) {               // protocolo inesperado
            std::cerr << "Tipo de pacote inválido.\n";
            break;
        }

        std::string header(pkt.payload, pkt.length);
        if ((header.rfind("putfile|", 0) != 0) && header.rfind("delfile|", 0) != 0) {          // qualquer outro comando → encerra
            std::cerr << "Comando desconhecido: " << header << '\n';
            break;
        }

        /* Extrai user e filename */
        size_t p1 = header.find('|');
        size_t p2 = header.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            std::cerr << "Cabeçalho malformado: " << header << '\n';
            break;
        }
        std::string username = header.substr(p1 + 1, p2 - p1 - 1);
        std::string filename = header.substr(p2 + 1);

        /* ---------- 2. Abre destino seguro ---------- */
        std::string full_path = get_sync_dir(username) + "/" + filename;
        if (header.rfind("delfile|", 0) == 0) {
            std::lock_guard<std::mutex> lock(file_mutex);
            if (std::filesystem::exists(full_path)) {
                if (std::filesystem::remove(full_path)) {
                    std::cout << "[DELETE] " << username << '/' << filename << " removido.\n";
                } else {
                    std::cerr << "Erro ao remover " << full_path << '\n';
                }
            } else {
                std::cerr << "Arquivo para remover não encontrado: " << full_path << '\n';
            }
            goto close_connection; 
        }
        {
            std::lock_guard<std::mutex> lock(file_mutex);   // protege criação do dir/arquivo
            std::ofstream out(full_path, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "Não foi possível criar " << full_path << '\n';
                break;
            }

            std::cout << "[UPLOAD] " << username << '/' << filename << '\n';

            /* ---------- 3. Blocos DATA até length==0 ---------- */
            while (true) {
                if (!recv_packet(client_socket, pkt)) {      // drop inesperado
                    std::cerr << "Conexão perdida durante upload.\n";
                    goto close_connection;                        // sai do dois níveis
                }
                if (pkt.type != PACKET_TYPE_DATA) {
                    std::cerr << "Esperava DATA, recebi outro tipo.\n";
                    goto close_connection;
                }
                if (pkt.length == 0) {                       // marcador EOF
                    std::cout << "Upload concluído (" << filename << ").\n";
                    break;                                   // volta ao laço externo p/ próximo arquivo
                }
                out.write(pkt.payload, pkt.length);
                if (!out) {
                    std::cerr << "Erro de escrita em disco.\n";
                    goto close_connection;
                }
            }
        }   // mutex liberado aqui — permite outros uploads em paralelo
        continue;                                           // pronto p/ novo cabeçalho
    }

close_connection:
    close(client_socket);
}


void start_server_socket(int port, void (*handler)(int)) {
    int server_fd, client_fd;
    sockaddr_in serv_addr{}, cli_addr{};
    socklen_t clilen = sizeof(cli_addr);
    {
        std::lock_guard<std::mutex> lock(socket_creation_mutex);

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
    }

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
    std::cout << std::unitbuf;             // C++-way: flush after each insertion
    
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