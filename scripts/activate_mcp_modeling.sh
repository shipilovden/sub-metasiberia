#!/usr/bin/env bash
set -euo pipefail
release="$(cd -- "${1:-$(dirname -- "$0")}" && pwd -P)"
case "$release" in /srv/metasiberia/releases/master-*-mcp-modeling-*) ;; *) echo "Unexpected release path" >&2; exit 1;; esac
(cd "$release" && sha256sum -c server.sha256)
sudo -v
# The capability-bearing service can deny /proc/PID/exe to its own Unix user.
# Check privileged access BEFORE switching releases. readlink -f alone can
# report the unresolved /proc path as if it were a resolved executable.
old_pid="$(systemctl show metasiberia-server.service -p MainPID --value)"
if [[ "$old_pid" != 0 ]]; then
  if ! old_exe="$(sudo -n readlink -e "/proc/$old_pid/exe")"; then
    echo "Cannot inspect the running service executable with sudo; release not changed." >&2
    exit 1
  fi
  echo "Current executable: $old_exe"
fi
previous="$(readlink -f /srv/metasiberia/releases/current)"
printf '%s\n' "$previous" > "$release/previous-release.txt"
ln -sfn "$release" /srv/metasiberia/releases/current.next
mv -Tf /srv/metasiberia/releases/current.next /srv/metasiberia/releases/current
if ! sudo -n systemctl restart metasiberia-server.service; then
  echo "Restart failed. Restoring previous release." >&2
  ln -sfn "$previous" /srv/metasiberia/releases/current.next
  mv -Tf /srv/metasiberia/releases/current.next /srv/metasiberia/releases/current
  sudo -n systemctl restart metasiberia-server.service
  exit 1
fi
echo "Waiting for the new process, world port and MCP HTTP listener (up to 5 minutes)..."
for ((attempt=0; attempt<150; attempt++)); do
  pid="$(systemctl show metasiberia-server.service -p MainPID --value)"
  exe=""
  if [[ "$pid" != 0 ]] && ! exe="$(sudo -n readlink -e "/proc/$pid/exe" 2>/dev/null)"; then
    if ! sudo -n -v; then
      echo "Sudo authentication expired while checking the process." >&2
      break
    fi
  fi
  if [[ "$exe" == "$release/server" ]] && systemctl is-active --quiet metasiberia-server.service && ss -ltnH 'sport = :7600' | grep -q ':7600'; then
    code="$(curl -ks --max-time 3 -H 'Host: vr.metasiberia.com' -H 'Content-Type: application/json' -X POST --data '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' -o /dev/null -w '%{http_code}' https://127.0.0.1:8443/mcp || true)"
    if [[ "$code" == 401 ]]; then
      echo "Modeling release active: $release"
      echo "World port is listening; MCP authentication endpoint responds. Restart the updated Qt client and start a new AI session."
      exit 0
    fi
  fi
  if ((attempt % 15 == 0)); then
    echo "Waiting: PID=$pid executable=${exe:-not available} MCP_HTTP=${code:-not checked}"
  fi
  sleep 2
done
echo "Startup health check failed: PID=${pid:-0}, executable=${exe:-not available}, MCP_HTTP=${code:-not checked}. Restoring previous release." >&2
ln -sfn "$previous" /srv/metasiberia/releases/current.next
mv -Tf /srv/metasiberia/releases/current.next /srv/metasiberia/releases/current
sudo -n systemctl restart metasiberia-server.service
exit 1
