#!/usr/bin/env python3
import os
import sys
import time
import json
import atexit
import smtplib
import argparse
import shutil
import subprocess
import RPi.GPIO as GPIO  # Importacao da biblioteca na "head" do código
import ipaddress
from pathlib import Path
from email.mime.text import MIMEText
from email.mime.base import MIMEBase
from email.mime.multipart import MIMEMultipart
from email import encoders
from dotenv import load_dotenv
from urllib.request import urlopen

# ================== CONFIG & GPIO ==================
HERE = Path(__file__).resolve().parent
MAC_FILE = HERE / "mac.txt"
MAC_INDEX_FILE = HERE / "mac_index.txt"
OOKLA_JSON = HERE / "ookla_result.json"
LOG_FILE = HERE / "connection_log.csv"
PID_FILE = HERE / "program.pid"

# Carrega .env logo no início
load_dotenv()
NET_IFACE = os.getenv("NET_IFACE", "eth0")  # defina eth0 ou wlan0 no .env
RELAY_PIN_DEFAULT = int(os.getenv("RELAY_PIN", "17"))

# ADDED: seleção de UF e prefixo base (4 octetos)
UF_ENV = (os.getenv("UF") or "").strip().upper()
MAC_BASE_PREFIX = os.getenv("MAC_BASE_PREFIX", "02:AB:CD:EF").strip().upper()

# ADDED: mapeamento único (penúltimo byte) para as 27 UFs
UF_HEX_MAP = {
    "AC": 0x01, "AL": 0x02, "AP": 0x03, "AM": 0x04, "BA": 0x05, "CE": 0x06, "DF": 0x07,
    "ES": 0x08, "GO": 0x09, "MA": 0x0A, "MT": 0x0B, "MS": 0x0C, "MG": 0x0D, "PA": 0x0E,
    "PB": 0x0F, "PR": 0x10, "PE": 0x11, "PI": 0x12, "RJ": 0x13, "RN": 0x14, "RS": 0x15,
    "RO": 0x16, "RR": 0x17, "SC": 0x18, "SP": 0x19, "SE": 0x1A, "TO": 0x1B,
}

GPIO = None
def _gpio_cleanup():
    try:
        if GPIO:
            GPIO.cleanup()
    except Exception:
        pass

def maybe_setup_gpio(relay_pin: int):
    """Inicializa GPIO se possível."""
    global GPIO
    try:
        import RPi.GPIO as _GPIO
        GPIO = _GPIO
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(relay_pin, GPIO.OUT, initial=GPIO.LOW)
        atexit.register(_gpio_cleanup)
        return True
    except Exception as e:
        print(f"⚠️  GPIO indisponível ({e}). Continuando sem relé.")
        return False

# ================== SHELL HELPERS ==================
def sh(cmd, **kw):
    """Executa comando e retorna (rc, stdout, stderr)."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)
    out, err = p.communicate()
    return p.returncode, out, err

def has_cmd(name: str) -> bool:
    return shutil.which(name) is not None

# ================== REDE & MAC ==================
def ping_ok():
    rc, _, _ = sh(["ping", "-c", "3", "-W", "2", "8.8.8.8"])
    return rc == 0

def get_ip_address(iface: str):
    rc, out, _ = sh(["ip", "-4", "addr", "show", "dev", iface])
    if rc != 0:
        return None
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("inet "):
            return line.split()[1].split("/")[0]
    return None

# ADDED: obter IP público
def get_public_ip(timeout=4):
    """Descobre o IP público consultando endpoints externos."""
    # 1) Tenta via curl (se existir)
    for url in ["https://api.ipify.org", "https://ifconfig.me", "https://icanhazip.com"]:
        if has_cmd("curl"):
            rc, out, _ = sh(["curl", "-fsS", "--max-time", str(timeout), url])
            if rc == 0:
                ip = out.strip()
                try:
                    ipaddress.ip_address(ip)
                    return ip
                except Exception:
                    pass
    # 2) Fallback: urllib (sem depender de curl)
    for url in ["https://api.ipify.org", "https://ifconfig.me", "https://icanhazip.com"]:
        try:
            with urlopen(url, timeout=timeout) as r:
                ip = r.read().decode().strip()
                ipaddress.ip_address(ip)
                return ip
        except Exception:
            continue
    return None

def wait_connectivity(timeout_sec=90) -> bool:
    """Espera a internet voltar (ping OK) até timeout."""
    t0 = time.time()
    while time.time() - t0 < timeout_sec:
        if ping_ok():
            return True
        time.sleep(3)
    return False

def nmcli_set_cloned_mac(iface: str, mac: str) -> bool:
    """Aplica MAC via NetworkManager (se presente)."""
    if not has_cmd("nmcli"):
        return False
    # achar conexão ativa da interface
    rc, out, _ = sh(["nmcli", "-t", "-f", "NAME,DEVICE", "connection", "show", "--active"])
    if rc != 0:
        return False
    conn_name = None
    for line in out.splitlines():
        parts = line.strip().split(":")
        if len(parts) == 2 and parts[1] == iface:
            conn_name = parts[0]
            break
    if not conn_name:
        return False

    # tipo do device (wifi/ethernet)
    rc, devinfo, _ = sh(["nmcli", "-t", "-f", "GENERAL.TYPE", "device", "show", iface])
    prop = "802-ethernet.cloned-mac-address"
    if rc == 0 and "wifi" in (devinfo or ""):
        prop = "802-11-wireless.cloned-mac-address"

    # aplica e recarrega conexão
    sh(["nmcli", "connection", "modify", conn_name, prop, mac])
    sh(["nmcli", "connection", "down", conn_name])
    time.sleep(2)
    sh(["nmcli", "connection", "up", conn_name])
    return True

def iplink_set_mac(iface: str, mac: str) -> bool:
    """Fallback: aplica MAC com ip link (derruba/sobe)."""
    sh(["ip", "link", "set", iface, "down"])
    rc, _, err = sh(["ip", "link", "set", "dev", iface, "address", mac])
    sh(["ip", "link", "set", iface, "up"])
    if rc != 0:
        print(f"⚠️  ip link set dev {iface} address {mac} falhou: {err.strip()}")
    return rc == 0

def apply_mac_rotation(iface: str, mac: str) -> bool:
    """Tenta nmcli; se não, usa ip link. Depois espera conectividade."""
    print(f"🔁 Trocando MAC em {iface} para {mac} ...")
    ok = nmcli_set_cloned_mac(iface, mac)
    if not ok:
        ok = iplink_set_mac(iface, mac)
    if not ok:
        print("❌ Falha ao aplicar MAC.")
        return False
    # aguarda rede voltar
    if wait_connectivity(90):
        print("✅ Rede voltou após troca de MAC.")
        return True
    print("⚠️  Rede não voltou dentro do timeout após troca de MAC.")
    return False

# ================== MAC LISTA ==================
# ADDED: gera lista de 10 MACs para a UF (penúltimo byte fixo por UF, último 01..10)
def gen_mac_list_for_uf(uf: str):
    uf = (uf or "").upper()
    if uf not in UF_HEX_MAP:
        return None
    try:
        # valida prefixo "AA:BB:CC:DD"
        parts = MAC_BASE_PREFIX.split(":")
        if len(parts) != 4 or not all(len(p) == 2 for p in parts):
            print(f"⚠️ Prefixo MAC_BASE_PREFIX inválido: {MAC_BASE_PREFIX}. Usando padrão 02:AB:CD:EF.")
            base = "02:AB:CD:EF"
        else:
            base = MAC_BASE_PREFIX
    except Exception:
        base = "02:AB:CD:EF"

    xx = UF_HEX_MAP[uf]
    macs = [f"{base}:{xx:02X}:{i:02X}" for i in range(1, 11)]  # :01 .. :10
    return macs

# CHANGED: agora suporta UF; se UF definida, usa lista gerada e índice por UF
def get_next_mac():
    """Retorna o próximo MAC em sequência circular.
       - Se UF definida no .env: usa lista gerada por UF (10 MACs), índice por UF.
       - Caso contrário: lê de mac.txt como antes."""
    # Caminho de índice pode variar por UF
    if UF_ENV in UF_HEX_MAP:
        macs = gen_mac_list_for_uf(UF_ENV)
        if not macs:
            return None
        idx_path = HERE / f"mac_index_{UF_ENV}.txt"  # índice por UF
        idx = 0
        if idx_path.exists():
            try:
                idx = int(idx_path.read_text().strip() or "0")
            except ValueError:
                idx = 0
        mac = macs[idx % len(macs)]
        idx_path.write_text(str((idx + 1) % len(macs)))
        return mac

    # Fallback: comportamento antigo com mac.txt
    if not MAC_FILE.exists():
        return None
    macs = [l.strip().upper() for l in MAC_FILE.read_text().splitlines() if l.strip()]
    if not macs:
        return None
    idx = 0
    if MAC_INDEX_FILE.exists():
        try:
            idx = int(MAC_INDEX_FILE.read_text().strip() or "0")
        except ValueError:
            idx = 0
    mac = macs[idx % len(macs)]
    MAC_INDEX_FILE.write_text(str((idx + 1) % len(macs)))
    return mac

# ================== LOG ==================
def ensure_log_file():
    if not LOG_FILE.exists():
        LOG_FILE.write_text("timestamp;mac;ip;resultado\n", encoding="utf-8")

def append_log_entry(timestamp: str, mac: str, ip: str, result: str):
    ensure_log_file()
    safe_result = result.replace("\n", " ").replace("\r", " ")
    safe_result = safe_result.replace(";", ",")
    line = f"{timestamp};{mac};{ip};{safe_result}\n"
    with LOG_FILE.open("a", encoding="utf-8") as fp:
        fp.write(line)

# ================== TESTES ==================
def run_ookla_speedtest():
    cmd = ["speedtest", "--accept-license", "--accept-gdpr", "-f", "json"]
    rc, out, err = sh(cmd)
    if rc != 0:
        return {"error": {"rc": rc, "stderr": err.strip()}}
    try:
        data = json.loads(out)
        OOKLA_JSON.write_text(json.dumps(data, indent=2, ensure_ascii=False))
        return data
    except Exception as e:
        return {"error": f"parse json: {e}", "raw": out[:4000]}

def run_js_speedtest(js_path: Path, result_json_path: Path):
    rc, _, err = sh(["node", str(js_path)])
    if rc != 0:
        return {"error": {"rc": rc, "stderr": err.strip()}}
    try:
        return json.loads(result_json_path.read_text(encoding="utf-8"))
    except Exception as e:
        return {"error": f"read {result_json_path.name}: {e}"}

def perform_speed_tests(label: str, mac: str, js_path: Path, result_json_path: Path):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    mac_display = mac or "N/D"
    # CHANGED: usar IP público
    public_ip = get_public_ip()
    ip_display = public_ip or "N/D"

    print(f"🔍 [{label}] Ping 8.8.8.8 ...")
    ping_success = ping_ok()
    log_parts = [label, f"Ping {'OK' if ping_success else 'falhou'}"]

    body_lines = [
        f"📋 {label}",
        f"🕒 Momento: {timestamp}",
        f"📡 Interface: {NET_IFACE}",
        f"🆔 MAC: {mac_display}",
        f"🌐 IP público: {ip_display}",
        f"🇧🇷 UF: {UF_ENV or 'N/D'}",  # ADDED: só informativo
    ]

    if ping_success:
        print("✅ Ping OK")
        print(f"🚀 [{label}] Ookla speedtest (CLI) ...")
        ookla = run_ookla_speedtest()
        body_lines.append("")
        body_lines.append("⚡ Ookla CLI:")
        body_lines.append(summarize_ookla(ookla))
        if isinstance(ookla, dict) and "error" not in ookla:
            dl_bw = ookla.get("download", {}).get("bandwidth")
            ul_bw = ookla.get("upload", {}).get("bandwidth")
            dl_mbps = (dl_bw or 0) / 1e6
            ul_mbps = (ul_bw or 0) / 1e6
            log_parts.append(f"Ookla DL {dl_mbps:.2f} UL {ul_mbps:.2f}")
        else:
            log_parts.append("Ookla erro")

        print(f"📊 [{label}] Script JS de velocidade ...")
        js_data = run_js_speedtest(js_path, result_json_path)
        body_lines.append("")
        body_lines.append("📈 Speedtest JS (resumo bruto JSON):")
        body_lines.append(json.dumps(js_data, indent=2, ensure_ascii=False)[:6000])

        if isinstance(js_data, dict) and "error" not in js_data:
            dl_js = js_data.get("download_mbps")
            ul_js = js_data.get("upload_mbps")
            if isinstance(dl_js, (int, float)) and isinstance(ul_js, (int, float)):
                log_parts.append(f"JS DL {dl_js:.2f} UL {ul_js:.2f}")
            else:
                log_parts.append("JS dados incompletos")
        else:
            log_parts.append("JS erro")
    else:
        print("❌ Sem conectividade (ping falhou).")
        body_lines.append("")
        body_lines.append("❌ Sem conectividade (ping falhou).")
        ookla = {}
        js_data = {}

    log_result = " | ".join(log_parts)
    append_log_entry(timestamp, mac_display, ip_display, log_result)

    return {
        "label": label,
        "timestamp": timestamp,
        "mac": mac_display,
        "ip": ip_display,
        "ping_ok": ping_success,
        "ookla": ookla,
        "js_data": js_data,
        "body_text": "\n".join(body_lines),
        "log_result": log_result,
    }

# ================== EMAIL ==================
def send_email(subject, body, attachments=None):
    EMAIL_USER = os.getenv("EMAIL_USER")
    EMAIL_PASS = os.getenv("EMAIL_PASS")
    EMAIL_TO   = os.getenv("EMAIL_TO")
    SMTP_SERVER = os.getenv("SMTP_SERVER", "smtp.gmail.com")
    SMTP_PORT   = int(os.getenv("SMTP_PORT", "587"))
    USE_SSL     = os.getenv("EMAIL_USE_SSL", "0") in ("1", "true", "True")

    if not (EMAIL_USER and EMAIL_PASS and EMAIL_TO):
        raise RuntimeError("EMAIL_USER/EMAIL_PASS/EMAIL_TO ausentes no .env")

    msg = MIMEMultipart()
    msg["From"] = EMAIL_USER
    msg["To"] = EMAIL_TO
    msg["Subject"] = subject
    msg.attach(MIMEText(body, "plain", "utf-8"))

    for fpath in attachments or []:
        try:
            fp = Path(fpath)
            if fp.exists():
                part = MIMEBase("application", "octet-stream")
                part.set_payload(fp.read_bytes())
                encoders.encode_base64(part)
                part.add_header("Content-Disposition", f'attachment; filename="{fp.name}"')
                msg.attach(part)
        except Exception as e:
            print(f"⚠️  Falha anexando {fpath}: {e}")

    if USE_SSL:
        with smtplib.SMTP_SSL(SMTP_SERVER, SMTP_PORT) as s:
            s.login(EMAIL_USER, EMAIL_PASS)
            s.sendmail(EMAIL_USER, [EMAIL_TO], msg.as_string())
    else:
        with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as s:
            s.starttls()
            s.login(EMAIL_USER, EMAIL_PASS)
            s.sendmail(EMAIL_USER, [EMAIL_TO], msg.as_string())

# ================== RELÉ ==================
def reset_modem(relay_pin: int, pulse_seconds: float = 2.0, active_high: bool = True):
    if not GPIO:
        print("⚠️  Ignorando reset_modem: GPIO não carregado.")
        return False
    level_on  = GPIO.HIGH if active_high else GPIO.LOW
    level_off = GPIO.LOW if active_high else GPIO.HIGH
    print(f"🔌 Relé acionado por {pulse_seconds}s ...")
    GPIO.output(relay_pin, level_on)
    time.sleep(pulse_seconds)
    GPIO.output(relay_pin, level_off)
    print("🔌 Relé desacionado.")
    return True

# ================== UTIL ==================
def summarize_ookla(d):
    if not isinstance(d, dict) or "error" in d:
        return f"Erro Ookla: {d.get('error') if isinstance(d, dict) else d}"
    ping = d.get("ping", {}).get("latency")
    dl_bw = d.get("download", {}).get("bandwidth")   # bytes/s (depende versão)
    ul_bw = d.get("upload", {}).get("bandwidth")
    dl_mbps = (dl_bw or 0) / 1e6
    ul_mbps = (ul_bw or 0) / 1e6
    srv = d.get("server", {}).get("host")
    return f"Servidor: {srv}\nPing: {ping} ms\nDownload (bandwidth): {dl_mbps:.2f} Mbps\nUpload (bandwidth): {ul_mbps:.2f} Mbps"

# ADDED: PID helpers
def _remove_pidfile():
    try:
        if PID_FILE.exists():
            PID_FILE.unlink()
    except Exception:
        pass

def _write_pidfile():
    try:
        PID_FILE.write_text(str(os.getpid()), encoding="utf-8")
        atexit.register(_remove_pidfile)
        print(f"🆔 PID salvo em {PID_FILE}")
    except Exception as e:
        print(f"⚠️ Não foi possível gravar PID em {PID_FILE}: {e}")

# ================== MAIN ==================
def main():
    ap = argparse.ArgumentParser(description="Monitor de rede + rotação de MAC + ciclo 3h")
    ap.add_argument("--once", action="store_true", help="Executa 1 ciclo e sai")
    ap.add_argument("--no-relay", dest="no_relay", action="store_true", help="Não aciona o relé")
    ap.add_argument("--relay-pin", type=int, default=RELAY_PIN_DEFAULT, help="GPIO BCM do relé (padrão: 17)")
    ap.add_argument("--relay-delay-seconds", type=int, default=30, help="Tempo (s) com o relé acionado antes de liberar")
    ap.add_argument("--cooldown-seconds", type=int, default=3*60*60, help="Intervalo entre ciclos (padrão: 10800s)")
    ap.add_argument("--js", default=str(HERE / "test.js"), help="Caminho do script JS")
    ap.add_argument("--json", default=str(HERE / "result.json"), help="Caminho do result.json do JS")
    args = ap.parse_args()

    _write_pidfile()
    
    gpio_ready = False
    if not args.no_relay:
        gpio_ready = maybe_setup_gpio(args.relay_pin)
#----------------> @@@Alterado 28/09
#"Reset" GPIO - limpa e monta - Solucao para disparo do rele no "start"
    GPIO.cleanup()
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(17, GPIO.OUT)
#<---------------------------------##
    js_path = Path(args.js)
    result_json_path = Path(args.json)

    while True:
#----------------> @@@Alterado 28/09
#"Limpa" GPIO - Desliga o rele (Reativa ONT)
        GPIO.cleanup()
#Espera xx tempo reinicializacao ONT
        time.sleep(180)
#<---------------------------------##
        # 1) Rotação de MAC
        mac = get_next_mac()
        if mac:
            ok = apply_mac_rotation(NET_IFACE, mac)
            print(f"📡 MAC aplicado ({NET_IFACE}): {mac}" if ok else f"⚠️  Falha ao aplicar MAC {mac}")
        else:
            print("⚠️  Lista de MACs indisponível — verifique UF no .env ou mac.txt.")

        # 2) Testes (APENAS UMA VEZ, após trocar MAC)
        reports = []
        reports.append(perform_speed_tests("Teste após MAC", mac, js_path, result_json_path))

        # 3) Reset modem (mantido)
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(17, GPIO.OUT)
        if gpio_ready and not args.no_relay:
            print("🔄 Resetando modem via relé ...")
            try:
                acionou = reset_modem(args.relay_pin, pulse_seconds=float(max(args.relay_delay_seconds, 0)), active_high=True)
                if acionou:
                    print("✅ Ciclo do relé concluído.")
                else:
                    print("⚠️  Relé não pôde ser acionado.")
            except Exception as e:
                print(f"⚠️  Falha no reset via GPIO: {e}")
            wait_time = max(args.relay_delay_seconds, 90)
            print(f"⏱️  Aguardando retorno da conectividade (timeout {wait_time}s) ...")
            if wait_connectivity(wait_time):
                print("✅ Conectividade restabelecida após reset.")
            else:
                print("⚠️  Conectividade não voltou no tempo esperado após reset.")

            # (sem novo bloco de testes pós-reset)

        # 4) Intervalo
        attachments = []
        if OOKLA_JSON.exists(): attachments.append(OOKLA_JSON)
        if result_json_path.exists(): attachments.append(result_json_path)
        rcsv = HERE / "results.csv"
        if rcsv.exists(): attachments.append(rcsv)
        if LOG_FILE.exists(): attachments.append(LOG_FILE)

        body_sections = []
        for report in reports:
            body_sections.append(report["body_text"])
            body_sections.append("")
        body_sections.append("🗒️ Entradas adicionadas ao arquivo connection_log.csv:")
        for report in reports:
            body_sections.append(f"- {report['timestamp']} | {report['log_result']}")

        try:
            send_email("Relatório de Teste de Conexão (Raspberry)", "\n".join(body_sections).strip(), attachments)
            print("✉️  Email enviado.")
        except Exception as e:
            print(f"❌ Falha enviando email: {e}")

        if args.once:
            break
        secs = args.cooldown_seconds
        print(f"⏳ Aguardando {secs//3600}h para repetir ...")
        time.sleep(secs)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n⏹️ Interrompido pelo usuário.")
