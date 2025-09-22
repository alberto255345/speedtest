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
from pathlib import Path
from email.mime.text import MIMEText
from email.mime.base import MIMEBase
from email.mime.multipart import MIMEMultipart
from email import encoders
from dotenv import load_dotenv

# ================== CONFIG & GPIO ==================
HERE = Path(__file__).resolve().parent
MAC_FILE = HERE / "mac.txt"
MAC_INDEX_FILE = HERE / "mac_index.txt"
OOKLA_JSON = HERE / "ookla_result.json"

# Carrega .env logo no início
load_dotenv()
NET_IFACE = os.getenv("NET_IFACE", "eth0")  # defina eth0 ou wlan0 no .env
RELAY_PIN_DEFAULT = int(os.getenv("RELAY_PIN", "17"))

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
def get_next_mac():
    """Lê próximo MAC de mac.txt em sequência circular."""
    if not MAC_FILE.exists():
        return None
    macs = [l.strip() for l in MAC_FILE.read_text().splitlines() if l.strip()]
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
        return
    level_on  = GPIO.HIGH if active_high else GPIO.LOW
    level_off = GPIO.LOW if active_high else GPIO.HIGH
    GPIO.output(relay_pin, level_on)
    time.sleep(pulse_seconds)
    GPIO.output(relay_pin, level_off)

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

# ================== MAIN ==================
def main():
    ap = argparse.ArgumentParser(description="Monitor de rede + rotação de MAC + ciclo 3h")
    ap.add_argument("--once", action="store_true", help="Executa 1 ciclo e sai")
    ap.add_argument("--no-relay", dest="no_relay", action="store_true", help="Não aciona o relé")
    ap.add_argument("--relay-pin", type=int, default=RELAY_PIN_DEFAULT, help="GPIO BCM do relé (padrão: 17)")
    ap.add_argument("--cooldown-seconds", type=int, default=3*60*60, help="Intervalo entre ciclos (padrão: 10800s)")
    ap.add_argument("--js", default=str(HERE / "test.js"), help="Caminho do script JS")
    ap.add_argument("--json", default=str(HERE / "result.json"), help="Caminho do result.json do JS")
    args = ap.parse_args()

    gpio_ready = False
    if not args.no_relay:
        gpio_ready = maybe_setup_gpio(args.relay_pin)

    while True:
        # 1) Rotação de MAC
        mac = get_next_mac()
        if mac:
            ok = apply_mac_rotation(NET_IFACE, mac)
            print(f"📡 MAC aplicado ({NET_IFACE}): {mac}" if ok else f"⚠️  Falha ao aplicar MAC {mac}")
        else:
            print("⚠️  mac.txt ausente ou vazio — sem rotação de MAC.")

        # 2) Testes (só seguem se ping OK)
        print("🔍 Ping 8.8.8.8 ...")
        if ping_ok():
            print("✅ Ping OK")
            print("🚀 Ookla speedtest (CLI) ...")
            ookla = run_ookla_speedtest()

            print("📊 Script JS de velocidade ...")
            js_data = run_js_speedtest(Path(args.js), Path(args.json))

            body = []
            body.append(f"📡 Interface: {NET_IFACE} | MAC: {mac or 'N/D'}")
            body.append("✅ Ping: OK")
            body.append("\n⚡ Ookla CLI:\n" + summarize_ookla(ookla))
            body.append("\n📈 Speedtest JS (resumo bruto JSON):")
            body.append(json.dumps(js_data, indent=2, ensure_ascii=False)[:6000])

            attachments = []
            if OOKLA_JSON.exists(): attachments.append(OOKLA_JSON)
            rjson = Path(args.json)
            if rjson.exists(): attachments.append(rjson)
            rcsv = HERE / "results.csv"
            if rcsv.exists(): attachments.append(rcsv)

            try:
                send_email("Relatório de Teste de Conexão (Raspberry)", "\n".join(body), attachments)
                print("✉️  Email enviado.")
            except Exception as e:
                print(f"❌ Falha enviando email: {e}")
        else:
            print("❌ Sem conectividade (ping falhou).")

        # 3) Reset modem
        if gpio_ready and not args.no_relay:
            print("🔄 Resetando modem via relé ...")
            try:
                reset_modem(args.relay_pin, pulse_seconds=2.0, active_high=True)
                print("✅ Reset efetuado.")
            except Exception as e:
                print(f"⚠️  Falha no reset via GPIO: {e}")

        # 4) Intervalo
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
