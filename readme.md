

---

# 🗂️ Sistema de Sincronização de Arquivos (Servidor e Cliente)

Este projeto implementa um sistema de sincronização de arquivos com suporte a **upload**, **download**, **listagem** e **remoção** de arquivos, via **comunicação TCP** entre um cliente e um servidor multithread.

---

## 📁 Estrutura do Projeto

* `server_dir/`: Código-fonte do **servidor**.
* `client/`: Código-fonte do **cliente**.
* `common/`: Arquivos compartilhados (ex: `packet.cpp`, `packet.h`).
* `bin/`: Binários gerados pelo `make`.
* `server_storage/`: Diretório onde o servidor armazena os arquivos dos usuários.
* `client_sync/`: Diretório de sincronização do lado do cliente.
* `Makefile`: Script de build e configuração.

---

## 📋 Requisitos

* Sistema Linux ou compatível
* `g++` com suporte a C++17
* `make`

---

## 🛠️ Compilação e Setup

1. **Clone o repositório** (caso ainda não tenha feito):

```bash
git clone <URL_DO_REPO>
cd <nome_da_pasta>
```

2. **Compile e configure os diretórios:**

```bash
make
```

Esse comando:

* Cria diretórios como `bin/`, `server_storage/`, `client_sync/`
* Cria subpastas para usuários como `testuser`
* Compila os binários:

  * `bin/server_exec` — executável do servidor
  * `bin/myClient` — executável do cliente

---

## 🧹 Limpeza

* Remover apenas os binários:

```bash
make clean
```

* Remover **tudo** (binários + diretórios de arquivos):

```bash
make clean_all
```

---

## ▶️ Execução

### 1. Inicie o servidor:

```bash
./bin/server_exec
```

O servidor escutará nas seguintes portas:

* `4000` – comandos (ex: list, delete)
* `4001` – watcher (futuro)
* `4002` – transferência de arquivos (upload/download)

---

### 2. Execute o cliente:

Em outro terminal:

```bash
./bin/myClient testuser 127.0.0.1 4000
```

**Formato:**

```bash
./myClient <username> <server_ip> <porta_comandos>
```

Você verá o menu com os comandos disponíveis.

---

## 💬 Comandos do Cliente

* `upload <caminho_do_arquivo>`: Envia um arquivo ao servidor.
* `download <nome_do_arquivo>`: (WIP) Baixa um arquivo do servidor.
* `list_server`: Lista todos os arquivos do usuário no servidor.
* `delete <nome_do_arquivo>`: (WIP) Remove um arquivo no servidor.
* `exit`: Fecha a conexão com o servidor.

---

## 🧪 Testando o Upload

1. Crie um arquivo de teste:

```bash
echo "Exemplo de conteúdo" > exemplo.txt
```

2. No cliente, execute:

```bash
upload exemplo.txt
```

3. No terminal do servidor, você verá:

```
Recebendo arquivo: exemplo.txt
Recebido pacote: seqn 1 com X bytes.
Upload concluído.
```

4. O arquivo será salvo em `server_storage/exemplo.txt`.

---

### 📄 Listando os Arquivos

Use:

```bash
list_server
```

Saída esperada:

```
[Arquivos no servidor]
exemplo.txt
```

---

## 🔧 Estrutura Interna das Portas

| Porta | Função                                      |
| ----- | ------------------------------------------- |
| 4000  | Comandos gerais                             |
| 4001  | Watcher (eventual)                          |
| 4002  | Transferência de arquivos (upload/download) |

---

