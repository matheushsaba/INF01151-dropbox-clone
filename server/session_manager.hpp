#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>
#include <map>

struct UserSession {
    int cmd_socket;
    int watch_socket;
};

struct SessionManager {
    std::mutex sessions_mtx;
    std::map<int, std::shared_ptr<UserSession>> sessions_by_cmd_fd;
    std::map<int, std::string> username_by_cmd_fd;
    std::map<std::string, int> device_count_by_user;
};

void session_manager_register(SessionManager& manager, const std::string& username, int cmd_fd, int watch_fd);
void session_manager_close_by_cmd_fd(SessionManager& manager, int cmd_fd);
int session_manager_get_connected(SessionManager& manager, const std::string& username);
bool session_manager_try_connect(SessionManager& manager, const std::string& username, int handshake_fd);
