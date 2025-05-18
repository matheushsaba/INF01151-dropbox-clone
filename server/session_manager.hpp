#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <memory>

struct UserSession {
    int connected_devices = 0;
    std::mutex mtx;
};

class SessionManager {
    public:
        bool try_connect(const std::string& username);
        void disconnect(const std::string& username);

    private:
        std::unordered_map<std::string, std::shared_ptr<UserSession>> sessions;
        std::mutex sessions_mtx; // protege o mapa
};