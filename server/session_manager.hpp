#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>



// struct UserSession {
//     int connected_devices = 0;
//     std::mutex mtx;
//     std::vector<int> sockets;
// };

struct UserSession {
    int cmd_socket;
    int watch_socket;
};

class SessionManager {
    public:
        void register_session(const std::string& username, int cmd_fd, int watch_fd);
        void close_session_by_cmd_fd(int cmd_fd);
        int  get_connected_devices(const std::string& username);
        bool try_connect(const std::string& username, int handshake_fd);

    private:
        // Device-specific session management
        std::unordered_map<int, std::shared_ptr<UserSession>> sessions_by_cmd_fd;

        // Track device count per username
        std::unordered_map<std::string, int> device_count_by_user;

        // Track which username belongs to which session
        std::unordered_map<int, std::string> username_by_cmd_fd;

        std::mutex sessions_mtx;
};