#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <functional>
#include <vector>
#include "command_interface.hpp"

//pointers to the client functions, initialized by init_command_callbacks
static std::function<void(const std::string&)> send_command_function;
static std::function<void(const std::string&)> send_file_function;

void init_command_callbacks(
    std::function<void(const std::string&)> send_command_cb,
    std::function<void(const std::string&)> send_file_cb
) {
    send_command_function = std::move(send_command_cb);
    send_file_function = std::move(send_file_cb);
}

std::vector<std::string> tokenize(const std::string& input) {
	std::stringstream ss(input);
	std::string token;
	std::vector<std::string> tokens;
	
	while (ss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

void handle_help(const std::vector<std::string>&) {
    print_menu();
}

void handle_upload(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: upload <filename>\n";
        return;
    }

    const std::string& path = args[0];
    std::cout << "Uploading file: " << path << "\n";

    std::string sync_path = move_file_to_sync_dir(path);
    if (!sync_path.empty()) {
        send_file_function(sync_path); // Upload from sync_dir
    }
}

void handle_download(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: download <filename>\n";
        return;
    }

    const std::string& filename = args[0];
    std::cout << "Downloading file: " << filename << "\n";

    std::string result = download_from_sync_dir(filename);
    if (result.empty()) {
        std::cout << "Falha no download.\n";
    }
}

void handle_list_server(const std::vector<std::string>& args){
 
}

void handle_list_client(const std::vector<std::string>&) {
    list_client_sync_dir();
}

void handle_delete(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: delete <filename>\n";
        return;
    }

    const std::string& filename = args[0];
    std::cout << "Deletando arquivo: " << filename << "\n";

    if (!delete_from_sync_dir(filename)) {
        std::cout << "Falha ao deletar o arquivo.\n";
    }
}

void handle_exit(const std::vector<std::string>& args){
	(void)args;
	std::cout << "handle exit \n";
}

// to connect: ./myClient <username> <server_ip_address> <port>

void print_options () {
	std::cout << "Commands: \n";
	std::cout << "# upload <path/filename.ext> \n"; //send file to sync_dir (server) and then send them to the devices
	std::cout << "# download <filename.ext> \n"; //unsyncronized copy (download) of the file to local directory
	std::cout << "# delete <filename.ext> \n"; //delete the file from sync_dir
	std::cout << "# list_server \n"; //list all user files in the server
	std::cout << "# list_client \n"; //list all user files in the client
	//get_sync_dir: command executed in the server
	std::cout << "# exit \n"; // close section with the server
}

void print_menu(){
	print_options();
	std::cout << "Enter the command: ";
}

void process_command(const std::string& user_input){

	static std::map<std::string, std::function<void(const std::vector<std::string>&)>> command_map = { 
		{"upload", handle_upload},
		{"download", handle_download},
		{"delete", handle_delete},
		{"list_server", handle_list_server},
		{"list_client", handle_list_client},
		{"exit", handle_exit},
        {"help", handle_help},
	};

	auto tokens = tokenize(user_input); //sentence -> vector of strings

	if (tokens.empty()) {
		std::cout << "No command entered";
        return;
	}
	
	std::string command = tokens[0]; //get the first word to know what function to call
	std::vector<std::string> arguments(tokens.begin() + 1, tokens.end()); //get the arguments if there are any

	if (command_map.count(command)) {
		command_map[command](arguments); //calls the function and sends argument
	} else {
		std::cout << "Unknown command: \"" << command << "\". Type 'help' to see available commands.\n";
	}
}
