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
#include <atomic>
#include <vector>
#include "command_interface.hpp"
#include "../common/packet.h"
#include <sys/stat.h>
#include "../common/common.hpp"
#include <sys/inotify.h>
#include <map> 
#include <utime.h>
#include "../common/FileInfo.hpp"

extern void connect_to_port(int& socket_fd, int port);
extern int file_socket;
extern std::string hostname;

constexpr int NAME_MAX = 255;

int dynamic_file_port = -1;
int command_socket;
int watcher_socket;
int file_socket;

std::string hostname;
std::string username;
std::atomic<bool> watcher_running{true};

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

    // Packet resp{};
    // if (!recv_packet(command_socket, resp)) {
    //     perror("ERROR receiving command response"); return;
    // }
    // std::cout << "Server response: "
    //           << std::string(resp.payload, resp.length) << '\n';
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

std::string get_sync_dir()
{
    static std::string cached =
        common::ensure_sync_dir("client_storage", username);
    return cached;
}

std::vector<FileInfo> list_client_sync_dir()
{
    std::string sync_dir = get_sync_dir();
    std::vector<FileInfo> files;

    for (const auto& entry : std::filesystem::directory_iterator(sync_dir)) {
        if (entry.is_regular_file()) {
            struct stat st{};
            if (::stat(entry.path().c_str(), &st) == 0) {
                FileInfo info{};
                strncpy(info.name, entry.path().filename().string().c_str(), sizeof(info.name) - 1);
                info.mtime = st.st_mtime;
                info.ctime = st.st_ctime;
                files.push_back(info);
            }
        }
    }
    return files;
}

std::vector<FileInfo> get_server_sync_dir() {
    std::string cmd = "list_server|" + username;
    send_command(cmd);       

    std::vector<char> buffer;
    Packet pkt{};
    std::cout << "Receiving file list from server...\n";
    while (true) {
        if (!recv_packet(command_socket, pkt)) {
            std::cout << "Erro ao receber resposta do servidor.\n";
            return {};
        };
        if (pkt.type == PACKET_TYPE_END) break; // End of transmission
        if (pkt.type != PACKET_TYPE_DATA) continue;
        buffer.insert(buffer.end(), pkt.payload, pkt.payload + pkt.length);
    }

    size_t count = buffer.size() / sizeof(FileInfo);
    std::vector<FileInfo> files;
    const FileInfo* infos = reinterpret_cast<const FileInfo*>(buffer.data());
    for (size_t i = 0; i < count; ++i) {
        files.push_back(infos[i]);
    }
    return files;
}

void cleanup_sockets() {            // friendlier shutdown
    shutdown(command_socket, SHUT_RDWR);
    close(command_socket);

    shutdown(watcher_socket, SHUT_RDWR);
    close(watcher_socket);
    
    shutdown(file_socket,  SHUT_RDWR);
    close(file_socket);
}

void watch_sync_dir_inotify() {
    std::string sync_dir = get_sync_dir();
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        perror("inotify_init1");
        return;
    }

    int wd = inotify_add_watch(inotify_fd, sync_dir.c_str(), IN_CLOSE_WRITE | IN_DELETE);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(inotify_fd);
        return;
    }

    const size_t event_size = sizeof(struct inotify_event);
    const size_t buf_len = 1024 * (event_size + NAME_MAX + 1);
    std::vector<char> buffer(buf_len);

    while (watcher_running) {
        int length = read(inotify_fd, buffer.data(), buf_len);
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100 * 1000); // Sleep 100ms
                continue;
            }
            perror("read");
            break;
        }

        for (int i = 0; i < length;) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->len > 0) {
                std::string filepath = sync_dir + "/" + event->name;
                if (event->mask & IN_CLOSE_WRITE) {
                    std::cout << "File closed after write: " << filepath << std::endl;
                    if (std::filesystem::is_regular_file(filepath)) {
                        // // Reconnect to the file transfer port (4002) before each upload
                        connect_to_port(file_socket, dynamic_file_port);
                        std::cout << "✅ Connected to file socket on port " << dynamic_file_port << '\n';
                        send_file(filepath); // Upload the file
                        shutdown(file_socket, SHUT_RDWR);
                        close(file_socket);
                    }
                } else if (event->mask & IN_DELETE) {
                    std::cout << "File deleted: " << filepath << std::endl;
                    std::string filename = event->name;
                    // Reconnect to the file transfer port (4002) before each delete
                    connect_to_port(file_socket, dynamic_file_port);

                    // Build and send delete command packet
                    std::string header = "delfile|" + username + "|" + filename;
                    Packet header_pkt{};
                    header_pkt.type = PACKET_TYPE_CMD;
                    header_pkt.seqn = 0;
                    header_pkt.length = std::min((int)header.size(), MAX_PAYLOAD_SIZE);
                    std::memcpy(header_pkt.payload, header.c_str(), header_pkt.length);

                    send_packet(file_socket, header_pkt);

                    shutdown(file_socket, SHUT_RDWR);
                    close(file_socket);
                }
            }
            i += event_size + event->len;
        }
    }

    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);
}

void sync_with_server() {
    std::string sync_dir = get_sync_dir();
    std::cout << "Syncing with server...\n";

    // Get file lists
    std::vector<FileInfo> server_files = get_server_sync_dir();
    std::vector<FileInfo> local_files = list_client_sync_dir();

    // Build lookup for local files
    std::unordered_map<std::string, FileInfo> local_map;
    for (const auto& info : local_files) {
        local_map[info.name] = info;
    }

    // For each server file, check if missing or outdated locally
    for (const auto& srv_info : server_files) {
        auto it = local_map.find(srv_info.name);
        bool need_pull = false;
        if (it == local_map.end()) {
            need_pull = true; // missing locally
        } else if (it->second.mtime < srv_info.mtime) {
            need_pull = true; // outdated locally
        }
        if (need_pull) {
            std::cout << "Pulling updated file from server: " << srv_info.name << std::endl;
            // TODO: Implement download logic here
            send_command("download|" + std::string(srv_info.name));
            
            // receive file and save to sync_dir
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <username> <server_ip_address> <port>\n";
        return 1;
    }

    username = argv[1];                  // e.g. "alice"
    hostname = argv[2];                  // e.g. "127.0.0.1"
    int port = std::stoi(argv[3]);       // e.g. 4000

    int session_socket;
    connect_to_port(session_socket, port);

    // Send handshake packet: "username"
    Packet hello{};
    hello.type = PACKET_TYPE_CMD;
    hello.length = std::min((int)username.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(hello.payload, username.c_str(), hello.length);
    send_packet(session_socket, hello);

    // Wait for response
    Packet reply{};
    if (!recv_packet(session_socket, reply)) {
        std::cerr << "❌ Failed to receive response from server.\n";
        close(session_socket);
        return 1;
    }

    std::string response_msg(reply.payload, reply.length);
    if (response_msg.rfind("DENY", 0) == 0) {
        std::cerr << "❌ Connection refused: " << response_msg << '\n';
        close(session_socket);
        return 1;
    }

    // Assume OK and expect 3-port info next
    Packet ports_pkt;
    if (!recv_packet(session_socket, ports_pkt)) {
        std::cerr << "❌ Failed to receive port info from server.\n";
        close(session_socket);
        return 1;
    }

    close(session_socket);  // We no longer use the initial socket

    std::string ports_str(ports_pkt.payload, ports_pkt.length);
    size_t p1 = ports_str.find('|');
    size_t p2 = ports_str.find('|', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) {
        std::cerr << "❌ Malformed port message: " << ports_str << '\n';
        return 1;
    }
    
    // sync_with_server();

    int command_port = std::stoi(ports_str.substr(0, p1));
    int watcher_port = std::stoi(ports_str.substr(p1 + 1, p2 - p1 - 1));
    dynamic_file_port = std::stoi(ports_str.substr(p2 + 1));

    connect_to_port(command_socket, command_port);
    std::cout << "✅ Connected to command socket on port " << command_port << '\n';

    connect_to_port(watcher_socket, watcher_port);
    std::cout << "✅ Connected to watcher socket on port " << watcher_port << '\n';

    // The file port only receives a connection reuest when a file is sent

    std::string g_sync_dir = get_sync_dir();
    std::cout << "Local sync directory: " << g_sync_dir << '\n';

    std::thread inotify_thread(watch_sync_dir_inotify);

    init_command_callbacks(send_command, send_file);

    std::string input;
    while (true) {
        print_menu();
        std::getline(std::cin, input);
        if (input == "exit") {
            send_exit_command();
            std::cout << "Closing connection... \n";            
            break;
        } else {
            process_command(input);
        }
    }

    watcher_running = false;
    if (inotify_thread.joinable()) inotify_thread.join();
    cleanup_sockets();
    return 0;
}