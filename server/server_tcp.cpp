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


// void start_server_socket(int port, void (*handler)(int)) {
//     int server_fd, client_fd;
//     sockaddr_in serv_addr{}, cli_addr{};
//     socklen_t clilen = sizeof(cli_addr);
//     {
//         std::lock_guard<std::mutex> lock(socket_creation_mutex);

//         // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
//         server_fd = socket(AF_INET, SOCK_STREAM, 0);
//         if (server_fd < 0) {
//             perror("ERROR opening socket");
//             return;
//         }

//         // Set server address and port. Slide 20 Aula-11
//         serv_addr.sin_family = AF_INET;
//         serv_addr.sin_addr.s_addr = INADDR_ANY;
//         serv_addr.sin_port = htons(port); // Ensures correct byte order on the communication. Slide 26 Aula-11
//         memset(&(serv_addr.sin_zero), 0, 8);

//         // Bind the socket to the address and port. Slide 18 Aula-11
//         int bind_result = bind(server_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr));
//         if (bind_result < 0) {
//             perror("ERROR on binding");
//             return;
//         }
    
//         // Start listening for incoming connections (backlog = 5, max number of connections)
//         // Slide 21 Aula-11
//         listen(server_fd, 5);
//         std::cout << "Server listening on port " << port << "...\n";
//     }

//     while (true) {
//         // Accept client connections. Slide 22 Aula-11
//         client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&cli_addr), &clilen);
//         if (client_fd < 0) {
//             perror("ERROR on accept");
//             continue;
//         }

//         // Create a new detached thread to handle each client
//         std::thread(handler, client_fd).detach();
//     }

//     close(server_fd);
// }

// void handle_new_client(int initial_socket) {
//     // 1. Recebe username do cliente
//     Packet pkt;
//     if (!recv_packet(initial_socket, pkt) || pkt.type != PACKET_TYPE_CMD) {
//         std::cerr << "Erro ao receber identificação do cliente.\n";
//         close(initial_socket);
//         return;
//     }

//     std::string username(pkt.payload, pkt.length);
//     std::cout << "Novo cliente conectado: " << username << '\n';

//     // 2. Cria novas threads por cliente
//     std::thread(handle_command_client, initial_socket).detach(); // reutiliza o socket original como "comando"
//     std::thread watcher_thread(start_server_socket, WATCHER_PORT, handle_watcher_client);
// }

// int main() {
//     std::cout << std::unitbuf;             // C++-way: flush after each insertion
    
//     // Start one server socket per function on different ports
//     // std::thread command_thread(start_server_socket, COMMAND_PORT, handle_command_client);
//     // std::thread watcher_thread(start_server_socket, WATCHER_PORT, handle_watcher_client);
//     // std::thread file_thread(start_server_socket, FILE_PORT, handle_file_client);

//     // Wait for each server thread to finish (keeps the main process alive)
//     // command_thread.join();
//     // watcher_thread.join();
//     // file_thread.join();

//     start_server_socket(MAIN_PORT, handle_new_client);

//     return 0;
// }

int create_dynamic_socket(int& port_out) {
    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error: server socket opening");
        return -1;
    }

    // Set server address and port. Slide 20 Aula-11
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // bind to any available port

    // Bind the socket to the address and port. Slide 18 Aula-11
    int bind_result = bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (bind_result < 0) {
        perror("Error: server socket bind");
        return -1;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sockfd, reinterpret_cast<sockaddr*>(&addr), &len) == -1) {
        perror("getsockname failed");
        return -1;
    }

    port_out = ntohs(addr.sin_port);
    listen(sockfd, 1); // Listen for 1 client
    return sockfd;
}

void handle_new_connection(int listener_socket) {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listener_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        std::thread([client_fd]() {
            // Receive username
            Packet pkt;
            if (!recv_packet(client_fd, pkt)) {
                close(client_fd);
                return;
            }
            std::string username(pkt.payload, pkt.length);
            std::cout << "Client connected: " << username << std::endl;

            // Create command and watcher sockets
            int cmd_port, watch_port;
            int cmd_sock = create_dynamic_socket(cmd_port);
            int watch_sock = create_dynamic_socket(watch_port);

            if (cmd_sock < 0 || watch_sock < 0) {
                std::cerr << "Failed to create dynamic sockets.\n";
                close(client_fd);
                return;
            }

            // Send the two ports to the client
            std::string ports_msg = std::to_string(cmd_port) + "|" + std::to_string(watch_port);
            Packet reply{};
            reply.type = PACKET_TYPE_CMD;
            reply.seqn = 0;
            reply.length = ports_msg.size();
            std::memcpy(reply.payload, ports_msg.c_str(), reply.length);
            send_packet(client_fd, reply);
            close(client_fd); // Done with initial handshake

            // Wait for client to connect to command and watcher sockets
            sockaddr_in tmp{};
            socklen_t tmp_len = sizeof(tmp);
            int cmd_client_fd = accept(cmd_sock, reinterpret_cast<sockaddr*>(&tmp), &tmp_len);
            int watch_client_fd = accept(watch_sock, reinterpret_cast<sockaddr*>(&tmp), &tmp_len);

            std::thread(handle_command_client, cmd_client_fd).detach();
            std::thread(handle_watcher_client, watch_client_fd).detach();

            close(cmd_sock);
            close(watch_sock);
        }).detach();
    }
}

int main() {
    std::cout << std::unitbuf;

    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
    int listener_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_socket < 0) {
        perror("Error opening listener socket");
        return -1;
    }

    // Set server address and port. Slide 20 Aula-11
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(4000); // Well-known port for accepting new clients

    // Bind the socket to the address and port. Slide 18 Aula-11
    int bind_result = bind(listener_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (bind_result < 0) {
        perror("Error binding listener socket");
        return -1;
    }

    // Start listening for incoming connections (backlog = 5, max number of connections)
    // Slide 21 Aula-11
    listen(listener_socket, 5);
    std::cout << "Listening for new client sessions on port 4000...\n";

    handle_new_connection(listener_socket);

    return 0;
}