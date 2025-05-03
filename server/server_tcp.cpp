// server_tcp.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

// constexpr int PORT = 4000;
// std::mutex file_mutex; // Usamos mutex para sincronizar threads em processos diferentes

constexpr int COMMAND_PORT = 4000;
constexpr int WATCHER_PORT = 4001;
constexpr int FILE_PORT = 4002;
std::mutex file_mutex;  // Global mutex used to synchronize access to shared resources (e.g., files)

// Function to deal with simple command messages
void handle_command_client(int client_socket) {
    char buffer[256]{}; // Buffer to store incoming command
    int n = read(client_socket, buffer, 255); // Read data from the client

    if (n <= 0) {
        perror("ERROR reading from command socket");
        close(client_socket);
        return;
    }

    std::cout << "Command received: " << buffer << std::endl;

    {
        // Lock to ensure thread-safe handling of shared resources
        std::lock_guard<std::mutex> lock(file_mutex);

        // Prepare and send a response back to the client
        const char* reply = "Command received and processed.\n";
        if (write(client_socket, reply, strlen(reply)) < 0) {
            perror("ERROR writing to command socket");
        }
    }

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

// Deals with file transfer logic
void handle_file_client(int client_socket) {
    char buffer[256]{};
    
    while (true) {
        int n = read(client_socket, buffer, 255);
        if (n <= 0) {
            perror("ERROR reading from file socket");
            break;
        }

        std::cout << "File transfer: " << buffer << std::endl;
        // TODO
        // Here you would implement the file transfer logic
    }

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