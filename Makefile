CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -pthread
LDFLAGS = -lstdc++fs

BIN_DIR = bin
SERVER_SRC = server_dir/server_tcp.cpp
CLIENT_SRC = client/command_interface.cpp
COMMON_SRC = common/packet.cpp

SERVER_BIN = $(BIN_DIR)/server_exec
CLIENT_BIN = $(BIN_DIR)/myClient

# Diretório de armazenamento do servidor
STORAGE_DIR = server_storage
DEFAULT_USERS = testuser

# Diretório do cliente
CLIENT_SYNC_DIR = client_sync

all: setup $(SERVER_BIN) $(CLIENT_BIN)

# Cria pastas necessárias
setup:
	mkdir -p $(BIN_DIR)
	mkdir -p $(STORAGE_DIR)
	mkdir -p $(CLIENT_SYNC_DIR)
	@for user in $(DEFAULT_USERS); do \
		mkdir -p $(STORAGE_DIR)/$$user; \
		mkdir -p $(CLIENT_SYNC_DIR)/$$user; \
	done

$(SERVER_BIN): $(SERVER_SRC) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(COMMON_SRC) $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(COMMON_SRC) $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)

clean_all: clean
	rm -rf $(STORAGE_DIR) $(CLIENT_SYNC_DIR)

.PHONY: all clean clean_all setup
