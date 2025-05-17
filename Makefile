# CXX = g++
# CXXFLAGS = -std=c++17 -Wall -Wextra -g -pthread

# BIN_DIR = bin
# SERVER_SRC = server/server_tcp.cpp
# CLIENT_SRC = client/client_tcp.cpp client/command_interface.cpp
# COMMON_SRC = common/packet.cpp

# SERVER_BIN = $(BIN_DIR)/server_exec
# CLIENT_BIN = $(BIN_DIR)/myClient

# # Diretório de armazenamento do servidor
# STORAGE_DIR = server_storage

# # Diretório do cliente
# CLIENT_DIR = client_storage

# all: setup $(SERVER_BIN) $(CLIENT_BIN)

# # Cria pastas necessárias
# setup:
# 	mkdir -p $(BIN_DIR) $(STORAGE_DIR) $(CLIENT_DIR)

# $(SERVER_BIN): $(SERVER_SRC) $(COMMON_SRC)
# 	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(COMMON_SRC) $(LDFLAGS)

# $(CLIENT_BIN): $(CLIENT_SRC) $(COMMON_SRC)
# 	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(COMMON_SRC) $(LDFLAGS)

# clean:
# 	rm -rf $(BIN_DIR)

# clean_all: clean
# 	 rm -rf $(STORAGE_DIR) $(CLIENT_DIR)

# .PHONY: all clean clean_all setup

# Makefile – tidy out‑of‑source build that mirrors the source tree
# Works with the file structure:
#   client/*.cpp   server/*.cpp   common/*.cpp
#   objects → build/obj/<same‑subdir>/*.o
#   binaries → bin/
# ---------------------------------------------------------------------------
# Makefile – tidy out‑of‑source build (mirrors source tree)
# Removes client_storage/ and server_storage on clean
# ---------------------------------------------------------------------------
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -pthread

BUILD_DIR    := build
OBJDIR       := $(BUILD_DIR)/obj
BINDIR   := bin
STORAGEDIRS := client_storage server_storage

# ---------------------------------------------------------------------------
#  Collect sources
# ---------------------------------------------------------------------------
CLIENT_SRCS := $(wildcard client/*.cpp)
SERVER_SRCS := $(wildcard server/*.cpp)
COMMON_SRCS := $(wildcard common/*.cpp)

CLIENT_OBJS := $(patsubst client/%.cpp,$(OBJDIR)/client/%.o,$(CLIENT_SRCS))
SERVER_OBJS := $(patsubst server/%.cpp,$(OBJDIR)/server/%.o,$(SERVER_SRCS))
COMMON_OBJS := $(patsubst common/%.cpp,$(OBJDIR)/common/%.o,$(COMMON_SRCS))

CLIENT_BIN  := $(BINDIR)/myClient
SERVER_BIN  := $(BINDIR)/server_exec

# ---------------------------------------------------------------------------
#  Phony targets
# ---------------------------------------------------------------------------
.PHONY: all clean clean_all

all: $(CLIENT_BIN) $(SERVER_BIN)

# ---------------------------------------------------------------------------
#  Link executables
# ---------------------------------------------------------------------------
$(CLIENT_BIN): $(CLIENT_OBJS) $(COMMON_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(SERVER_BIN): $(SERVER_OBJS) $(COMMON_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# ---------------------------------------------------------------------------
#  Pattern rule: any *.cpp → $(OBJDIR)/same_path.o  (auto‑mkdirs)
# ---------------------------------------------------------------------------
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
#  Utility dirs
# ---------------------------------------------------------------------------
$(BINDIR):
	@mkdir -p $@

# ---------------------------------------------------------------------------
#  House‑keeping
# ---------------------------------------------------------------------------
clean:
	@echo "Cleaning build artefacts and storage dirs..."
	$(RM) -r $(BUILD_DIR) $(BINDIR) $(STORAGEDIRS)
