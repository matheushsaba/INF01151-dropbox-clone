#pragma once
#include <string>
void start_primary_heartbeat_ping();                     // primary side
void start_backup_heartbeat_listener(const std::string& primary_ip); // backup side
