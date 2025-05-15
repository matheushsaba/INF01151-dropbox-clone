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

// constexpr int PORT = 4000;

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;

int command_socket;
int watcher_socket;
int file_socket;
std::string hostname;
std::string username;

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

void send_command(const std::string& cmd) {
    ssize_t bytes_written = write(command_socket, cmd.c_str(), cmd.length()); // Write - Flowchart slide 16 Aula-11
    if (bytes_written < 0) {
        perror("ERROR writing to command socket");
        return;
    }

    char buffer[256]{}; // Reads at most 256 bytes
    ssize_t bytes_read = read(command_socket, buffer, 255); // Read - Flowchart slide 16 Aula-11
    if (bytes_read < 0) {
        perror("ERROR reading from command socket");
        return;
    }

    std::cout << "Server response: " << buffer << std::endl; // Prints the response
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

    std::cout << "Iniciando upload do arquivo: " << file_path << std::endl;

    Packet pkt{};
    pkt.type = PACKET_TYPE_CMD;
    pkt.seqn = 1;


    std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1);

    pkt.length = filename.size();
    std::memcpy(pkt.payload, filename.c_str(), pkt.length);

    if (!send_packet(file_socket, pkt)) {
        std::cerr << "Erro ao enviar o nome do arquivo\n";
        return;
    }

    char buffer[MAX_PAYLOAD_SIZE];
    int seqn = 2;

    while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
        pkt.type = PACKET_TYPE_DATA;
        pkt.seqn = seqn++;
        pkt.length = file.gcount();
        std::memcpy(pkt.payload, buffer, pkt.length);

        if (!send_packet(file_socket, pkt)) {
            std::cerr << "Erro ao enviar pacote de dados\n";
            break;
        }
    }

    // send packet with lenght 0 to indicate the end of the upload
    pkt.type = PACKET_TYPE_DATA;
    pkt.seqn = seqn;
    pkt.length = 0;
    send_packet(file_socket, pkt); // end of file

    std::cout << "Upload concluído com sucesso.\n";
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

void cleanup_sockets() {
    close(command_socket);
    close(watcher_socket);
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
            break;
        } else {
            // Handle regular command
            process_command(input);
        }
    }

    cleanup_sockets();
    return 0;
}