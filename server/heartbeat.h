#pragma once
#include <atomic>
#include <string>
#include <atomic>
void start_primary_heartbeat_ping();
void start_backup_heartbeat_listener(const std::string& primary_ip);
void promote_to_primary();

// Global, thread-safe variable to hold the current server role.
// std::atomic ensures that reads and writes are safe across different threads.
enum ServerRole { ROLE_PRIMARY, ROLE_BACKUP };
extern std::atomic<ServerRole> g_role;           // run-time role, can switch once