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
#include "command_interface.hpp"

// constexpr int PORT = 4000;

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;

int command_socket;
int watcher_socket;
int file_socket;
std::string hostname;

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

void send_file(const std::string& filename) {
    // TODO
    std::string message = "Sending file: " + filename;
    if (write(file_socket, message.c_str(), message.length()) < 0) {
        perror("ERROR writing to file socket");
    }
}

void cleanup_sockets() {
    close(command_socket);
    close(watcher_socket);
    close(file_socket);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_ip>\n";
        return 1;
    }

    hostname = argv[1];

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