# Compilador
CXX = g++
CXXFLAGS = -Wall -std=c++17

# Diretórios
CLIENT_DIR = client
SERVER_DIR = server
COMMON_DIR = common

# Fontes
CLIENT_SRC = $(CLIENT_DIR)/client_tcp.cpp \
             $(CLIENT_DIR)/command_interface.cpp \
             $(COMMON_DIR)/packet.cpp

SERVER_SRC = $(SERVER_DIR)/server_tcp.cpp \
             $(COMMON_DIR)/packet.cpp

# Objetos
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)

# Binaries
CLIENT_BIN = myClient
SERVER_BIN = myServer

# Targets
.PHONY: all clean

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SERVER_BIN): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(CLIENT_BIN) $(SERVER_BIN) $(CLIENT_OBJ) $(SERVER_OBJ)
