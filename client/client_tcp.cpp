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

extern bool connect_to_port(int& socket_fd, int port);
extern int file_socket;
extern std::string hostname;

constexpr int NAME_MAX = 255;


// Sockets for the 3 communication channels with the FE. Initialized as invalid.
int g_command_socket = -1;
int g_watcher_socket = -1;
int g_file_socket = -1; 

// Fake ports for front-end reconnection
int g_fake_command_port = -1;
int g_fake_watcher_port = -1;
int g_fake_file_port = -1;

std::string hostname;
std::string username;
std::atomic<bool> watcher_running{true};

std::string get_sync_dir();

// Method to create a socket and connect it to the specified port
bool connect_to_port(int& socket_fd, int port) {
    sockaddr_in serv_addr{};
    hostent* server = gethostbyname(hostname.c_str());

    if (!server) {
        std::cerr << "ERROR: No such host:" << hostname << std::endl;
        return false;
    }
    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("ERROR opening socket");
        return false;
    }

    // Set server address and port. Slide 20 Aula-11
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); // Ensures correct byte order on the communication. Slide 26 Aula-11
    serv_addr.sin_addr = *reinterpret_cast<in_addr*>(server->h_addr); // Copies the ip addres from gethostbyname(). server->h_addr is a pointer
    memset(&(serv_addr.sin_zero), 0, 8);

    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    return true;
}
void send_command(const std::string& cmd) 
{
    Packet pkt{};
    pkt.type = PACKET_TYPE_CMD;
    pkt.length = std::min<int>(cmd.size(), MAX_PAYLOAD_SIZE);
    std::memcpy(pkt.payload, cmd.c_str(), pkt.length);

    // first attempt to connect
    if (send_packet(g_command_socket, pkt)) {
        return;
    }
    // if it failed, probably the connection was lost. Try to reconnect
    std::cout << "\n[CLIENT] Command connection lost. Trying to reconnect to Front-End..." << std::endl;
    close(g_command_socket);
    connect_to_port(g_command_socket, g_fake_command_port);

    // second attempt to connect to new socket
    if (send_packet(g_command_socket, pkt)) {
        std::cout << "[CLIENT] Reconnected successfully. Command sent." << std::endl;
    } else {
        perror("[CLIENT] ERROR: Failed to send command after reconnecting");
    }
}

void send_file(const std::string& file_path) 
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening file " << file_path << std::endl;
        return;
    }

    // The reconnection logic for the file socket is crucial here.
    if (g_file_socket < 0) {
        std::cout << "[CLIENT] Connecting to file channel..." << std::endl;
        connect_to_port(g_file_socket, g_fake_file_port);
    }
    
    std::string filename = std::filesystem::path(file_path).filename().string();
    std::string header = "putfile|" + username + "|" + filename;

    Packet header_pkt{};
    header_pkt.type = PACKET_TYPE_CMD;
    header_pkt.length = header.length();
    std::memcpy(header_pkt.payload, header.c_str(), header_pkt.length);

    if (!send_packet(g_file_socket, header_pkt)) {
        std::cerr << "Error sending packet header during upload. Trying to reconnect..." << std::endl;
        close(g_file_socket);
        connect_to_port(g_file_socket, g_fake_file_port);
        if (!send_packet(g_file_socket, header_pkt)) {
            std::cerr << "Error sending packet header during upload even after reconnecting." << std::endl;
            close(g_file_socket);
            g_file_socket = -1;
            return;
        }
    }
    
    char buffer[MAX_PAYLOAD_SIZE];
    Packet data_pkt{};
    while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
        data_pkt.type = PACKET_TYPE_DATA;
        data_pkt.length = file.gcount();
        std::memcpy(data_pkt.payload, buffer, data_pkt.length);
        if (!send_packet(g_file_socket, data_pkt)) {
            std::cerr << "Error sending packet data." << std::endl;
            close(g_file_socket);
            g_file_socket = -1;
            break; 
        }
    }

    data_pkt.length = 0; //  end of file indicator
    send_packet(g_file_socket, data_pkt);
    std::cout << "Upload of file '" << filename << "' completed." << std::endl;

    // Closing the file connection to free resources, it will be reopened on the next operation.
    close(g_file_socket);
    g_file_socket = -1;
}

// Moves a file to the user's sync directory.
// Returns the full destination path or empty string on error.
std::string move_file_to_sync_dir(const std::string& source_path) 
{
    namespace fs = std::filesystem;

    fs::path src_path(source_path);
    if (!fs::exists(src_path)) {
        std::cerr << "File not found: " << source_path << '\n';
        return "";
    }

    std::string filename = src_path.filename().string();
    fs::path dest_path = fs::path(get_sync_dir()) / filename;

    std::error_code ec;
    fs::copy_file(src_path, dest_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Error copying to sync_dir: " << ec.message() << '\n';
        return "";
    }

    std::cout << "File moved to sync_dir: " << dest_path << '\n';
    return dest_path.string();
}

std::string download_from_sync_dir(const std::string& filename) 
{
    namespace fs = std::filesystem;

    fs::path sync_path = fs::path(get_sync_dir()) / filename;
    fs::path target_path = fs::current_path() / filename;

    if (!fs::exists(sync_path)) {
        std::cerr << "File not found at sync_dir: " << sync_path << '\n';
        return "";
    }

    std::error_code ec;
    fs::copy_file(sync_path, target_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Error copying file to current directory: " << ec.message() << '\n';
        return "";
    }

    std::cout << "File copied to current directory: " << target_path << '\n';
    return target_path.string();
}

bool delete_from_sync_dir(const std::string& filename) 
{
    namespace fs = std::filesystem;

    fs::path target_path = fs::path(get_sync_dir()) / filename;

    if (!fs::exists(target_path)) {
        std::cerr << "File not found at sync_dir: " << target_path << '\n';
        return false;
    }

    std::error_code ec;
    fs::remove(target_path, ec);
    if (ec) {
        std::cerr << "Error deleting the file: " << ec.message() << '\n';
        return false;
    }

    std::cout << "File removed: " << target_path << '\n';
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

std::vector<FileInfo> get_server_sync_dir() 
{
    std::string cmd = "list_server|" + username;
    send_command(cmd);

    std::vector<char> buffer;
    Packet pkt{};
    std::cout << "Receiving file list from server..." << std::endl;
    while (true) {
        if (!recv_packet(g_command_socket, pkt)) {
            std::cout << "Error receiving response from server.\n";
            return {};
        }
        if (pkt.type == PACKET_TYPE_END) break; // End of transmission
        if (pkt.type != PACKET_TYPE_DATA) continue;
        buffer.insert(buffer.end(), pkt.payload, pkt.payload + pkt.length);
    }

    std::vector<FileInfo> files(reinterpret_cast<const FileInfo*>(buffer.data()),
                                reinterpret_cast<const FileInfo*>(buffer.data() + buffer.size()));
    return files;
}

void cleanup_sockets() 
{
    close(g_command_socket);
    close(g_watcher_socket);
    if (g_file_socket > 0) close(g_file_socket);
}

void watch_sync_dir_inotify() 
{
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
            usleep(100 * 1000);
            continue;
        }

        for (int i = 0; i < length;) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->len > 0) {
                std::string filepath = sync_dir + "/" + event->name;
                if (event->mask & IN_CLOSE_WRITE) {
                    std::cout << "[INOTIFY] File modification detected: " << event->name << ". Sending..." << std::endl;
                    if (std::filesystem::is_regular_file(filepath)) {
                        // Just call send_file. The connection responsibility is hers.
                        send_file(filepath);
                    }
                } else if (event->mask & IN_DELETE) {
                    std::cout << "[INOTIFY] File deleted: " << event->name << std::endl;
                    std::string cmd = "delete|" + std::string(event->name);
                    send_command(cmd); 
                }
            }
            i += event_size + event->len;
        }
    }
    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);
}

void sync_with_server() 
{
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
            send_command("download|" + std::string(srv_info.name));

            // Receive file and save to sync_dir
            std::string filepath = sync_dir + "/" + srv_info.name;
            FILE* fp = fopen(filepath.c_str(), "wb");
            if (!fp) {
                std::cerr << "Failed to open file for writing: " << filepath << std::endl;
                continue;
            }

            Packet pkt{};
            while (true) {
                if (!recv_packet(g_command_socket, pkt)) {
                    std::cerr << "Error receiving file data from server.\n";
                    break;
                }
                if (pkt.type == PACKET_TYPE_END) break;
                if (pkt.type != PACKET_TYPE_DATA) continue;
                fwrite(pkt.payload, 1, pkt.length, fp);
            }
            fclose(fp);
            std::cout << "File received and saved: " << filepath << std::endl;
        }
    }
}

void watch_server_sync(int socket_fd) 
{
    while (watcher_running) {
        Packet pkt{};
        if (!recv_packet(socket_fd, pkt)) {
            std::cout << "[Watcher] Connection with server lost. Ending watcher thread." << std::endl;
            // The thread will exit here, but the reconnection logic is handled in the main loop if necessary.
            // This is a change from the previous logic where we would print an error and continue.
            // This allows the main thread to handle reconnections or other operations without blocking.
            return;
        }

        if (pkt.type == PACKET_TYPE_NOTIFY) {
            std::cout << "[watch_server_sync] Server change notification received. Syncing...\n";
            sync_with_server();
        } else if (pkt.type == PACKET_TYPE_DELETE) {
            std::cout << "[watch_server_sync] Deletion notification received. Deleting from sync_dir...\n";
            std::string filename = pkt.payload;
            std::string filename2 = filename.substr(19);
            delete_from_sync_dir(filename2);
        }
    }
}


int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <username> <frontend_ip> <frontend_port>\n";
        return 1;
    }
    username = argv[1];
    hostname = argv[2];
    int port = std::stoi(argv[3]);

    // 1. Handshake connection
    int handshake_socket;
    if (!connect_to_port(handshake_socket, port)) {
        std::cerr << "ERROR: Not possible to connect to Front-End in " << hostname << ":" << port << std::endl;
        return 1;
    }
    
    Packet hello{};
    hello.type = PACKET_TYPE_CMD;
    hello.length = username.length();
    memcpy(hello.payload, username.c_str(), hello.length);
    send_packet(handshake_socket, hello);

    // 2. Receive response and fake ports
    Packet reply{};
    if (!recv_packet(handshake_socket, reply) || std::string(reply.payload, reply.length) != "OK") {
   std::cerr << "❌ Failed to receive response from server: " << std::string(reply.payload, reply.length) << std::endl;
        close(handshake_socket);
        return 1;
    }
    if (!recv_packet(handshake_socket, reply)) {
 std::cerr << "❌ Failed to receive port info from front-end." << std::endl;
        close(handshake_socket);
        return 1;
    }
    close(handshake_socket); // We no longer use the initial socket

    // 3. Store fake ports and establish service connections
    std::string ports_str(reply.payload, reply.length);
    size_t p1 = ports_str.find('|');
    size_t p2 = ports_str.find('|', p1 + 1);
    
    g_fake_command_port = std::stoi(ports_str.substr(0, p1));
    g_fake_watcher_port = std::stoi(ports_str.substr(p1 + 1, p2 - p1 - 1));
    g_fake_file_port = std::stoi(ports_str.substr(p2 + 1));

    if (!connect_to_port(g_command_socket, g_fake_command_port)) { return 1; }
    std::cout << "✅ Connected to command channel via Front-End" << std::endl;

    if (!connect_to_port(g_watcher_socket, g_fake_watcher_port)) { return 1; }
    std::cout << "✅ Connected to watcher via Front-End." << std::endl;
    
    // File port will be opened only when sending a file
    std::cout << "Local sync directory: " << get_sync_dir() << std::endl;

    // 4. Initial Sync and Background Threads
    sync_with_server();

    std::thread watcher_thread(watch_server_sync, g_watcher_socket);
    std::thread inotify_thread(watch_sync_dir_inotify);
    
    init_command_callbacks(send_command, send_file);

    // 5. User Command Loop
    std::string input;
    while (true) {
        print_menu();
        std::cout << username << "> ";
        std::flush(std::cout);
        if (!std::getline(std::cin, input)) { // Lida com Ctrl+D
            watcher_running = false;
            break;
        }
        if (input == "exit") {
            watcher_running = false;
            break;
        }
        process_command(input);
    }
    
    // 6. Cleanup 
    std::cout << "\nEncerrando... Por favor, aguarde o término das threads." << std::endl;
    //  Close sockets to unblock threads from blocking network calls
    shutdown(g_watcher_socket, SHUT_RDWR);
    shutdown(g_command_socket, SHUT_RDWR);
    
   // Wait for threads to finish
    watcher_thread.join();
    inotify_thread.join();
    
    cleanup_sockets();
    
    std::cout << "Closing connection..." << std::endl;
    return 0;
}