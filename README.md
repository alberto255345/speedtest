# Monitoramento de Conexão + Speedtest + Rotação de MAC

Este projeto roda em **Raspberry Pi** e executa um ciclo automático de testes de rede:

1. Troca o **MAC address** da interface (`eth0` ou `wlan0`).
2. Testa conectividade (ping `8.8.8.8`).
3. Executa **Speedtest CLI** (Ookla).
4. Executa o seu script **JS (`test.js`)** que gera `result.json`/`results.csv`.
5. Envia um **relatório por e-mail** com anexos (`ookla_result.json`, `result.json`, `results.csv`).
6. Aciona um **relé** no GPIO (reset modem).
7. Aguarda 3h e repete.

---

## 📂 Estrutura

```
/home/pi/speedtest/
├─ monitor.py # Script principal em Python
├─ .env # Credenciais e configuração
├─ mac.txt # Lista de MACs (10 linhas)
├─ test.js # Script JS que gera result.json
├─ result.json # Saída do test.js
├─ results.csv # Saída do test.js
├─ requirements.txt # Dependências Python
└─ install_speedtest_service.sh # Script para criar e iniciar o serviço systemd
```

---

## ⚙️ Instalação

### 1. Atualizar sistema e instalar dependências

```bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv nodejs npm \
                    curl ca-certificates git \
                    python3-rpi.gpio
```

### 2. Instalar Speedtest CLI (Ookla oficial)

```
curl -s https://packagecloud.io/install/repositories/ookla/speedtest-cli/script.deb.sh | sudo bash
sudo apt install -y speedtest
# Teste:
speedtest --accept-license --accept-gdpr -f json | jq .
```

### 3. Clonar/copiar projeto

```
mkdir -p ~/speedtest && cd ~/speedtest
# copie monitor.py, test.js, mac.txt, requirements.txt, package.json, package-lock.json e install_speedtest_service.sh para o Raspberry Pi (ou diretório de destino)
npm install
```

> 💡 O `npm install` garante a instalação do `node-fetch` (via `package.json`), necessário para o `test.js`.

### 4. Configurar ambiente Python

```
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

### 5. Configurar .env

```
# Rede
NET_IFACE=wlan0   # ou eth0

# E-mail
EMAIL_USER=seuemail@gmail.com
EMAIL_PASS=senha_ou_app_password
EMAIL_TO=destinatario@exemplo.com
SMTP_SERVER=smtp.gmail.com
SMTP_PORT=587
# EMAIL_USE_SSL=1   # se usar porta 465

# GPIO
RELAY_PIN=17
```

### 6. Lista de MACs (mac.txt)

```
02:AB:CD:EF:12:01
02:AB:CD:EF:12:02
02:AB:CD:EF:12:03
02:AB:CD:EF:12:04
02:AB:CD:EF:12:05
02:AB:CD:EF:12:06
02:AB:CD:EF:12:07
02:AB:CD:EF:12:08
02:AB:CD:EF:12:09
02:AB:CD:EF:12:10
```
---

## ▶️ Uso
### Testar manualmente

#### 1. Rodar apenas **1 ciclo** e sair

```bash
cd /home/pi/speedtest
source .venv/bin/activate
python3 monitor.py --once
```

* Troca o MAC → testa ping → roda Speedtest (Ookla + JS) → envia e-mail → reseta modem (se relé ativo)
* **Sai** após o ciclo. Útil para testes.
* Antes de rodar este ciclo (ou executar o `test.js` manualmente), garanta que já foi executado `npm install` para instalar as dependências Node.

---

#### 2. Rodar em **loop infinito** (default: 3h entre ciclos)

```bash
python3 monitor.py
```

* Faz o mesmo ciclo, mas **aguarda 3 horas (10800 segundos)** e repete indefinidamente.
* Ideal para produção, quando rodando como serviço.

---

#### 3. Rodar 1 ciclo **sem usar o relé**

```bash
python3 monitor.py --no-relay --once
```

* Faz todos os testes (MAC, ping, speedtests, e-mail).
* **Não aciona GPIO** para reset do modem.
* Sai no final (como `--once`).

---

#### 4. Alterar o intervalo entre ciclos

```bash
python3 monitor.py --cooldown-seconds 600
```

* Faz o loop infinito, mas espera **600 segundos (10 minutos)** em vez de 3 horas.
* Bom para debugging/testes mais rápidos.

---

⚠️ Observações importantes:

* Sempre ative o ambiente virtual antes (`source .venv/bin/activate`), a não ser que esteja rodando via **systemd**, que já chama o Python dentro da venv.
* Se você rodar manualmente em SSH e trocar o **MAC da interface Wi-Fi (`wlan0`)**, pode perder a conexão — por isso recomendo testar primeiro com `--once` local, no HDMI/teclado.

---

## 🛠️ Rodar como serviço (systemd)

### 1. Instalar automaticamente o serviço
O script ```install_speedtest_service.sh``` cria o arquivo de unit, ativa e inicia:

```
chmod +x install_speedtest_service.sh
./install_speedtest_service.sh
```

### 2. Comandos úteis

Ver status:
```
systemctl status speedtest-monitor
```

Logs em tempo real:
```
journalctl -u speedtest-monitor -f -n 50
```

Parar / iniciar:
```
sudo systemctl stop speedtest-monitor
sudo systemctl start speedtest-monitor
```
## ⚡ Notas Importantes

O serviço roda como root para poder trocar o MAC (CAP_NET_ADMIN).

Trocar MAC derruba a rede por alguns segundos; o script espera até 90s o ping voltar.

Se usar Wi-Fi (wlan0), certifique-se que o NetworkManager não sobrescreve o MAC.

Se usar Ethernet (eth0), pode ser necessário renovar DHCP (o script já tenta esperar a rede voltar).

Teste primeiro com --once localmente (HDMI/teclado) antes de depender só do SSH, pois você pode perder a sessão durante a troca de MAC.

## ✅ Checklist rápido

Configurar .env (interface, e-mail, GPIO).

Popular mac.txt com 10 MACs.

Testar: python3 monitor.py --once.

Instalar serviço: ./install_speedtest_service.sh.

Verificar logs: journalctl -u speedtest-monitor -f -n 50.


---

👉 Quer que eu já inclua no `install_speedtest_service.sh` a opção de receber **argumentos extras** (por ex. `--cooldown-seconds 600 --relay-pin 23`) e gravar direto no `ExecStart` do unit file?
