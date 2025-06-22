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
#include "session_manager.hpp"
#include <map>
#include "../common/FileInfo.hpp"
#include <sys/inotify.h>
#include "heartbeat.h"
#include <atomic>
#include "election_bully.h"
#include <vector>

std::mutex file_mutex;  // Global mutex used to synchronize access to shared resources (e.g., files)
std::mutex socket_creation_mutex;

static SessionManager session_manager;
std::atomic<ServerRole> g_role;

std::string my_ip;

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
void handle_command_client(int client_socket, const std::string& username) {
    std::cout << "[RM-HANDLER] Thread 'handle_command_client' created for socket " << client_socket << std::endl;
    Packet pkt;

    while (true) {
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
            if (command.rfind("exit|", 0) == 0) {
                const char bye[] = "BYE";
                response.length = sizeof bye - 1;
                memcpy(response.payload, bye, response.length);
                send_packet(client_socket, response);   
                session_manager_close_by_cmd_fd(session_manager, client_socket);
                return;
            } else if (command.rfind("list_server", 0) == 0) {
                std::string username;
                size_t pos = command.find('|');
                if (pos != std::string::npos) {
                    username = command.substr(pos + 1);
                }

                std::string user_dir = get_sync_dir(username);
                std::vector<FileInfo> file_infos;

                for (const auto& entry : std::filesystem::directory_iterator(user_dir)) {
                    if (!entry.is_regular_file()) continue;
                    struct stat st{};
                    if (::stat(entry.path().c_str(), &st) == 0) {
                        FileInfo info{};
                        strncpy(info.name, entry.path().filename().string().c_str(), sizeof(info.name) - 1);
                        info.mtime = st.st_mtime;
                        info.ctime = st.st_ctime;
                        file_infos.push_back(info);
                    }
                }

                // Serialize the vector to a buffer
                const char* data_ptr = reinterpret_cast<const char*>(file_infos.data());
                size_t bytes_left = file_infos.size() * sizeof(FileInfo);
                int seqn = pkt.seqn;
                std::cout << "Sending file_infos to client..." << std::endl;
                while (bytes_left > 0) {
                    int chunk_size = std::min((int)bytes_left, MAX_PAYLOAD_SIZE);
                    if (chunk_size <= 0) break;
                    Packet response;
                    response.type = PACKET_TYPE_DATA;
                    response.seqn = seqn++;
                    response.length = chunk_size;
                    std::memcpy(response.payload, data_ptr, chunk_size);
                    send_packet(client_socket, response);

                    data_ptr += chunk_size;
                    bytes_left -= chunk_size;
                }

                // Optionally, send a zero-length packet to indicate end
                Packet end_pkt;
                end_pkt.type = PACKET_TYPE_END;
                end_pkt.seqn = seqn;
                end_pkt.length = 0;
                send_packet(client_socket, end_pkt);
            } else if (command.rfind("download", 0) == 0) {
                std::string filename = command.substr(9);
                std::string full_path = get_sync_dir(username) + "/" + filename;

                if (std::filesystem::exists(full_path)) {
                    std::ifstream file(full_path, std::ios::binary);
                    if (file) {
                        char buffer[MAX_PAYLOAD_SIZE];
                        int seqn = pkt.seqn;
                        while (file) {
                            file.read(buffer, MAX_PAYLOAD_SIZE);
                            std::streamsize bytes_read = file.gcount();
                            if (bytes_read > 0) {
                                Packet response;
                                response.type = PACKET_TYPE_DATA;
                                response.seqn = seqn++;
                                response.length = bytes_read;
                                std::memcpy(response.payload, buffer, bytes_read);
                                send_packet(client_socket, response);
                            }
                        }
                        // Send a zero-length packet to indicate end of file
                        Packet end_pkt;
                        end_pkt.type = PACKET_TYPE_END;
                        end_pkt.seqn = seqn;
                        end_pkt.length = 0;
                        send_packet(client_socket, end_pkt);
                    } else {
                        const char* reply = "Erro ao abrir o arquivo.";
                        std::memcpy(response.payload, reply, response.length);
                        response.length = strlen(reply);
                        send_packet(client_socket, response);
                    }
                } else {
                    const char* reply = "Arquivo não encontrado.";
                    std::memcpy(response.payload, reply, response.length);
                    response.length = strlen(reply);
                    send_packet(client_socket, response);
    }
            } else if (command.rfind("delete", 0) == 0) {
                std::string filename = command.substr(7);
                std::string full_path = get_sync_dir(username) + "/" + filename;

                if (std::filesystem::remove(full_path)) {
                    const char* reply = "Arquivo deletado com sucesso.";
                    response.length = strlen(reply);
                    std::memcpy(response.payload, reply, response.length);
                    send_packet(client_socket, response);
                } else {
                    const char* reply = "Erro ao deletar o arquivo.";
                    response.length = strlen(reply);
                    std::memcpy(response.payload, reply, response.length);
                    send_packet(client_socket, response);
            }
            } else {
                const char* reply = "Comando desconhecido ou não implementado.";
                response.length = strlen(reply);
                std::memcpy(response.payload, reply, response.length);
                send_packet(client_socket, response);
            }
        }

        //send_packet(client_socket, response);
    }
}

// Listens for updates, could monitor for file changes
void handle_watcher_client(int client_socket, const std::string& dir) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[watcher] Failed to initialize inotify\n";
        return;
    }

    int wd = inotify_add_watch(fd, dir.c_str(), IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        std::cerr << "[watcher] Failed to add watch for " << dir << "\n";
        close(fd);
        return;
    }

    std::cout << "[watcher] Watching directory: " << dir << "\n";

    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;

    while (true) {
        len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        for (char* ptr = buf; ptr < buf + len; ) {
            struct inotify_event* event = (struct inotify_event*) ptr;
            if (event->len) {
                Packet notify_pkt{};
                notify_pkt.type = PACKET_TYPE_NOTIFY; // Define this in your protocol
                std::string msg = "Change: ";
                if (event->mask & IN_CREATE) msg += "Created ";
                if (event->mask & IN_MODIFY) msg += "Modified ";
                if (event->mask & IN_DELETE) msg += "Deleted ";
                if (event->mask & IN_MOVED_FROM) msg += "Moved from ";
                if (event->mask & IN_MOVED_TO) msg += "Moved to ";
                msg += event->name;
                notify_pkt.length = msg.size();
                memcpy(notify_pkt.payload, msg.c_str(), notify_pkt.length);
                std::cout << "sending notify: " << msg << std::endl;
                if (event->mask & IN_DELETE || event->mask & IN_MOVED_FROM) {
                    notify_pkt.type = PACKET_TYPE_DELETE;
                }
                send_packet(client_socket, notify_pkt);
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
    inotify_rm_watch(fd, wd);
    close(fd);
}

void handle_file_client(int client_socket) {
    std::cout << "[RM-HANDLER] Thread 'handle_file_client' iniciada para o socket " << client_socket << std::endl;

    Packet pkt;

    while (true) {                               // ❶ laço externo = 1-conexão / N-arquivos
        /* ---------- 1. Cabeçalho “putfile|<user>|<fname>” ---------- */
        if (!recv_packet(client_socket, pkt)) {          // EOF ou erro → fecha conexão
            std::cerr << "End of connection.\n";
            break;
        }
        if (pkt.type != PACKET_TYPE_CMD) {               // protocolo inesperado
            std::cerr << "Invalid packet type.\n";
            break;
        }

        std::string header(pkt.payload, pkt.length);
        if ((header.rfind("putfile|", 0) != 0) && header.rfind("delfile|", 0) != 0) {          // qualquer outro comando → encerra
            std::cerr << "Malformed header: " << header << '\n';
            break;
        }

        /* Extrai user e filename */
        size_t p1 = header.find('|');
        size_t p2 = header.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) {
            std::cerr << "Malformed header: " << header << '\n';
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
                    std::cout << "[DELETE] " << username << '/' << filename << " removed.\n";
                } else {
                    std::cerr << "Error removing " << full_path << '\n';
                }
            } else {
                std::cerr << "File to delete not found: " << full_path << '\n';
            }
            goto close_connection; 
        }
        {
            std::lock_guard<std::mutex> lock(file_mutex);   // protege criação do dir/arquivo
            std::ofstream out(full_path, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "Not possible to create " << full_path << '\n';
                break;
            }

            std::cout << "[UPLOAD] " << username << '/' << filename << '\n';

            /* ---------- 3. Blocos DATA até length==0 ---------- */
            while (true) {
                if (!recv_packet(client_socket, pkt)) {      // drop inesperado
                    std::cerr << "Connection lost during upload.\n";
                    goto close_connection;                        // sai do dois níveis
                }
                if (pkt.type != PACKET_TYPE_DATA) {
                    std::cerr << "Got a type other than DATA.\n";
                    goto close_connection;
                }
                if (pkt.length == 0) {                       // marcador EOF
                    std::cout << "Upload successful (" << filename << ").\n";
                    break;                                   // volta ao laço externo p/ próximo arquivo
                }
                out.write(pkt.payload, pkt.length);
                if (!out) {
                    std::cerr << "Error writing at disk.\n";
                    goto close_connection;
                }
            }
        }   // mutex liberado aqui — permite outros uploads em paralelo
        continue;                                           // pronto p/ novo cabeçalho
    }

close_connection:
    close(client_socket);
}

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
            Packet pkt;
            if (!recv_packet(client_fd, pkt) || pkt.type != PACKET_TYPE_CMD) {
                std::cerr << "❌ Error: failed to receive handshake packet.\n";
                close(client_fd);
                return;
            }

            std::string username(pkt.payload, pkt.length);
            std::cout << "🔗 Connection attempt from user: " << username << std::endl;

            // Wait here if more than 2 devices are already active
            auto& ctrl = user_controls[username];
            {
                std::unique_lock<std::mutex> lock(ctrl.mtx);
                while (ctrl.active_sessions >= 2) {
                    std::cout << "⏳ Waiting for slot for user: " << username << '\n';
                    ctrl.cv.wait(lock);
                }
                ctrl.active_sessions++;
            }

            // Send ACK first
            Packet ack{};
            ack.type = PACKET_TYPE_ACK;
            std::string ok_msg = "OK";
            ack.length = ok_msg.size();
            memcpy(ack.payload, ok_msg.c_str(), ack.length);
            send_packet(client_fd, ack);

            // Allocate dynamic ports for watcher, file, and command connections
            int cmd_port, watch_port, file_port;
            int cmd_sock   = create_dynamic_socket(cmd_port);
            int watch_sock = create_dynamic_socket(watch_port);
            int file_sock  = create_dynamic_socket(file_port);
            std::cout << "[RM] Sessão para " << username << ": Porta de Comando=" << cmd_port 
             << ", Porta de Watcher=" << watch_port << ", Porta de Arquivo=" << file_port << std::endl;
            std::cerr << "🔗 Command socket: " << cmd_sock << '\n';
            std::cerr << "🔗 Watcher socket: " << watch_sock << '\n';
            std::cerr << "🔗 File transfer socket: " << file_sock << '\n';

            if (cmd_sock < 0 || watch_sock < 0 || file_sock < 0) {
                std::cerr << "❌ Failed to create dynamic sockets.\n";
                close(client_fd);
                return;
            }

            // Send dynamic ports to client in format: cmd|watch|file
            std::string ports_msg = std::to_string(cmd_port) + "|" + std::to_string(watch_port) + "|" + std::to_string(file_port);
            Packet reply{};
            reply.type = PACKET_TYPE_CMD;
            reply.length = ports_msg.size();
            memcpy(reply.payload, ports_msg.c_str(), reply.length);
            send_packet(client_fd, reply);

            close(client_fd); // Always close handshake socket

            // Accept follow-up connections from the client on the 3 dynamic sockets
            sockaddr_in tmp{};
            socklen_t tmp_len = sizeof(tmp);
            int cmd_client_fd   = accept(cmd_sock,   reinterpret_cast<sockaddr*>(&tmp), &tmp_len);
            int watch_client_fd = accept(watch_sock, reinterpret_cast<sockaddr*>(&tmp), &tmp_len);

            session_manager_register(session_manager, username, cmd_client_fd, watch_client_fd);

            // Detach watchers for command and watcher as before
            std::thread([watch_client_fd, username]() {
                handle_watcher_client(watch_client_fd, get_sync_dir(username));
            }).detach();
            std::thread([cmd_client_fd, username]() {
                handle_command_client(cmd_client_fd, username);

                // Cleanup on disconnect
                auto& ctrl = user_controls[username];
                {
                    std::lock_guard<std::mutex> lock(ctrl.mtx);
                    ctrl.active_sessions--;
                }
                ctrl.cv.notify_one(); // Wake up one blocked connection
                std::cout << "👋 Session ended for " << username << '\n';
            }).detach();

            // Start a thread that loops and handles multiple file uploads
            std::thread([file_sock]() {
                while (true) {
                    sockaddr_in tmp{};
                    socklen_t tmp_len = sizeof(tmp);
                    int file_client_fd = accept(file_sock, reinterpret_cast<sockaddr*>(&tmp), &tmp_len);
                    if (file_client_fd < 0) {
                        perror("accept failed on file socket");
                        continue;
                    }
                    std::thread(handle_file_client, file_client_fd).detach();
                }
            }).detach();

            close(cmd_sock);
            close(watch_sock);
            std::cout << "✅ User " << username << " fully connected (CMD/WATCH/FILE sockets established)\n";

        }).detach();
    }
}

int start_primary_server_client_connections() {
    std::cout << std::unitbuf;

    // AF_INET for ipv4, SOCK_STREAM for TCP and 0 for default protocol. Slide 17 Aula-11
    int listener_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_socket < 0) {
        perror("Error opening listener socket");
        return -1;
    }

    // Set the SO_REUSEADDR socket option. This allows the server's listener_socket
    // to bind to its designated address and port (e.g., port 4000) immediately
    // after a previous instance of the server using that same port has been closed.
    // Without this, the port might remain in a TIME_WAIT state, preventing a quick
    // restart and causing "Address already in use" errors
    int option = 1;
    if (setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
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

void run_as_primary() {
    // Start listening for connections of backup servers and sending 
    // heartbeats to the ones already connected
    start_primary_heartbeat_ping();

    // Start listening to client connections
    start_primary_server_client_connections();

    // It should never return
}

void promote_to_primary() 
{
    ServerRole expected = ROLE_BACKUP;
    if (!g_role.compare_exchange_strong(expected, ROLE_PRIMARY))
    {
        return; // we were already primary
    }

    std::cout << "\n[PROMOTE] Backup became PRIMARY – switching services\n";

    // The main thread in run_as_backup() will detect this role change
    // and transition to run_as_primary(). This function, called from
    // the election thread, should now simply return and terminate.
}

void run_as_backup(const std::string& primary_ip) {
    // Connects to the primary server via its ip and starts
    // listening for its heartbeats
    start_backup_heartbeat_listener(primary_ip);
    
    // TODO: Listen for replication data

    // Stay alive until elected as new primary in a leader election
    while (g_role.load() == ROLE_BACKUP)
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    // TODO: should stop listening for replicator data here
    // If the while loop exits, it means we have been promoted.
    // The main thread now takes on the primary role.
    run_as_primary();
}

static void usage(const char* prog_name)
{
    std::cerr << "Usage:\n";
    std::cerr << "  Primary: " << prog_name << " -p --ip <self_ip> --frontend-ip <fe_ip>\n";
    std::cerr << "  Backup:  " << prog_name << " -b <primary_ip> --ip <self_ip> --frontend-ip <fe_ip>\n";
    std::cerr << "Example (Primary): " << prog_name << " -p --ip 192.168.1.10 --frontend-ip 127.0.0.1\n";
    std::cerr << "Example (Backup):  " << prog_name << " -b 192.168.1.10 --ip 192.168.1.11 --frontend-ip 127.0.0.1\n";
}

int main(int argc, char* argv[])
{
    // Minimum args:
    // Primary: server -p --ip <self_ip> --frontend-ip <fe_ip> (6 args)
    // Backup:  server -b <primary_ip> --ip <self_ip> --frontend-ip <fe_ip> (7 args)
    if (argc < 6)
    { 
        usage(argv[0]); 
        return 1; 
    }

    std::string role_flag;
    std::string primary_ip;
    std::string self_ip;
    std::string frontend_ip;

    // Loop through the command-line arguments.
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        // Check if the argument is a role flag.
        if (arg == "-p" || arg == "-b")
        {
            role_flag = arg;
            if (arg == "-b")
            {
                // Check if there's another argument after -b for the primary_ip.
                if (++i >= argc) 
                {
                    usage(argv[0]); 
                    return 1; 
                }
                primary_ip = argv[i];
            }
        }
        else if (arg == "--ip") // Check if the argument is the --ip flag.
        {
            // Check if there's another argument after --ip for the self_ip. 
            if (++i >= argc)
            { 
                usage(argv[0]); 
                return 1; 
            }
            self_ip = argv[i];
        }
        else if (arg == "--frontend-ip")
        {
            // Check if there's another argument after --frontend-ip for the frontend_ip.
            if (++i >= argc)
            {
                usage(argv[0]);
                return 1;
            }
            frontend_ip = argv[i];
        }
        else    // unknown token
        {
            // If an unrecognized argument is found, print usage and exit.
            usage(argv[0]); 
            return 1;
        }
    }

    // Ensure that the --ip argument was provided.
    if (self_ip.empty()) 
    { 
        std::cerr << "--ip is required\n"; 
        std::cerr << "Error: --ip is a required argument.\n";
        usage(argv[0]);
        return 1; 
    }
    // Ensure that the --frontend-ip argument was provided.
    if (frontend_ip.empty())
    {
        std::cerr << "Error: --frontend-ip is a required argument.\n";
        usage(argv[0]);
        return 1;
    }
    my_ip = self_ip;                     // Store self_ip in the global variable for use in other parts of the server.

    // Initialize the Bully election algorithm listener with this server's IP.
    bully_init(my_ip);
    bully_set_frontend_ip(frontend_ip);

    // Determine the server's role based on the parsed role_flag.
    if (role_flag == "-p")
    {
        g_role.store(ROLE_PRIMARY); // Set the global role to primary
        std::cout << "Starting as PRIMARY on " << my_ip << '\n';
        run_as_primary(); // Blocks indefinitely
    }
    else if (role_flag == "-b")
    {
        std::cout << "Starting as BACKUP on " << my_ip
                  << "  (primary = " << primary_ip << ")\n";
        g_role.store(ROLE_BACKUP);  // Set the global role to backup
        run_as_backup(primary_ip);  // Blocks until this server is promoted
    }
    else
    {
        // If no valid role flag (-p or -b) was provided, print usage and exit.
        std::cerr << "Error: Missing role flag -p or -b.\n";
        usage(argv[0]);
        return 1;
    }
}