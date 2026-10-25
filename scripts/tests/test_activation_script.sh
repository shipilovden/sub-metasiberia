#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
ACTIVATE_SCRIPT="$SCRIPT_DIR/activate_mcp_modeling.sh"
if [[ "$(uname -s)" != "Linux" ]]; then
	echo "activation integration scenarios require Linux Bash/systemd path semantics; no activation was attempted" >&2
	exit 77
fi
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/metasiberia-activation-test.XXXXXX")"
TEST_ROOT="$(cd -- "$TEST_ROOT" && pwd -P)"
TEST_TIMEOUT_BIN="$(command -v timeout || true)"
[[ -n "$TEST_TIMEOUT_BIN" ]] || { echo "timeout utility is required for isolated activation tests" >&2; exit 1; }
cleanup()
{
	case "$TEST_ROOT" in
		"${TMPDIR:-/tmp}"/metasiberia-activation-test.*|/tmp/metasiberia-activation-test.*) rm -rf -- "$TEST_ROOT" ;;
		*) echo "Refusing to remove unexpected test path: $TEST_ROOT" >&2; return 1 ;;
	esac
}
trap cleanup EXIT

MOCK_BIN="$TEST_ROOT/mock-bin"
mkdir -p "$MOCK_BIN"

cat > "$MOCK_BIN/sudo" <<'MOCK'
#!/usr/bin/env bash
set -e
printf 'sudo %s\n' "$1" >> "$ACTIVATION_TEST_EVENTS"
if [[ "$1" == "-v" ]]; then exit 0; fi
[[ "$1" == "-n" ]] && shift
if [[ "$1" == "-v" ]]; then exit 0; fi
case "$1" in
	"$SYSTEMCTL_BIN") exec "$SYSTEMCTL_BIN" "${@:2}" ;;
	"$READLINK_BIN") exec "$READLINK_BIN" "${@:2}" ;;
	*) echo "unexpected sudo command" >&2; exit 90 ;;
esac
MOCK

cat > "$MOCK_BIN/systemctl" <<'MOCK'
#!/usr/bin/env bash
set -e
printf 'systemctl %s\n' "$1" >> "$ACTIVATION_TEST_EVENTS"
case "$1" in
	is-active) echo active ;;
	show)
		property=""
		while (($#)); do
			if [[ "$1" == "-p" ]]; then property="$2"; break; fi
			shift
		done
		case "$property" in
			ActiveState) echo active ;;
			SubState) echo running ;;
			MainPID) echo 4242 ;;
			ExecMainCode|ExecMainStatus) echo 0 ;;
			*) exit 91 ;;
		esac
		;;
	restart|start|stop) printf '%s\n' "$1" >> "$ACTIVATION_TEST_EVENTS" ;;
	*) echo "unexpected systemctl command" >&2; exit 92 ;;
esac
MOCK

cat > "$MOCK_BIN/readlink" <<'MOCK'
#!/usr/bin/env bash
set -e
printf 'readlink %s\n' "$1" >> "$ACTIVATION_TEST_EVENTS"
if [[ "$1" == "-e" && "$2" == /proc/*/exe ]]; then
	current="$(cat "$ACTIVATION_TEST_CURRENT")"
	printf '%s/server\n' "$current"
elif [[ "$1" == "-f" && "$2" == "$ACTIVATION_TEST_CURRENT" ]]; then
	cat "$ACTIVATION_TEST_CURRENT"
else
	echo "unexpected readlink request" >&2
	exit 93
fi
MOCK

cat > "$MOCK_BIN/ln" <<'MOCK'
#!/usr/bin/env bash
set -e
[[ "$1" == "-sfn" && $# == 3 ]] || exit 94
printf '%s\n' "$2" > "$3"
MOCK

cat > "$MOCK_BIN/mv" <<'MOCK'
#!/usr/bin/env bash
set -e
[[ "$1" == "-Tf" && $# == 3 ]] || exit 95
cp "$2" "$3"
rm "$2"
MOCK

cat > "$MOCK_BIN/ss" <<'MOCK'
#!/usr/bin/env bash
printf 'LISTEN 0 128 127.0.0.1:mock\n'
MOCK

cat > "$MOCK_BIN/journalctl" <<'MOCK'
#!/usr/bin/env bash
now="$(date +%s)"
printf '%s.000000 Reading world state from [test fixture]\n' "$now"
printf '%s.000000 Loaded 10 object(s), 0 placeholder(s) in 200.7 seconds\n' "$now"
MOCK

cat > "$MOCK_BIN/curl" <<'MOCK'
#!/usr/bin/env bash
printf '301\thttps://vr.metasiberia.com/\ttext/html\n'
MOCK

cat > "$MOCK_BIN/sleep" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK

cat > "$MOCK_BIN/python3" <<'MOCK'
#!/usr/bin/env bash
set -e
if [[ "${ACTIVATION_TEST_SCENARIO:-}" == no-credentials-* ]]; then
	[[ "${4:-}" == "--endpoint" ]] || { echo 'default activation tried to authenticate' >&2; exit 97; }
	if [[ "$ACTIVATION_TEST_SCENARIO" == "no-credentials-preflight-failure" ||
		( "$ACTIVATION_TEST_SCENARIO" == "no-credentials-failure" && "$(cat "$ACTIVATION_TEST_CURRENT")" == *master-test-mcp-modeling-new ) ]]; then
		echo 'MCP endpoint returned HTTP 404'
		exit 1
	fi
	echo 'MCP endpoint check OK; authenticated tools not checked'
	exit 0
fi
if [[ "${ACTIVATION_TEST_SCENARIO:-success}" == "preflight-mcp-failure" ]]; then
	echo 'MCP protocol check failed: initialize returned HTTP 401'
	exit 1
fi
if [[ "${ACTIVATION_TEST_SCENARIO:-success}" == "candidate-mcp-failure" && "$(cat "$ACTIVATION_TEST_CURRENT")" == *master-test-mcp-modeling-new ]]; then
	echo 'MCP protocol check failed: initialize returned HTTP 401'
	exit 1
fi
if [[ "${4:-}" == "--basic" ]]; then
	echo 'MCP protocol check OK: initialized 2024-11-05; tools/list returned 1 tools with base tools'
else
	echo 'MCP protocol check OK: initialized 2024-11-05; tools/list returned 1 tools with bridge probe and no-client rejection'
fi
MOCK

chmod +x "$MOCK_BIN"/*

make_release()
{
	local release="$1"
	mkdir -p "$release"
	printf '#!/usr/bin/env bash\nexit 0\n' > "$release/server"
	chmod +x "$release/server"
	(cd "$release" && sha256sum server > server.sha256)
}

run_case()
{
	local scenario="$1"
	local expected_status="$2"
	local case_root="$TEST_ROOT/$scenario"
	local releases="$case_root/releases"
	local previous="$releases/master-old-mcp-modeling-old"
	local candidate="$releases/master-test-mcp-modeling-new"
	local current="$releases/current"
	local status current_target
	local test_token='616c696365.70617373'
	if [[ "$scenario" == no-credentials-* ]]; then test_token=""; fi
	mkdir -p "$releases"
	make_release "$previous"
	make_release "$candidate"
	printf '%s\n' "$previous" > "$current"
	: > "$case_root/events.log"

	set +e
	METASIBERIA_ROOT="$case_root" \
	MCP_ACTIVATION_LOGIN_TOKEN="$test_token" \
	MCP_ACTIVATION_WORLD='__root__' \
	ACTIVATION_TIMEOUT_SECONDS=5 \
	ACTIVATION_POLL_INTERVAL_SECONDS=1 \
	ACTIVATION_TEST_CURRENT="$current" \
	ACTIVATION_TEST_EVENTS="$case_root/events.log" \
	ACTIVATION_TEST_SCENARIO="$scenario" \
	SYSTEMCTL_BIN="$MOCK_BIN/systemctl" \
	SUDO_BIN="$MOCK_BIN/sudo" \
	READLINK_BIN="$MOCK_BIN/readlink" \
	LN_BIN="$MOCK_BIN/ln" \
	MV_BIN="$MOCK_BIN/mv" \
	SS_BIN="$MOCK_BIN/ss" \
	JOURNALCTL_BIN="$MOCK_BIN/journalctl" \
	CURL_BIN="$MOCK_BIN/curl" \
	PYTHON_BIN="$MOCK_BIN/python3" \
	SLEEP_BIN="$MOCK_BIN/sleep" \
	"$TEST_TIMEOUT_BIN" --signal=TERM --kill-after=2 10 \
		"$ACTIVATE_SCRIPT" "$candidate" </dev/null > "$case_root/output.log" 2>&1
	status=$?
	set -e
	current_target="$(cat "$current")"
	if [[ "$status" != "$expected_status" ]]; then
		cat "$case_root/output.log" >&2
		cat "$case_root/events.log" >&2
		echo "$scenario: expected exit $expected_status, received $status" >&2
		return 1
	fi
	if [[ "$expected_status" == 0 && "$current_target" != "$candidate" ]]; then
		cat "$case_root/output.log" >&2
		echo "successful activation did not retain candidate current link" >&2
		return 1
	fi
	if [[ "$expected_status" != 0 && "$current_target" != "$previous" ]]; then
		cat "$case_root/output.log" >&2
		echo "failed candidate was not rolled back to the previous release" >&2
		return 1
	fi
	if [[ "$scenario" == "preflight-mcp-failure" || "$scenario" == "no-credentials-preflight-failure" ]]; then
		if [[ -s "$case_root/events.log" ]] || ! grep -q 'MCP preflight failed' "$case_root/output.log"; then
			echo "failed login must stop before any privileged operation or service change" >&2
			return 1
		fi
		echo "activation scenario OK: $scenario (no service changes)"
		return 0
	fi
	if ! grep -q 'world-load completion detected' "$case_root/output.log" || ! grep -q 'TCP port 8443' "$case_root/output.log"; then
		cat "$case_root/output.log" >&2
		echo "$scenario: readiness milestones were not logged" >&2
		return 1
	fi
	if [[ "$expected_status" != 0 ]] && ! grep -q 'ROLLBACK verified' "$case_root/output.log"; then
		cat "$case_root/output.log" >&2
		echo "rollback was not verified" >&2
		return 1
	fi
	echo "activation scenario OK: $scenario (exit=$status, current=$(basename "$current_target"))"
}

run_case success 0
run_case candidate-mcp-failure 1
run_case preflight-mcp-failure 1
run_case no-credentials-success 0
run_case no-credentials-failure 1
run_case no-credentials-preflight-failure 1
