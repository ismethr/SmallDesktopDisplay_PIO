#!/usr/bin/env bash
# MiniDisplay Bridge for Linux: venv setup, foreground run and systemd user service.
#
#   tools/linux_bridge.sh install     create .venv, install deps, enable the user service
#   tools/linux_bridge.sh run [args]  run in the foreground (Ctrl+C to stop)
#   tools/linux_bridge.sh start|stop|restart|status|logs
#   tools/linux_bridge.sh uninstall   disable and remove the user service (keeps .venv/settings)
#   tools/linux_bridge.sh ports       list serial ports
#   tools/linux_bridge.sh plugin      install/update the Omarchy bar widget (minidisplay.bridge)
#   tools/linux_bridge.sh plugin-remove
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRIDGE_DIR="$REPO_ROOT/tools/desktop_display_bridge"
VENV="${MINIDISPLAY_VENV:-$REPO_ROOT/.venv}"
PYTHON="$VENV/bin/python"
SERVICE_NAME="minidisplay-bridge.service"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT_FILE="$UNIT_DIR/$SERVICE_NAME"

PLUGIN_ID="minidisplay.bridge"
PLUGIN_SRC="$REPO_ROOT/omarchy_plugin"
PLUGIN_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/omarchy/plugins/$PLUGIN_ID"

log() { printf '[minidisplay] %s\n' "$*"; }

ensure_venv() {
	if [[ ! -x "$PYTHON" ]]; then
		log "creating virtualenv at $VENV"
		python3 -m venv "$VENV"
	fi
	"$PYTHON" -m pip install --quiet --disable-pip-version-check -r "$BRIDGE_DIR/requirements.txt"
}

check_serial_access() {
	local port found=0
	for port in /dev/ttyUSB* /dev/ttyACM*; do
		[[ -e "$port" ]] || continue
		found=1
		if [[ -r "$port" && -w "$port" ]]; then
			log "serial port $port is accessible"
		else
			local group
			group="$(stat -c %G "$port")"
			log "WARNING: no permission for $port (group '$group')."
			log "  run: sudo usermod -aG $group $USER   then log out and back in"
		fi
	done
	if [[ $found -eq 0 ]]; then
		log "no /dev/ttyUSB* or /dev/ttyACM* found yet; plug in the display (CH340 uses the ch341 driver)"
	fi
}

# systemd user services get a minimal PATH; keep the directories that hold
# codex/claude/nvidia-smi so Codex and Claude quotas and NVIDIA temps still work.
service_path() {
	local dirs=() tool dir
	for tool in codex claude nvidia-smi; do
		if dir="$(command -v "$tool" 2>/dev/null)"; then
			dirs+=("$(dirname "$dir")")
		fi
	done
	dirs+=("$HOME/.local/bin" /usr/local/bin /usr/bin /bin)
	printf '%s\n' "${dirs[@]}" | awk '!seen[$0]++' | paste -sd:
}

write_unit() {
	mkdir -p "$UNIT_DIR"
	cat >"$UNIT_FILE" <<EOF
[Unit]
Description=MiniDisplay Bridge (USB system status display)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=$REPO_ROOT
Environment=PATH=$(service_path)
Environment=PYTHONUNBUFFERED=1
ExecStart=$PYTHON $BRIDGE_DIR/desktop_display_bridge.py --listen-host 127.0.0.1
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF
	log "wrote $UNIT_FILE"
}

install_plugin() {
	if ! command -v omarchy >/dev/null 2>&1; then
		log "omarchy not found; skipping the bar widget"
		return 0
	fi
	local fresh=0
	[[ -d "$PLUGIN_DIR" ]] || fresh=1
	mkdir -p "$PLUGIN_DIR"
	cp -r "$PLUGIN_SRC/." "$PLUGIN_DIR/"
	chmod +x "$PLUGIN_DIR/scripts/minidisplayctl"
	omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
	local _
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		omarchy-shell shell listPlugins 2>/dev/null | grep -q "\"$PLUGIN_ID\"" && break
		sleep 0.5
	done
	if [[ $fresh -eq 1 ]]; then
		omarchy plugin enable "$PLUGIN_ID" >/dev/null && log "bar widget enabled ($PLUGIN_ID)"
	else
		log "bar widget updated ($PLUGIN_ID)"
	fi
}

cmd="${1:-help}"
shift || true
case "$cmd" in
install)
	ensure_venv
	check_serial_access
	write_unit
	systemctl --user daemon-reload
	systemctl --user enable --now "$SERVICE_NAME"
	install_plugin
	log "service running; status page: http://127.0.0.1:8766/"
	log "logs: journalctl --user -u $SERVICE_NAME -f"
	;;
run)
	ensure_venv
	check_serial_access
	exec "$PYTHON" "$BRIDGE_DIR/desktop_display_bridge.py" --listen-host 127.0.0.1 "$@"
	;;
start | stop | restart | status)
	systemctl --user "$cmd" "$SERVICE_NAME"
	;;
logs)
	exec journalctl --user -u "$SERVICE_NAME" -f "$@"
	;;
uninstall)
	systemctl --user disable --now "$SERVICE_NAME" 2>/dev/null || true
	rm -f "$UNIT_FILE"
	systemctl --user daemon-reload
	log "service removed"
	;;
plugin)
	install_plugin
	;;
plugin-remove)
	omarchy plugin disable "$PLUGIN_ID" >/dev/null 2>&1 || true
	rm -rf "$PLUGIN_DIR"
	omarchy-shell shell rescanPlugins >/dev/null 2>&1 || true
	log "bar widget removed"
	;;
ports)
	ensure_venv
	"$PYTHON" -m serial.tools.list_ports -v
	;;
*)
	sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
	;;
esac
