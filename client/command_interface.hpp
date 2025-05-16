#pragma once
#include <string>
#include <functional>

std::string move_file_to_sync_dir(const std::string& source_path);

void init_command_callbacks(
    std::function<void(const std::string&)> send_command_cb,
    std::function<void(const std::string&)> send_file_cb
    );

void process_command(const std::string& user_input);
void print_menu();