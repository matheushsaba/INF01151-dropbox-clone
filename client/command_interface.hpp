#pragma once
#include <string>
#include <functional>

void init_command_callbacks(
    std::function<void(const std::string&)> send_command_cb,
    std::function<void(const std::string&)> send_file_cb
    );

void process_command(const std::string& user_input);
void print_menu();