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

void handle_upload(const std::vector<std::string>& args){
    if (args.empty()) {
        std::cout << "Usage: upload <filename>\n";
        return;
    }
    // Chama a função do client para enviar o arquivo
    send_file_function(args[0]);
}

void handle_download(const std::vector<std::string>& args){
	(void)args;
	std::cout << "handle download \n";
}

void handle_list_server(const std::vector<std::string>& args){
 
}

void handle_delete(const std::vector<std::string>& args){
	(void)args;
	std::cout << "handle delete \n";
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
	//list_client and get_sync_dir: commands executed in the server
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
		{"exit", handle_exit}
	};

	auto tokens = tokenize(user_input); //sentence -> vector of strings

	if (tokens.empty()) {
		std::cout << "No command entered";
	}
	
	std::string command = tokens[0]; //get the first word to know what function to call
	std::vector<std::string> arguments(tokens.begin() + 1, tokens.end()); //get the arguments if there are any

	if (command_map.count(command)) {
		command_map[command](arguments); //calls the function and sends argument
	} else {
		std::cout << "Command unknown: " << command << std::endl;
	}
}
