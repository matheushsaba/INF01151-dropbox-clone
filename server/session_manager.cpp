#include "session_manager.hpp"
#include <unistd.h>     // for close()
#include <sys/socket.h> // for shutdown()

void session_manager_register(SessionManager& manager, const std::string& username, int cmd_fd, int watch_fd) {
    auto session = std::make_shared<UserSession>();
    session->cmd_socket = cmd_fd;
    session->watch_socket = watch_fd;

    std::lock_guard<std::mutex> lock(manager.sessions_mtx);

    manager.sessions_by_cmd_fd[cmd_fd] = session;
    manager.username_by_cmd_fd[cmd_fd] = username;
    manager.device_count_by_user[username]++;
}

void session_manager_close_by_cmd_fd(SessionManager& manager, int cmd_fd) {
    std::lock_guard<std::mutex> lock(manager.sessions_mtx);

    auto session_it = manager.sessions_by_cmd_fd.find(cmd_fd);
    if (session_it == manager.sessions_by_cmd_fd.end()) return;

    auto session = session_it->second;
    shutdown(session->cmd_socket, SHUT_RDWR);
    close(session->cmd_socket);
    shutdown(session->watch_socket, SHUT_RDWR);
    close(session->watch_socket);

    manager.sessions_by_cmd_fd.erase(session_it);

    auto user_it = manager.username_by_cmd_fd.find(cmd_fd);
    if (user_it != manager.username_by_cmd_fd.end()) {
        std::string username = user_it->second;
        manager.username_by_cmd_fd.erase(user_it);

        auto count_it = manager.device_count_by_user.find(username);
        if (count_it != manager.device_count_by_user.end()) {
            count_it->second--;
            if (count_it->second <= 0) {
                manager.device_count_by_user.erase(count_it);
            }
        }
    }
}

int session_manager_get_connected(SessionManager& manager, const std::string& username) {
    std::lock_guard<std::mutex> lock(manager.sessions_mtx);
    auto it = manager.device_count_by_user.find(username);
    return (it != manager.device_count_by_user.end()) ? it->second : 0;
}

bool session_manager_try_connect(SessionManager& manager, const std::string& username, int /*handshake_fd*/) {
    std::lock_guard<std::mutex> lock(manager.sessions_mtx);
    return manager.device_count_by_user[username] < 2;
}