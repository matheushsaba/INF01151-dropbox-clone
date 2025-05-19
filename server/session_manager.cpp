#include "session_manager.hpp"
#include <unistd.h>     // for close()
#include <sys/socket.h> // for shutdown()

void SessionManager::register_session(const std::string& username, int cmd_fd, int watch_fd)
{
    auto session = std::make_shared<UserSession>();
    session->cmd_socket = cmd_fd;
    session->watch_socket = watch_fd;

    std::lock_guard<std::mutex> lock(sessions_mtx);

    sessions_by_cmd_fd[cmd_fd] = session;
    username_by_cmd_fd[cmd_fd] = username;
    device_count_by_user[username]++;
}

void SessionManager::close_session_by_cmd_fd(int cmd_fd)
{
    std::lock_guard<std::mutex> lock(sessions_mtx);

    auto session_it = sessions_by_cmd_fd.find(cmd_fd);
    if (session_it == sessions_by_cmd_fd.end()) return;

    auto session = session_it->second;

    // Close both sockets for this device
    shutdown(session->cmd_socket, SHUT_RDWR);
    close(session->cmd_socket);

    shutdown(session->watch_socket, SHUT_RDWR);
    close(session->watch_socket);

    sessions_by_cmd_fd.erase(session_it);

    auto user_it = username_by_cmd_fd.find(cmd_fd);
    if (user_it != username_by_cmd_fd.end()) {
        std::string username = user_it->second;
        username_by_cmd_fd.erase(user_it);

        auto count_it = device_count_by_user.find(username);
        if (count_it != device_count_by_user.end()) {
            count_it->second--;
            if (count_it->second <= 0) {
                device_count_by_user.erase(count_it);
            }
        }
    }
}

int SessionManager::get_connected_devices(const std::string& username)
{
    std::lock_guard<std::mutex> lock(sessions_mtx);
    auto it = device_count_by_user.find(username);
    if (it != device_count_by_user.end()) {
        return it->second;
    }
    return 0;
}

bool SessionManager::try_connect(const std::string& username, int handshake_fd)
{
    std::lock_guard<std::mutex> lock(sessions_mtx);

    int connected_devices = device_count_by_user[username];
    if (connected_devices >= 2) {
        return false;
    }

    // Just a pre-check — no session is created yet here
    return true;
}