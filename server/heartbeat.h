#pragma once
#include <string>
void start_primary_heartbeat_ping();
void start_backup_heartbeat_listener(const std::string& primary_ip);
void promote_to_primary();