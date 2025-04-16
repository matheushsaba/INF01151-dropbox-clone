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

// to connect: ./myClient <username> <server_ip_address> <port>

void print_options () {
	std::cout << "Commands: \n";
	std::cout << "# upload <path/filename.ext> \n";
/*Envia o arquivo filename.ext para o servidor, colocando-o no “sync_dir” do servidor e propagando-o para todos os dispositivos daquele usuário. e.g. upload /home/user/MyFolder/filename.ext*/
	std::cout << "# download <filename.ext> \n"; 
/*Faz uma cópia não sincronizada do arquivo filename.ext do servidor para o diretório local (de onde o servidor foi chamado). e.g. download mySpreadsheet.xlsx*/
	std::cout << "# delete <filename.ext> \n"; 
//Exclui o arquivo <filename.ext> de “sync_dir”.
	std::cout << "# list_server \n"; 
//Lista os arquivos salvos no servidor associados ao usuário.
	std::cout << "# list_client \n";
//Lista os arquivos salvos no diretório “sync_dir”
	std::cout << "# get_sync_dir \n"; 
//Cria o diretório “sync_dir” e inicia as atividades de sincronização
	std::cout << "# exit \n"; 
//Fecha a sessão com o servidor.
}

int main () {
	std::string user_input;
	print_options();
	std::cout << "Enter the command: ";
	std::getline(std::cin, user_input);
	
	auto tokens = tokenize(user_input); //sentence -> vector of strings

	if (tokens.empty()) {
		std::cout << "No command entered";
		return 1;
	}
	
	std::string command = tokens[0]; //get the first word to know what function to call
	std::vector<std::string> arguments(tokens.begin() + 1, tokens.end()); //get the arguments if there are any
	
	std::map<std::string, std::function<void(const std::vector<std::string>&)>> command_map = { 
	{"upload", handle_upload},
	{"download", handle_download},
	{"list_server", handle_list_server}
	};
	
	if (command_map.count(command)) {
		command_map[command](arguments); //calls the function and sends argument
	} else {
		std::cout << "Command unknown: " << command << std::endl;
	}
	return 0;
}
