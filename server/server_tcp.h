#pragma once

#include <string>
#include <mutex>
#include <atomic>

// enum para o papel do servidor
enum ServerRole { ROLE_PRIMARY, ROLE_BACKUP };

// variáveis globais definidas em server_tcp.cpp
extern std::mutex file_mutex;
extern std::mutex socket_creation_mutex;
extern std::atomic<ServerRole> g_role;
extern std::string my_ip;

// declarações de funções
std::string get_sync_dir(const std::string& username);

void handle_command_client(int client_socket, const std::string& username);
void handle_watcher_client(int client_socket, const std::string& dir);
void handle_file_client(int client_socket);

int create_dynamic_socket(int& port_out);
void handle_new_connection(int listener_socket);

int start_primary_server_client_connections();
void run_as_primary();
void promote_to_primary();
void run_as_backup(const std::string& primary_ip);
