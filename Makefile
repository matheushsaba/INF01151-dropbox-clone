# ---------------------------------------------------------------------------
#  Toolchain & flags
# ---------------------------------------------------------------------------
CXX      ?= g++
CXXFLAGS ?= -g -std=c++17 -Wall -Wextra -pedantic -pthread

# ---------------------------------------------------------------------------
#  Directories & files
# ---------------------------------------------------------------------------
BUILD_DIR := build
OBJDIR    := $(BUILD_DIR)/obj
BINDIR    := bin

CLIENT_SRCS := $(wildcard client/*.cpp)
SERVER_SRCS := $(wildcard server/*.cpp)
COMMON_SRCS := $(wildcard common/*.cpp)
FRONTEND_SRCS := $(wildcard frontend/*.cpp)

CLIENT_OBJS := $(patsubst client/%.cpp,$(OBJDIR)/client/%.o,$(CLIENT_SRCS))
SERVER_OBJS := $(patsubst server/%.cpp,$(OBJDIR)/server/%.o,$(SERVER_SRCS))
COMMON_OBJS := $(patsubst common/%.cpp,$(OBJDIR)/common/%.o,$(COMMON_SRCS))
FRONTEND_OBJS := $(patsubst frontend/%.cpp,$(OBJDIR)/frontend/%.o,$(FRONTEND_SRCS)) 

CLIENT_BIN  := $(BINDIR)/myClient
SERVER_BIN  := $(BINDIR)/server_exec
FRONTEND_BIN := $(BINDIR)/frontend_exec 

STORAGEDIRS := client_storage server_storage

# ---------------------------------------------------------------------------
#  Ports to free before running the server
# ---------------------------------------------------------------------------
# Adicionei as portas do frontend aqui para conveniência
PORTS      := 4000 5001 5002 8080 9090 
KILL_CMD   := sudo fuser -k $(addsuffix /tcp,$(PORTS))

# ---------------------------------------------------------------------------
#  Phony targets
# ---------------------------------------------------------------------------
.PHONY: all clean clean_all killports run_server run_frontend

# ---------------------------------------------------------------------------
#  Build rules
# ---------------------------------------------------------------------------
all: $(CLIENT_BIN) $(SERVER_BIN) $(FRONTEND_BIN) 

$(CLIENT_BIN): $(CLIENT_OBJS) $(COMMON_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(SERVER_BIN): $(SERVER_OBJS) $(COMMON_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@


$(FRONTEND_BIN): $(FRONTEND_OBJS) $(COMMON_OBJS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Any *.cpp  →  obj/…/*.o  (auto-creates subdirs)
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Utility dir
$(BINDIR):
	@mkdir -p $@


# ---------------------------------------------------------------------------
#  Convenience targets
# ---------------------------------------------------------------------------
## Kill anything already listening on the chosen ports
killports:
	@echo "🔪  Killing processes on ports $(PORTS)…"
	-@$(KILL_CMD) 2>/dev/null || true        # ignore 'no process found' errors

## Build, free the ports, then start the server binary
run_server: $(SERVER_BIN) killports
	@echo "🚀  Starting $(SERVER_BIN)…"
	@./$(SERVER_BIN)

## Build, free the ports, then start the frontend binary
run_frontend: $(FRONTEND_BIN) killports
	@echo "🚀  Starting $(FRONTEND_BIN)…"
	@./$(FRONTEND_BIN) 127.0.0.1 4000 # Exemplo de execução com primário inicial

# ---------------------------------------------------------------------------
#  House-keeping
# ---------------------------------------------------------------------------
clean:
	@echo "🧹  Cleaning build artefacts and storage files…"
	$(RM) -r $(BUILD_DIR) $(BINDIR) $(STORAGEDIRS)_*