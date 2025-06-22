## Abrir 4 terminais 
### - terminal 1 Front-end:

``` ./bin/frontend_exec 127.0.0.4 127.0.0.1 4000 ```
##### O Front-End começará a escutar por clientes na porta 8080 e por notificações de líder na porta 9090.

### - terminal 2 Servidor primario:
```./bin/server_exec -p --ip 127.0.0.1 --frontend-ip 127.0.0.4```

### - terminal 3 Iniciar o Servidor Backup:
```./bin/server_exec -b 127.0.0.1 --ip 127.0.0.2```

### - terminal 4 Iniciar o cliente:
```./bin/myClient usuario_teste 127.0.0.4 8080```

## Testando a Tolerância a Falhas

#### Encontre o PID do processo do servidor primário:

```pgrep -f "./bin/server_exec -p"```

#### Encerre o processo do primário abruptamente:

```kill -9 <PID_DO_PRIMARIO>```

