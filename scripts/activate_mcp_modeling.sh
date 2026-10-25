#!/usr/bin/env bash
set -Eeuo pipefail

# One release activation path for the server. The environment overrides below
# exist for isolated tests; production defaults remain fixed to MetaSiberia.
META_ROOT="${METASIBERIA_ROOT:-/srv/metasiberia}"
RELEASES_DIR="$META_ROOT/releases"
CURRENT_LINK="$RELEASES_DIR/current"
SERVICE="${METASIBERIA_SERVICE:-metasiberia-server.service}"
WORLD_PORT=7600
HTTP_PORT=8080
HTTPS_PORT=8443
# The existing Caddy listener serves the current public TLS certificate.
# Port 8443 is the application's internal listener, checked separately below.
MCP_PORT=443
MCP_HOST="${METASIBERIA_MCP_HOST:-vr.metasiberia.com}"
TIMEOUT_SECONDS="${ACTIVATION_TIMEOUT_SECONDS:-600}"
POLL_INTERVAL_SECONDS="${ACTIVATION_POLL_INTERVAL_SECONDS:-2}"

SYSTEMCTL_BIN="${SYSTEMCTL_BIN:-systemctl}"
SUDO_BIN="${SUDO_BIN:-sudo}"
TIMEOUT_BIN="${TIMEOUT_BIN:-timeout}"
READLINK_BIN="${READLINK_BIN:-readlink}"
LN_BIN="${LN_BIN:-ln}"
MV_BIN="${MV_BIN:-mv}"
SS_BIN="${SS_BIN:-ss}"
JOURNALCTL_BIN="${JOURNALCTL_BIN:-journalctl}"
CURL_BIN="${CURL_BIN:-curl}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
SHA256SUM_BIN="${SHA256SUM_BIN:-sha256sum}"
DATE_BIN="${DATE_BIN:-date}"
SLEEP_BIN="${SLEEP_BIN:-sleep}"

fail()
{
	echo "Activation preflight failed: $*" >&2
	exit 1
}

if ! [[ "$TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || (( TIMEOUT_SECONDS < 1 || TIMEOUT_SECONDS > 600 )); then
	fail "ACTIVATION_TIMEOUT_SECONDS must be between 1 and 600 (10 minutes)."
fi
if ! [[ "$POLL_INTERVAL_SECONDS" =~ ^[0-9]+$ ]] || (( POLL_INTERVAL_SECONDS < 1 || POLL_INTERVAL_SECONDS > 30 )); then
	fail "ACTIVATION_POLL_INTERVAL_SECONDS must be between 1 and 30."
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
RELEASE="$(cd -- "${1:-$SCRIPT_DIR}" && pwd -P)" || fail "release directory is unavailable."
case "$RELEASE" in
	"$RELEASES_DIR"/master-*-mcp-modeling-*) ;;
	*) fail "unexpected release path: $RELEASE" ;;
esac
[[ -x "$RELEASE/server" ]] || fail "release server binary is missing or not executable."
[[ -f "$RELEASE/server.sha256" ]] || fail "server.sha256 is missing."
[[ -x "$SYSTEMCTL_BIN" || "$(command -v "$SYSTEMCTL_BIN" || true)" != "" ]] || fail "systemctl is unavailable."
[[ -x "$SUDO_BIN" || "$(command -v "$SUDO_BIN" || true)" != "" ]] || fail "sudo is unavailable."
[[ -x "$TIMEOUT_BIN" || "$(command -v "$TIMEOUT_BIN" || true)" != "" ]] || fail "timeout is unavailable."
[[ -x "$SS_BIN" || "$(command -v "$SS_BIN" || true)" != "" ]] || fail "ss is unavailable."
[[ -x "$JOURNALCTL_BIN" || "$(command -v "$JOURNALCTL_BIN" || true)" != "" ]] || fail "journalctl is unavailable."
[[ -x "$CURL_BIN" || "$(command -v "$CURL_BIN" || true)" != "" ]] || fail "curl is unavailable."
[[ -x "$PYTHON_BIN" || "$(command -v "$PYTHON_BIN" || true)" != "" ]] || fail "python3 is unavailable."
[[ -f "$SCRIPT_DIR/mcp_protocol_probe.py" ]] || fail "mcp_protocol_probe.py must be staged beside activate.sh."
MCP_PROBE="$SCRIPT_DIR/mcp_protocol_probe.py"

read -r EXPECTED_SHA256 CHECKED_FILE _ < "$RELEASE/server.sha256" || fail "could not read server.sha256."
[[ "$EXPECTED_SHA256" =~ ^[0-9a-fA-F]{64}$ && ( "$CHECKED_FILE" == "server" || "$CHECKED_FILE" == "*server" ) ]] || fail "server.sha256 must contain one SHA-256 entry for 'server'."
ACTUAL_SHA256="$("$SHA256SUM_BIN" "$RELEASE/server" | awk '{print $1}')"
[[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]] || fail "server SHA-256 does not match server.sha256."

MCP_LOGIN_TOKEN="${MCP_ACTIVATION_LOGIN_TOKEN:-}"
unset MCP_ACTIVATION_LOGIN_TOKEN
MCP_WORLD="${MCP_ACTIVATION_WORLD:-__root__}"
MCP_PREFLIGHT_MODE=--endpoint
MCP_CHECK_SCOPE="HTTPS endpoint and authentication boundary; authenticated tools not checked"
# Routine service activation must not require an application account. Operators
# may explicitly supply credentials for the additional protocol/bridge check.
if [[ -n "$MCP_LOGIN_TOKEN" ]]; then
	[[ "$MCP_LOGIN_TOKEN" =~ ^[0-9a-fA-F]+\.[0-9a-fA-F]+$ ]] || fail "MCP login token format is invalid; release not changed."
	[[ "$MCP_WORLD" != *$'\n'* && "$MCP_WORLD" != *$'\r'* ]] || fail "active world key is invalid; release not changed."
	MCP_PREFLIGHT_MODE=--basic
	MCP_CHECK_SCOPE="authenticated MCP protocol"
fi

TIMESTAMP="$("$DATE_BIN" -u +%Y%m%dT%H%M%SZ)"
LOG_FILE="$RELEASE/activation-$TIMESTAMP.log"
(umask 077 && : > "$LOG_FILE") || fail "cannot create activation diagnostic log in the release directory."
log()
{
	local now
	now="$("$DATE_BIN" -u +%Y-%m-%dT%H:%M:%SZ)"
	printf '%s %s\n' "$now" "$*" | tee -a "$LOG_FILE"
}

log "Activation started; service=$SERVICE release=$RELEASE timeout=${TIMEOUT_SECONDS}s poll=${POLL_INTERVAL_SECONDS}s."
log "Candidate SHA-256 verified: $ACTUAL_SHA256; binary=$RELEASE/server."
log "MCP health check for host=$MCP_HOST port=$MCP_PORT: $MCP_CHECK_SCOPE."

# Reject a mistyped account/world before switching the link or restarting the
# service. The current release only needs to support the basic MCP contract.
log "Checking the current MCP endpoint before any service changes."
if preflight_output="$(printf '%s\n%s\n' "$MCP_LOGIN_TOKEN" "$MCP_WORLD" | "$PYTHON_BIN" "$MCP_PROBE" "$MCP_HOST" "$MCP_PORT" "$MCP_PREFLIGHT_MODE" 2>&1)"; then
	log "$preflight_output"
else
	log "MCP preflight failed: $preflight_output"
	fail "Предварительная проверка MCP не пройдена. Релиз не переключён, служба не перезапускалась."
fi
unset preflight_output

"$SUDO_BIN" -v || fail "sudo authentication failed; release not changed."
OLD_ACTIVE="$("$SYSTEMCTL_BIN" is-active "$SERVICE" 2>/dev/null || true)"
OLD_PID="$("$SYSTEMCTL_BIN" show "$SERVICE" -p MainPID --value 2>/dev/null || true)"
[[ "$OLD_ACTIVE" == "active" && "$OLD_PID" =~ ^[1-9][0-9]*$ ]] || fail "existing service is not active with a valid PID; release not changed."
OLD_EXE="$("$SUDO_BIN" -n "$READLINK_BIN" -e "/proc/$OLD_PID/exe" 2>/dev/null)" || fail "cannot inspect the active server executable; release not changed."
PREVIOUS="$("$READLINK_BIN" -f "$CURRENT_LINK" 2>/dev/null)" || fail "current release link cannot be resolved."
[[ -x "$PREVIOUS/server" && "$OLD_EXE" == "$PREVIOUS/server" ]] || fail "current link does not match the running server executable; release not changed."
[[ "$PREVIOUS" != "$RELEASE" ]] || fail "candidate is already current."
log "Preflight service state=$OLD_ACTIVE PID=$OLD_PID executable=$OLD_EXE current=$PREVIOUS."

phase_started_epoch=0
last_failure=""
last_systemctl_failure=""
transaction_active=0

refresh_sudo()
{
	if ! "$SUDO_BIN" -n -v 2>/dev/null; then
		log "sudo timestamp needs refresh before a privileged check."
		"$SUDO_BIN" -v || return 1
	fi
}

restart_service()
{
	local budget="${1:-$TIMEOUT_SECONDS}"
	local output status
	if output="$("$TIMEOUT_BIN" --signal=TERM --kill-after=10 "$budget" \
		"$SUDO_BIN" -n "$SYSTEMCTL_BIN" restart "$SERVICE" 2>&1)"; then
		last_systemctl_failure=""
		log "systemd restart completed."
		return 0
	else
		status=$?
	fi
	last_systemctl_failure="systemd restart failed (exit=$status): ${output:-no diagnostic output}"
	log "$last_systemctl_failure"
	return "$status"
}

run_systemctl_action()
{
	local action="$1"
	local budget="$2"
	local output status
	if output="$("$TIMEOUT_BIN" --signal=TERM --kill-after=10 "$budget" \
		"$SUDO_BIN" -n "$SYSTEMCTL_BIN" "$action" "$SERVICE" 2>&1)"; then
		last_systemctl_failure=""
		log "systemd $action completed."
		return 0
	else
		status=$?
	fi
	last_systemctl_failure="systemd $action failed (exit=$status): ${output:-no diagnostic output}"
	log "$last_systemctl_failure"
	return "$status"
}

switch_current()
{
	local target="$1"
	local temp_link="$CURRENT_LINK.next.$$"
	"$LN_BIN" -sfn "$target" "$temp_link" || return 1
	if ! "$MV_BIN" -Tf "$temp_link" "$CURRENT_LINK"; then
		rm -f "$temp_link"
		return 1
	fi
}

journal_time()
{
	local line="$1"
	local epoch="${line%% *}"
	epoch="${epoch%%.*}"
	if [[ "$epoch" =~ ^[0-9]+$ ]]; then
		"$DATE_BIN" -u -d "@$epoch" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || "$DATE_BIN" -u +%Y-%m-%dT%H:%M:%SZ
	else
		"$DATE_BIN" -u +%Y-%m-%dT%H:%M:%SZ
	fi
}

probe_mcp()
{
	local mode="$1"
	local -a args=("$MCP_HOST" "$MCP_PORT")
	local output
	if [[ -z "$MCP_LOGIN_TOKEN" ]]; then
		args+=(--endpoint)
	else
		# neuro.* is registered by the Qt bridge, not by this server.
		args+=(--basic)
	fi
	if output="$(printf '%s\n%s\n' "$MCP_LOGIN_TOKEN" "$MCP_WORLD" | "$PYTHON_BIN" "$MCP_PROBE" "${args[@]}" 2>&1)"; then
		log "$output"
		return 0
	fi
	last_failure="$output"
	log "MCP protocol check failed: $output"
	# These responses cannot become healthy by waiting for another poll.
	case "$output" in
		*"unsupported protocol version"*|*"CERTIFICATE_VERIFY_FAILED"*|*"no-client error"*|*"JSON-RPC error"*) return 2 ;;
	esac
	if [[ "$output" =~ HTTP\ 4[0-9][0-9] && "$output" != *"HTTP 429"* ]]; then return 2; fi
	return 1
}

wait_for_ready()
{
	local expected_release="$1"
	local phase="$2"
	local mcp_mode="$3"
	local start_epoch="$4"
	local now elapsed
	local unit_state unit_substate pid exec_code exec_status exe
	local journal_output world_start_line world_done_line duration
	local world_started=0 world_loaded=0
	local world_port_seen=0 http_port_seen=0 https_port_seen=0
	local http_ok=0 mcp_ok=0 last_status_log=0
	local http_meta http_status http_location http_type
	local deadline=$((start_epoch + TIMEOUT_SECONDS))
	last_failure="$phase readiness has not completed."
	log "$phase readiness checks started; expected executable=$expected_release/server; deadline=${TIMEOUT_SECONDS}s."

	while :; do
		now="$("$DATE_BIN" +%s)"
		elapsed=$((now - start_epoch))
		(( now <= deadline )) || break
		if ! refresh_sudo; then last_failure="sudo access expired during $phase checks."; log "$last_failure"; return 1; fi

		unit_state="$("$SYSTEMCTL_BIN" show "$SERVICE" -p ActiveState --value 2>/dev/null || true)"
		unit_substate="$("$SYSTEMCTL_BIN" show "$SERVICE" -p SubState --value 2>/dev/null || true)"
		pid="$("$SYSTEMCTL_BIN" show "$SERVICE" -p MainPID --value 2>/dev/null || true)"
		exec_code="$("$SYSTEMCTL_BIN" show "$SERVICE" -p ExecMainCode --value 2>/dev/null || true)"
		exec_status="$("$SYSTEMCTL_BIN" show "$SERVICE" -p ExecMainStatus --value 2>/dev/null || true)"
		exe=""
		if [[ "$unit_state" == "active" && "$pid" =~ ^[1-9][0-9]*$ ]]; then
			exe="$("$SUDO_BIN" -n "$READLINK_BIN" -e "/proc/$pid/exe" 2>/dev/null || true)"
		fi
		if [[ "$unit_state" != "active" || "$unit_substate" != "running" || ! "$pid" =~ ^[1-9][0-9]*$ || "$exe" != "$expected_release/server" ]]; then
			last_failure="process check failed: systemd=$unit_state/$unit_substate PID=${pid:-0} executable=${exe:-unavailable} ExecMainCode=${exec_code:-unknown} ExecMainStatus=${exec_status:-unknown}."
		else
			journal_output="$("$JOURNALCTL_BIN" --unit "$SERVICE" --since "@$start_epoch" --output short-unix --no-pager 2>/dev/null || true)"
			world_start_line="$(printf '%s\n' "$journal_output" | grep -m 1 -E 'Reading world state from ' || true)"
			world_done_line="$(printf '%s\n' "$journal_output" | grep -m 1 -E 'Loaded [0-9]+ object\(s\),.* in ' || true)"
			if [[ -n "$world_start_line" && "$world_started" == 0 ]]; then
				world_started=1
				log "$phase world-load start detected at $(journal_time "$world_start_line")."
			fi
			if [[ -n "$world_done_line" && "$world_loaded" == 0 ]]; then
				world_loaded=1
				duration="${world_done_line##* in }"
				log "$phase world-load completion detected at $(journal_time "$world_done_line"); server-reported duration=$duration."
			fi
			# Record each port as soon as it is observed, including while the world
			# is still loading. Application checks remain gated on world readiness.
			if (( ! world_port_seen )) && "$SS_BIN" -ltnH "sport = :$WORLD_PORT" 2>/dev/null | grep -q .; then
				world_port_seen=1; log "$phase TCP port $WORLD_PORT (game server) observed listening at elapsed=${elapsed}s."
			fi
			if (( ! http_port_seen )) && "$SS_BIN" -ltnH "sport = :$HTTP_PORT" 2>/dev/null | grep -q .; then
				http_port_seen=1; log "$phase TCP port $HTTP_PORT (HTTP) observed listening at elapsed=${elapsed}s."
			fi
			if (( ! https_port_seen )) && "$SS_BIN" -ltnH "sport = :$HTTPS_PORT" 2>/dev/null | grep -q .; then
				https_port_seen=1; log "$phase TCP port $HTTPS_PORT (internal HTTPS) observed listening at elapsed=${elapsed}s."
			fi
			if [[ "$world_loaded" != 1 ]]; then
				last_failure="world state load completion marker has not appeared in the service journal."
			else
				if (( world_port_seen && http_port_seen && https_port_seen )); then
					if (( ! http_ok )); then
						http_meta="$("$CURL_BIN" --silent --show-error --max-time 5 --output /dev/null --write-out $'%{http_code}\t%{redirect_url}\t%{content_type}' --header "Host: $MCP_HOST" "http://127.0.0.1:$HTTP_PORT/" 2>/dev/null || true)"
						IFS=$'\t' read -r http_status http_location http_type <<< "$http_meta"
						if [[ "$http_status" == "301" || "$http_status" == "302" ]] && [[ "$http_location" == "https://$MCP_HOST/"* ]]; then
							http_ok=1
							log "$phase HTTP check OK: GET / returned HTTP $http_status and canonical HTTPS redirect (content-type=${http_type:-none})."
						else
							last_failure="HTTP check failed: port $HTTP_PORT GET / returned status=${http_status:-no-response} location=${http_location:-none} content-type=${http_type:-none}; expected redirect to https://$MCP_HOST/."
						fi
					fi
					if (( http_ok && ! mcp_ok )); then
						if probe_mcp "$mcp_mode"; then
							mcp_ok=1
						else
							local probe_result=$?
							if (( probe_result == 2 )); then return 1; fi
						fi
					fi
					if (( http_ok && mcp_ok )); then
						log "$phase readiness OK in $elapsed seconds: systemd=$unit_state/$unit_substate PID=$pid executable=$exe; ports=$WORLD_PORT,$HTTP_PORT,$HTTPS_PORT; HTTP passed; MCP: $MCP_CHECK_SCOPE."
						return 0
					fi
				fi
			fi
		fi

		if (( now - last_status_log >= 30 )); then
			last_status_log=$now
			log "$phase waiting: elapsed=${elapsed}s systemd=${unit_state:-unknown}/${unit_substate:-unknown} PID=${pid:-0} executable=${exe:-unavailable}; $last_failure"
		fi
		"$SLEEP_BIN" "$POLL_INTERVAL_SECONDS"
	done
	last_failure="$phase exceeded its ${TIMEOUT_SECONDS}s readiness deadline: $last_failure"
	log "$last_failure"
	return 1
}

rollback()
{
	local reason="$1"
	local stopped=0
	log "ROLLBACK started; cause=$reason; restoring=$PREVIOUS."
	if refresh_sudo && run_systemctl_action stop 60; then
		stopped=1
		log "ROLLBACK stopped the candidate service before restoring the release link."
	else
		log "ROLLBACK could not confirm a clean stop; it will restart the service after restoring the previous link."
	fi
	if ! switch_current "$PREVIOUS"; then
		log "ROLLBACK FAILED: could not atomically restore current link; manual service recovery is required."
		transaction_active=0
		return 1
	fi
	transaction_active=0
	if ! refresh_sudo; then
		log "ROLLBACK FAILED: previous link restored but sudo could not be refreshed; manual service recovery is required."
		return 1
	fi
	phase_started_epoch="$("$DATE_BIN" +%s)"
	if (( stopped )); then
		if ! run_systemctl_action start "$TIMEOUT_SECONDS"; then
			log "ROLLBACK FAILED: previous link restored but $last_systemctl_failure; manual service recovery is required."
			return 1
		fi
	else
		if ! restart_service; then
			log "ROLLBACK FAILED: previous link restored but $last_systemctl_failure; manual service recovery is required."
			return 1
		fi
	fi
	if wait_for_ready "$PREVIOUS" "rollback" basic "$phase_started_epoch"; then
		log "ROLLBACK verified: previous executable, world load, required ports and HTTP are ready; MCP: $MCP_CHECK_SCOPE."
		return 0
	fi
	log "ROLLBACK FAILED: previous release did not pass readiness: $last_failure"
	return 1
}

handle_exit()
{
	local status=$?
	trap - EXIT INT TERM
	if (( transaction_active )); then
		log "Unexpected exit status=$status after release switch; starting rollback."
		rollback "activation script exited unexpectedly with status $status" || status=2
	fi
	(( status == 0 )) || exit "$status"
}
trap handle_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

phase_started_epoch="$("$DATE_BIN" +%s)"
if ! switch_current "$RELEASE"; then fail "could not atomically switch current link; service not restarted."; fi
transaction_active=1
log "Current release link switched to $RELEASE."
remaining_budget=$((phase_started_epoch + TIMEOUT_SECONDS - $("$DATE_BIN" +%s)))
if (( remaining_budget < 1 )); then
	rollback "candidate readiness deadline expired before the service restart" || exit 2
	exit 1
fi
if ! restart_service "$remaining_budget"; then
	rollback "$last_systemctl_failure" || exit 2
	exit 1
fi

if wait_for_ready "$RELEASE" "candidate" neuro "$phase_started_epoch"; then
	transaction_active=0
	log "Candidate release is ready. Current link=$RELEASE; no further service changes were made."
	exit 0
fi

failure_reason="$last_failure"
if rollback "$failure_reason"; then
	log "Candidate was not activated; previous release is restored and verified. Diagnostics: $LOG_FILE"
	exit 1
fi
log "Candidate failed and rollback readiness could not be verified. Diagnostics: $LOG_FILE"
exit 2
