#pragma once
#include <string>
#include <functional>
#include <vector>
#include "../common/FileInfo.hpp"

std::string move_file_to_sync_dir(const std::string& source_path);
std::string download_from_sync_dir(const std::string& filename);
bool delete_from_sync_dir(const std::string& filename);
std::vector<FileInfo> list_client_sync_dir();
std::vector<FileInfo> get_server_sync_dir();

void init_command_callbacks(
    std::function<void(const std::string&)> send_command_cb,
    std::function<void(const std::string&)> send_file_cb
    );

void process_command(const std::string& user_input);
void print_menu();