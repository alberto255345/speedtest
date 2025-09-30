# Monitoramento de Conexão (C++)

Projeto para Raspberry Pi que automatiza testes de conexão, troca de endereço MAC e envio de relatórios por e-mail. A nova versão foi reescrita em **C++17**, mantendo a arquitetura em camadas (Domain, Application, Infrastructure, Utility).

## Funcionalidades

- Rotação de endereços MAC a partir de `mac.txt`.
- Verificação de conectividade (`ping` em 8.8.8.8).
- Testes de velocidade via **Speedtest CLI** (Ookla) e script Node (`test.js`).
- Envio de e-mail com anexos (`ookla_result.json`, `result.json`, `results.csv`, `connection_log.csv`).
- Acionamento de relé via GPIO (biblioteca wiringPi) para reset do modem.

## Estrutura do projeto

```
.
├── CMakeLists.txt
├── Makefile
├── README.md
├── mac.txt
├── package.json / package-lock.json / node_modules (para o script JS)
├── src/
│   ├── app/
│   │   └── MonitorService.{hpp,cpp}
│   ├── domain/
│   │   ├── EmailSender.hpp
│   │   ├── Logger.hpp
│   │   ├── MacProvider.hpp
│   │   ├── NetworkAdapter.hpp
│   │   ├── Relay.hpp
│   │   ├── Report.hpp
│   │   └── SpeedTester.hpp
│   ├── infra/
│   │   ├── CurlEmailSender.{hpp,cpp}
│   │   ├── FileCsvLogger.{hpp,cpp}
│   │   ├── JsSpeedTester.hpp
│   │   ├── LinuxNetworkAdapter.{hpp,cpp}
│   │   ├── MacListProvider.{hpp,cpp}
│   │   ├── OoklaSpeedTester.{hpp,cpp}
│   │   └── WiringPiRelay.hpp
│   ├── util/
│   │   ├── Env.{hpp,cpp}
│   │   ├── Process.{hpp,cpp}
│   │   └── Time.{hpp,cpp}
│   └── main.cpp
├── test.js
└── .env.example
```

## Dependências

- g++ com suporte a C++17
- CMake (opcional, mas recomendado)
- `libcurl` e `libcurl` headers
- `nlohmann-json3-dev` (ou equivalente via pkg-config `nlohmann_json`)
- `wiringpi`
- Node.js (para executar `test.js`)
- Speedtest CLI (Ookla)

No Raspberry Pi OS:

```bash
sudo apt update
sudo apt install -y g++ cmake pkg-config libcurl4-openssl-dev nlohmann-json3-dev wiringpi \
    nodejs npm
curl -s https://packagecloud.io/install/repositories/ookla/speedtest-cli/script.deb.sh | sudo bash
sudo apt install -y speedtest
```

Instale também as dependências do script JavaScript:

```bash
npm install
```

## Configuração

1. Copie `.env.example` para `.env` e ajuste variáveis como `NET_IFACE`, `EMAIL_USER`, `EMAIL_PASS`, `EMAIL_TO`, etc.
2. Edite `mac.txt` com a lista de endereços MAC a serem rotacionados (um por linha).

## Compilação

### Usando CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

O executável `monitor` ficará em `build/`.

### Usando Makefile

```bash
make
```

O executável `monitor` será gerado na raiz do projeto.

## Uso

Execute o binário com privilégios de root (para trocar MAC e acessar GPIO):

```bash
sudo ./monitor --once
```

Opções disponíveis:

- `--once`: executa um único ciclo.
- `--no-relay`: desativa o acionamento do relé.
- `--relay-pin <BCM>`: define o pino BCM utilizado (padrão: 17).
- `--relay-delay-seconds <segundos>`: tempo em segundos com o relé ativo.
- `--cooldown-seconds <segundos>`: intervalo entre ciclos no modo contínuo (padrão: 3 horas).
- `--js <caminho>` e `--json <caminho>`: caminhos para o script JS e arquivo de saída.

O programa registra logs em `connection_log.csv`, atualiza `mac_index.txt` com o próximo MAC e envia um resumo por e-mail com os anexos disponíveis.
