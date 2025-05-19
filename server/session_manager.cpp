#include "session_manager.hpp"

bool SessionManager::try_connect(const std::string& username, int socket_fd) {
    std::shared_ptr<UserSession> session;

    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        if (!sessions.count(username)) {
            sessions[username] = std::make_shared<UserSession>();
        }
        session = sessions[username];
    }

    std::lock_guard<std::mutex> lock(session->mtx);
    if (session->connected_devices >= 2) return false;
    session->connected_devices++;
    session->sockets.push_back(socket_fd);
    return true;
}

void SessionManager::disconnect(const std::string& username, int socket_fd) {
    std::shared_ptr<UserSession> session;

    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        auto it = sessions.find(username);
        if (it == sessions.end()) return;
        session = it->second;
    }

    bool should_erase = false;
    {
        std::lock_guard<std::mutex> lock(session->mtx);
        
        if (session->connected_devices > 0) {
            session->connected_devices--;
            auto it = std::remove(session->sockets.begin(), session->sockets.end(), socket_fd);
            session->sockets.erase(it, session->sockets.end());
            if (session->connected_devices == 0) {
                should_erase = true;
            }
        }
    }

    if (should_erase) {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions.erase(username);
    }
}

std::vector<int> SessionManager::get_user_sockets(const std::string& username) {
    std::shared_ptr<UserSession> session;
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        auto it = sessions.find(username);
        if (it == sessions.end()) return {};
        session = it->second;
    }
    std::lock_guard<std::mutex> lock(session->mtx);
    return session->sockets;
}