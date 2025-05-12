### Como testar
Crie esse diretório com arquivos de teste antes de rodar o servidor:
```
mkdir -p server_storage/alice
echo "teste 1" > server_storage/alice/teste1.txt
echo "teste 2" > server_storage/alice/teste2.txt
```
Substitua _alice_ pelo username que você está usando.

Compile tudo corretamente:
````
g++ -std=c++17 -pthread -o server server/server_tcp.cpp common/packet.cpp
g++ -std=c++17 -pthread -o myClient client/command_interface.cpp common/packet.cpp
````
Depois execute:
```
./server
```

E, em outro terminal:
````
./myClient alice 127.0.0.1 4000
> list_server
````

#### O que se espera na saida:
No cliente:
````
[Arquivos no servidor]
test1.txt
test2.doc
`````
No servidor:
````
Command received: list_server|alice
````

Se tudo isso funcionar, você está com a comunicação completa via Packet. O próximo passo será o upload.
