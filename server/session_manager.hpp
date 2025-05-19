#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>

struct UserSession {
    int connected_devices = 0;
    std::mutex mtx;
    std::vector<int> sockets;
};

class SessionManager {
    public:
        bool try_connect(const std::string& username, int socket_fd);
        void disconnect(const std::string& username, int socket_fd);
        std::vector<int> get_user_sockets(const std::string& username);

    private:
        std::unordered_map<std::string, std::shared_ptr<UserSession>> sessions;
        std::mutex sessions_mtx; // protege o mapa
};