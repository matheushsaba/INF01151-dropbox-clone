#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <functional>
#include "../common/packet.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <filesystem>
#include <fstream>



std::vector<std::string> tokenize(const std::string& input) {
	std::stringstream ss(input);
	std::string token;
	std::vector<std::string> tokens;
	
	while (ss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

void handle_upload(const std::vector<std::string>& args, const std::string& server_ip, int port) {
    if (args.empty()) {
        std::cerr << "Erro: Nenhum caminho de arquivo fornecido.\n";
        return;
    }
    
    std::string file_path = args[0]; // O primeiro argumento é o caminho do arquivo
    std::ifstream file(file_path, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Erro ao abrir o arquivo: " << file_path << std::endl;
        return;
    }

    std::cout << "Iniciando upload do arquivo: " << file_path << std::endl;

    // Criando o socket e conectando ao servidor
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar ao servidor");
        close(sockfd);
        return;
    }

    // Enviar o nome do arquivo para o servidor
    Packet pkt;
    pkt.type = PACKET_TYPE_CMD;
    pkt.seqn = 1;
    std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1); // Extrai o nome do arquivo
    pkt.length = filename.size();
    std::memcpy(pkt.payload, filename.c_str(), pkt.length);

    if (!send_packet(sockfd, pkt)) {
        std::cerr << "Erro ao enviar o nome do arquivo para o servidor.\n";
        close(sockfd);
        return;
    }

    // Enviar o conteúdo do arquivo em pacotes
    char buffer[MAX_PAYLOAD_SIZE];
    int seqn = 1;

    while (file.read(buffer, MAX_PAYLOAD_SIZE) || file.gcount() > 0) {
        pkt.type = PACKET_TYPE_DATA;
        pkt.seqn = seqn++;
        pkt.length = file.gcount();
        std::memcpy(pkt.payload, buffer, pkt.length);

        if (!send_packet(sockfd, pkt)) {
            std::cerr << "Erro ao enviar o pacote de dados para o servidor.\n";
            break;
        }
    }

    // Enviar um pacote com 0 de comprimento para indicar o fim do upload
    pkt.type = PACKET_TYPE_DATA;
    pkt.seqn = seqn;
    pkt.length = 0;
    send_packet(sockfd, pkt); // Indica o fim do arquivo

    std::cout << "Upload concluído com sucesso.\n";

    // Fechar o socket
    close(sockfd);
}

void handle_download(const std::vector<std::string>& args){
	(void)args;
	std::cout << "handle download \n";
}

void handle_list_server(const std::vector<std::string>& args, const std::string& username, const std::string& server_ip, int port){
	 int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar no servidor");
        close(sockfd);
        return;
    }

    Packet pkt{};
    pkt.type = PACKET_TYPE_CMD;
    pkt.seqn = 1;
    pkt.total_size = 0;
    pkt.length = snprintf(pkt.payload, MAX_PAYLOAD_SIZE, "list_server|%s", username.c_str());

    if (!send_packet(sockfd, pkt)) {
        std::cerr << "Erro ao enviar comando list_server.\n";
        close(sockfd);
        return;
    }

    Packet response;
    if (recv_packet(sockfd, response)) {
        std::string filelist(response.payload, response.length);
        std::cout << "[Arquivos no servidor]\n" << filelist << std::endl;
    } else {
        std::cerr << "Erro ao receber resposta do servidor.\n";
    }
	(void)args;
    close(sockfd);
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

int main (int argc, char* argv[]) {
	if (argc != 4) {
        std::cerr << "Uso: ./myClient <username> <server_ip_address> <port>\n";
        return 1;
    }

    std::string username = argv[1];
    std::string server_ip = argv[2];
    int port = std::stoi(argv[3]);

	std::map<std::string, std::function<void(const std::vector<std::string>&)>> command_map = { 
		{"upload", std::bind(handle_upload, std::placeholders::_1, server_ip, port)},
		{"download", handle_download},
		{"delete", handle_delete},
		{"list_server", std::bind(handle_list_server, std::placeholders::_1, username, server_ip, port)},
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
