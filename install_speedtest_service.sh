#!/bin/bash
set -e

SERVICE_NAME="speedtest-monitor"

# Descobre diretório onde o script está sendo rodado
WORKDIR="$(pwd)"
PYTHON_BIN="$WORKDIR/.venv/bin/python"
SCRIPT="$WORKDIR/monitor.py"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

echo "⚙️ Criando service $SERVICE_NAME em $SERVICE_FILE ..."
echo "   -> WorkingDirectory = $WORKDIR"

sudo tee $SERVICE_FILE > /dev/null <<EOF
[Unit]
Description=Monitoramento Speedtest + Reset + MAC rotate
After=network-online.target NetworkManager.service
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=$WORKDIR
Environment=PYTHONUNBUFFERED=1
ExecStart=$PYTHON_BIN $SCRIPT
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

echo "🔄 Recarregando systemd..."
sudo systemctl daemon-reload

echo "✅ Habilitando serviço..."
sudo systemctl enable $SERVICE_NAME

echo "🚀 Iniciando serviço..."
sudo systemctl restart $SERVICE_NAME

echo "📋 Status do serviço:"
sudo systemctl --no-pager --full status $SERVICE_NAME
