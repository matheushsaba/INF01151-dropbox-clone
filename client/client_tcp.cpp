// client_tcp.cpp
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include "command_interface.hpp"
#include "../common/packet.h"
#include <sys/stat.h>

// constexpr int PORT = 4000;

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;

int command_socket;
int watcher_socket;
int file_socket;
std::string hostname;
std::string username;

std::string get_sync_dir();

// Method to create a socket and connect it to the specified port
void connect_to_port(int& socket_fd, int port) {
    sockaddr_in serv_addr{};    // Initializes a struct of type sockaddr_in that is going to be filled later. Slide 20 Aula-11
    hostent* server = gethostbyname(hostname.c_str());  // Get server info based on hostname

    if (!server) { // Server returns null if it fails to be found
        std::cerr << "ERROR: No such host\n";
        exit(1);
    }

    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { // If is less than 0, it failed creating the socket
        perror("ERROR opening socket");
        exit(1);
    }

    // Set server address and port. Slide 20 Aula-11
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); // Ensures correct byte order on the communication. Slide 26 Aula-11
    serv_addr.sin_addr = *reinterpret_cast<in_addr*>(server->h_addr); // Copies the ip addres from gethostbyname(). server->h_addr is a pointer
    memset(&(serv_addr.sin_zero), 0, 8);

    // Connect the socket to the server. Slide 23 Aula-11
    int connect_result = connect(socket_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr));
    if (connect_result < 0) {
        perror("ERROR connecting");
        close(socket_fd);
        exit(1);
    }
}

// void send_command(const std::string& cmd) {
//     ssize_t bytes_written = write(command_socket, cmd.c_str(), cmd.length()); // Write - Flowchart slide 16 Aula-11
//     if (bytes_written < 0) {
//         perror("ERROR writing to command socket");
//         return;
//     }

//     char buffer[256]{}; // Reads at most 256 bytes
//     ssize_t bytes_read = read(command_socket, buffer, 255); // Read - Flowchart slide 16 Aula-11
//     if (bytes_read < 0) {
//         perror("ERROR reading from command socket");
//         return;
//     }

//     std::cout << "Server response: " << buffer << std::endl; // Prints the response
// }

void send_command(const std::string& cmd) {
    /* wrap the text in a Packet so it matches the server’s expectation */
    Packet pkt{};
    pkt.type  = PACKET_TYPE_CMD;
    pkt.seqn  = 0;
    pkt.total_size = 0;
    pkt.length = std::min<int>(cmd.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(pkt.payload, cmd.c_str(), pkt.length);

    if (!send_packet(command_socket, pkt)) {
        perror("ERROR sending command packet"); return;
    }

    Packet resp{};
    if (!recv_packet(command_socket, resp)) {
        perror("ERROR receiving command response"); return;
    }
    std::cout << "Server response: "
              << std::string(resp.payload, resp.length) << '\n';
}

void send_exit_command() {
    std::string cmd = "exit|" + username;          // tiny handshake
    Packet pkt{};
    pkt.type  = PACKET_TYPE_CMD;
    pkt.seqn  = 0;
    pkt.total_size = 0;
    pkt.length = std::min<int>(cmd.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(pkt.payload, cmd.c_str(), pkt.length);
    /* ignore ACK – we’re quitting anyway */
    send_packet(command_socket, pkt);
}

void start_watcher() {
    std::thread([]() {
        char buffer[256]{};
        while (true) {
            if (read(watcher_socket, buffer, 255) < 0) {
                perror("ERROR reading from watcher socket");
                break;
            }
            std::cout << "Watcher update: " << buffer << std::endl;
        }
    }).detach(); // Detach the thread to run independently
}

void send_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Erro ao abrir arquivo: " << file_path << "\n";
        return;
    }

    std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1);
    std::string header = "putfile|" + username + "|" + filename;

    // Send header packet first
    Packet header_pkt{};
    header_pkt.type = PACKET_TYPE_CMD;
    header_pkt.seqn = 0;
    header_pkt.length = std::min((int)header.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(header_pkt.payload, header.c_str(), header_pkt.length);

    if (!send_packet(file_socket, header_pkt)) {
        std::cerr << "Erro ao enviar cabeçalho do upload\n";
        return;
    }

    std::cout << "Enviando arquivo '" << filename << "' como usuário '" << username << "'\n";

    char buffer[MAX_PAYLOAD_SIZE];
    int seqn = 1;
    Packet data_pkt{};

    while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
        data_pkt.type = PACKET_TYPE_DATA;
        data_pkt.seqn = seqn++;
        data_pkt.length = file.gcount();
        std::memcpy(data_pkt.payload, buffer, data_pkt.length);

        if (!send_packet(file_socket, data_pkt)) {
            std::cerr << "Erro ao enviar pacote de dados\n";
            break;
        }
    }

    // End-of-file marker
    data_pkt.type = PACKET_TYPE_DATA;
    data_pkt.seqn = seqn;
    data_pkt.length = 0;
    send_packet(file_socket, data_pkt);

    std::cout << "Upload concluído com sucesso.\n";
}

// Moves a file to the user's sync directory.
// Returns the full destination path or empty string on error.
std::string move_file_to_sync_dir(const std::string& source_path) {
    namespace fs = std::filesystem;

    fs::path src_path(source_path);
    if (!fs::exists(src_path)) {
        std::cerr << "Arquivo não encontrado: " << source_path << '\n';
        return "";
    }

    std::string filename = src_path.filename().string();
    fs::path dest_path = fs::path(get_sync_dir()) / filename;

    std::error_code ec;
    fs::copy_file(src_path, dest_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Erro ao copiar para a pasta de sincronização: " << ec.message() << '\n';
        return "";
    }

    std::cout << "Arquivo movido para a pasta de sincronização: " << dest_path << '\n';
    return dest_path.string();
}

std::string download_from_sync_dir(const std::string& filename) {
    namespace fs = std::filesystem;

    fs::path sync_path = fs::path(get_sync_dir()) / filename;
    fs::path target_path = fs::current_path() / filename;

    if (!fs::exists(sync_path)) {
        std::cerr << "Arquivo não encontrado no diretório de sincronização: " << sync_path << '\n';
        return "";
    }

    std::error_code ec;
    fs::copy_file(sync_path, target_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Erro ao copiar arquivo para o diretório atual: " << ec.message() << '\n';
        return "";
    }

    std::cout << "Arquivo copiado para o diretório atual: " << target_path << '\n';
    return target_path.string();
}

bool delete_from_sync_dir(const std::string& filename) {
    namespace fs = std::filesystem;

    fs::path target_path = fs::path(get_sync_dir()) / filename;

    if (!fs::exists(target_path)) {
        std::cerr << "Arquivo não encontrado no diretório de sincronização: " << target_path << '\n';
        return false;
    }

    std::error_code ec;
    fs::remove(target_path, ec);
    if (ec) {
        std::cerr << "Erro ao deletar o arquivo: " << ec.message() << '\n';
        return false;
    }

    std::cout << "Arquivo removido: " << target_path << '\n';
    return true;
}

void list_client_sync_dir() {
    namespace fs = std::filesystem;

    fs::path sync_dir = get_sync_dir();

    std::cout << "\nArquivos no diretório de sincronização de " << username << ":\n";
    std::cout << "-------------------------------------------------------------\n";

    for (const auto& entry : fs::directory_iterator(sync_dir)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        std::string filename = path.filename().string();

        std::error_code ec;
        auto ftime = fs::last_write_time(path, ec);
        auto s = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now()
            + std::chrono::system_clock::now()
        );

        struct stat stat_buf;
        if (stat(path.c_str(), &stat_buf) != 0) {
            perror("stat");
            continue;
        }

        std::cout << "Nome: " << filename << '\n';
        std::cout << "  Acesso (atime):    " << std::asctime(std::localtime(&stat_buf.st_atime));
        std::cout << "  Modificado (mtime): " << std::asctime(std::localtime(&stat_buf.st_mtime));
        std::cout << "  Criado (ctime):     " << std::asctime(std::localtime(&stat_buf.st_ctime));
        std::cout << "-------------------------------------------------------------\n";
    }
}

// ---------------------------------------------------------------------------
// Returns an **absolute** path to client_storage/sync_dir_<username>
// and guarantees that the directory exists (idempotent).
// If the same username is reused, the same directory is returned.
// ---------------------------------------------------------------------------
std::string get_sync_dir()
{
    namespace fs = std::filesystem;

    if (username.empty()) {
        throw std::runtime_error("get_sync_dir() called before username is set");
    }

    fs::path sync_dir = fs::path{"client_storage"} / ("sync_dir_" + username);

    std::error_code ec;
    fs::create_directories(sync_dir, ec); // creates intermediate dirs too
    if (ec) {
        std::cerr << "Warning: could not create " << sync_dir
                  << " (" << ec.message() << ")\n";
    }

    return fs::absolute(sync_dir).string();
}

// void cleanup_sockets() {
//     close(command_socket);
//     close(watcher_socket);
//     close(file_socket);
// }

void cleanup_sockets() {            // friendlier shutdown
    shutdown(command_socket, SHUT_RDWR);
    close(command_socket);

    shutdown(watcher_socket, SHUT_RDWR);
    close(watcher_socket);
    
    shutdown(file_socket,  SHUT_RDWR);
    close(file_socket);
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <username> <server_ip_address> <port>\n";
        return 1;
    }

    username = argv[1];                  // e.g. "alice"
    hostname = argv[2];                  // e.g. "127.0.0.1"
    int base_port = std::stoi(argv[3]);  // e.g. 4000

    // Creates the client sync_dir
    std::string g_sync_dir = get_sync_dir();
    std::cout << "Local sync directory: " << g_sync_dir << '\n';

    connect_to_port(command_socket, COMMAND_PORT);
    connect_to_port(watcher_socket, WATCHER_PORT);
    connect_to_port(file_socket, FILE_PORT);

    start_watcher(); // Start the watcher thread

    init_command_callbacks(send_command, send_file);

    std::string input;
    while (true) {
        print_menu();
        std::cout << "Command ('exit' to quit): ";
        std::getline(std::cin, input);

        if (input == "exit") {
            send_exit_command();   // <-- notify the server
            break;
        } else {
            // Handle regular command
            process_command(input);
        }
    }

    cleanup_sockets();
    return 0;
}