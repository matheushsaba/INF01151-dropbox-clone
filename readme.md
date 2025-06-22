# 🗂️ Sistema de Sincronização de Arquivos  

## 📁 Estrutura de Diretórios

| Caminho                               | Descrição                                                   |
| ------------------------------------- | ----------------------------------------------------------- |
| `client/`                             | Código-fonte do **cliente multithread**                     |
| `server/`                             | Partes do **servidor**                                      |
| &emsp;`server/server_tcp.cpp`         | Front-end (aceita clientes/heartbeats)                      |
| &emsp;`server/heartbeat.*`            | Envio/escuta de heartbeats (TCP :5001)                      |
| &emsp;`server/election_bully.*`       | Implementação do Bully via UDP :5002                        |
| &emsp;`server/session_manager.*`      | Controle de sessões simultâneas                             |
| `common/`                             | Códigos compartilhados (`packet.*`, helpers, structs)       |
| `bin/`                                | Executáveis (`server_exec`, `myClient`)                     |
| `build/obj/`                          | Objetos gerados pelo `make`                                 |
| `server_storage/`                     | Árvore de arquivos dos usuários                             |
| `client_storage/`                     | Pasta-espelho do cliente                                    |
| `Makefile`                            | Build simples via `g++`                                     |
| `INF01151-Trabalho_pt1-v4.pdf`        | Enunciado original da disciplina                            |

> Os diretórios de _storage_ são (re)criáveis; `make clean` os limpa.

---

## 🛠️ Compilação

```bash
make          # gera bin/server_exec e bin/myClient
```

> ℹ️ Os diretórios `client_storage/` e `server_storage/` são criados/limpos automaticamente pelos alvos `make` e `make clean`.

---

## 📋 Requisitos

* Linux, WSL2 ou macOS com **GCC >= 10** ou Clang compatível com C++17
* `make`

---


O alvo padrão:

* Cria `bin/`, `build/obj/`, `client_storage/`, `server_storage/` (caso não existam);
* Compila todos os `.cpp` de `client/`, `server/` e `common/` para `build/obj/…`;
* Gera:

  * `bin/server_exec` – servidor
  * `bin/myClient`   – cliente

### Limpeza

| Comando          | Ação                                                                                           |
| ---------------- | ---------------------------------------------------------------------------------------------- |
| `make clean`     | Remove **executáveis**, **objetos** e todo o conteúdo de `client_storage/` e `server_storage/` |

---

# Parte 1

## ▶️ Execução

### 1. Iniciar o servidor primário

```bash
./bin/server_exec  -p  --ip <meu_ip>
# Exemplo local:
./bin/server_exec -p --ip 127.0.0.1
```

🔧 Por padrão, o servidor escuta as portas:

| Porta | Função                                      |
| ----: | ------------------------------------------- |
|  4000 | Comandos do cliente (list, delete, etc.)    |
|  4001 | Watcher (filesystem events)                 |
|  4002 | Transferência de arquivos (upload/download) |

### 2. Iniciar o mesmo cliente em dispositivos diferentes 
Simulação local deve ser adaptada para rodar em máquinas diferentes e mesma rede local

Em um terminal:

```bash
./bin/myClient <usuario> <ip_servidor> <porta> 
# Porta padrão: 4000
# **Exemplo local**: abrir novo terminal e rodar:
./bin/myClient testuser 127.0.0.1 4000
```

Em outro terminal, iniciar o mesmo cliente (ex: testuser):

```bash
./bin/myClient <usuario> <ip_servidor> <porta> 
# Porta padrão: 4000
# **Exemplo local**: abrir novo terminal e rodar:
./bin/myClient testuser 127.0.0.1 4000
```
Após conectar, o cliente exibe um menu interativo.

---

## 💬 Comandos Disponíveis no Cliente

| Comando                           | Descrição                             |
| --------------------------------- | ------------------------------------- |
| `upload <caminho_arquivo>`        | Envia arquivo ao servidor             |
| `download <nome_arquivo>`         | Baixa arquivo do servidor             |
| `list_server`                     | Lista arquivos do usuário no servidor |
| `delete <nome_arquivo>`           | Remove arquivo do servidor            |
| `exit`                            | Encerra a conexão                     |

---

## 🚀 Testando um Upload Rápido

```bash
# 1. Crie um arquivo de teste
 echo "Exemplo de conteúdo" > exemplo.txt

# 2. No cliente, execute
 upload exemplo.txt
```

Você deverá ver logs no servidor semelhantes a:

```
Recebendo arquivo: exemplo.txt
Recebido pacote: seqn 1 com X bytes.
Upload concluído.
```

O arquivo será salvo em `server_storage/exemplo.txt`.

---

# Parte 2
> **Servidor primário + réplicas em “bully” + cliente** – C++17

O projeto agora suporta **replicação passiva**: um servidor **primário** envia
batimentos (*heartbeat*) para **backups**; caso o primário caia, os backups
executam o **algoritmo Bully** para eleger um novo líder.

---



## ▶️ Execução

### 1. Iniciar um servidor primário

```bash
./bin/server_exec  -p  --ip <meu_ip>
# Exemplo local:
./bin/server_exec -p --ip 127.0.0.1
```

### 1. Iniciar um servidor backup

```bash
./bin/server_exec  -b <ip_do_primario>  --ip <meu_ip>
# Exemplo local:
./bin/server_exec -b 127.0.0.1 --ip 127.0.0.2
```

### 2. Rodar o cliente

Em outro terminal/Wsl tab:

```bash
./bin/myClient <usuario> <ip_servidor> 4000
# Exemplo local:
./bin/myClient testuser 127.0.0.1 4000
```

Após conectar, o cliente exibe um menu interativo.

---

## 💬 Comandos Disponíveis no Cliente

| Comando                           | Descrição                             |
| --------------------------------- | ------------------------------------- |
| `upload <caminho_arquivo>`        | Envia arquivo ao servidor             |
| `download <nome_arquivo>`         | Baixa arquivo do servidor             |
| `list_server`                     | Lista arquivos do usuário no servidor |
| `delete <nome_arquivo>`           | Remove arquivo do servidor            |
| `exit`                            | Encerra a conexão                     |

---

