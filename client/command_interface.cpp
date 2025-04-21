#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <functional>

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
	std::cout << "handle upload: ";
	std::ostringstream oss;
	for (const auto& arg : args) {
		oss << arg << " ";
	}
	std::string args_str = oss.str();
	std::cout << args_str << "\n";
}

void handle_download(const std::vector<std::string>& args){
	std::cout << "handle download \n";
}

void handle_list_server(const std::vector<std::string>& args){
	std::cout << "handle list_server \n";
}

void handle_delete(const std::vector<std::string>& args){
	std::cout << "handle delete \n";
}

void handle_exit(const std::vector<std::string>& args){
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

int main () {

	std::map<std::string, std::function<void(const std::vector<std::string>&)>> command_map = { 
		{"upload", handle_upload},
		{"download", handle_download},
		{"delete", handle_delete},
		{"list_server", handle_list_server},
		{"exit", handle_exit}
	};

	std::string user_input;
	print_menu();
	std::getline(std::cin, user_input);

	auto tokens = tokenize(user_input); //sentence -> vector of strings

	if (tokens.empty()) {
		std::cout << "No command entered";
		return 1;
	}
	
	std::string command = tokens[0]; //get the first word to know what function to call
	std::vector<std::string> arguments(tokens.begin() + 1, tokens.end()); //get the arguments if there are any

	if (command_map.count(command)) {
		command_map[command](arguments); //calls the function and sends argument
	} else {
		std::cout << "Command unknown: " << command << std::endl;
	}
	return 0;
}
