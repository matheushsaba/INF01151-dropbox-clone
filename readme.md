# 🗂️ Sistema de Sincronização de Arquivos (Servidor + Cliente)

Este projeto implementa, em **C++17**, um sistema de sincronização de arquivos que oferece **upload**, **download**, **listagem** e **remoção**, usando **TCP** entre um cliente multithread e um servidor multithread.

---

## 📁 Estrutura do Repositório

| Caminho                        | Descrição                                                   |
| ------------------------------ | ----------------------------------------------------------- |
| `client/`                      | Código‑fonte do **cliente**                                 |
| `server/`                      | Código‑fonte do **servidor**                                |
| `common/`                      | Módulos compartilhados (ex.: `packet.cpp`, `packet.h`)      |
| `build/obj/**`                 | Objetos gerados (**fora** da árvore de código)              |
| `bin/`                         | Executáveis criados pelo `make` (`myClient`, `server_exec`) |
| `client_storage/`              | Diretório local de sincronização do cliente                 |
| `server_storage/`              | Diretório onde o servidor guarda os arquivos dos usuários   |
| `Makefile`                     | Script de build                                             |
| `INF01151-Trabalho_pt1-v4.pdf` | Descrição formal do trabalho                                |

> ℹ️ Os diretórios `client_storage/` e `server_storage/` são criados/limpos automaticamente pelos alvos `make` e `make clean`.

---

## 📋 Requisitos

* Linux, WSL2 ou macOS com **GCC >= 10** ou Clang compatível com C++17
* `make`

---

## 🛠️ Compilação

```bash
make
```

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

## ▶️ Execução

### 1. Iniciar o servidor

```bash
./bin/server_exec
```

Por padrão o servidor escuta as portas:

| Porta | Propósito                                   |
| ----: | ------------------------------------------- |
|  4000 | Comandos (list, delete, etc.)               |
|  4001 | Watcher (filesystem events – futura)        |
|  4002 | Transferência de arquivos (upload/download) |

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

## 🔧 Detalhes Internos de Portas

| Porta | Função                    |
| ----: | ------------------------- |
|  4000 | Comandos gerais           |
|  4001 | Watcher (futuramente)     |
|  4002 | Transferência de arquivos |
