# ElectrumX Supervisor (Qt / headless)

Headless Qt supervisor for ElectrumX: automatic history compaction and restart.

This tool runs `history.sh` (typically `electrumx_compact_history`) and then starts ElectrumX via `start.sh`.  
If ElectrumX stops for any reason, the supervisor automatically runs `history.sh` again and restarts ElectrumX.

It also handles clean shutdown: on `SIGTERM` (systemd stop) or `SIGINT` (Ctrl+C), it terminates child processes and exits.

---

## How it works

Startup sequence:

1. Run `history.sh`
2. If `history.sh` exits with code `0`, run `start.sh`

Runtime behavior:

- If ElectrumX exits, the supervisor waits briefly and restarts the cycle: `history.sh` → `start.sh`
- If `history.sh` fails (non-zero exit code), the supervisor retries with exponential backoff (up to 5 minutes)

Shutdown behavior:

- On `SIGTERM` / `SIGINT`, the supervisor stops any running `history.sh` and `start.sh` child process and then exits.
- No restarts occur during shutdown.

---

## Directory layout

The supervisor expects a working directory containing your scripts (and the log file):
```
/home/ubuntu/electrumx/
├── start.sh
├── history.sh
└── supervisor.log
```
The working directory is configurable using `-dir=xxx`.

---

## Usage

### Basic 
```bash
cd supervisor
./start.sh
``` 
Default values:
```
-dir=/home/ubuntu/electrumx
-start=start.sh
-history=history.sh
```
Notes:
-start and -history are treated as file names relative to -dir. 
Scripts must exist and be executable.

### Custom directory and script names
```bash
./start.sh -dir=/home/ubuntu/electrumx -start=start.sh -history=history.sh
```
### Enable console debug output
By default, process debug output is written to supervisor.log but not printed to console.
```bash
./start.sh -debug (or -d)
```

### Print version
```bash
./start.sh -version (or -v)
```
|          Argument | Description                                                          | Example                       |
| ----------------: | -------------------------------------------------------------------- | ----------------------------- |
| `-v` / `-version` | Print application version and exit                                   | `-v`                          |
|   `-d` / `-debug` | Print Qt debug logs to console (ElectrumX output is logged as debug) | `-debug`                      |
|     `-dir=<path>` | Working directory that contains scripts                              | `-dir=/home/ubuntu/electrumx` |
|   `-start=<file>` | ElectrumX start script file name (relative to `-dir`)                | `-start=start.sh`             |
| `-history=<file>` | History compaction script file name (relative to `-dir`)             | `-history=history.sh`         |

---
### Scripts
#### start.sh

This script must start ElectrumX in the foreground (do not daemonize).
The supervisor monitors the process and restarts it when it exits.

Recommended pattern:
- Export required ElectrumX environment variables
- Set file descriptor limit (or configure it in systemd)
- Use exec so the script process becomes the ElectrumX process

Example (with nginx managing ssl/wss and venv):
```bash
#!/usr/bin/env bash
set -euo pipefail

export CACHE_MB=1200
export COIN=ElectraProtocol
export DB_DIRECTORY="$HOME/.electrumx/xep"
export DAEMON_URL="http://user:pass@127.0.0.1:PORT"
export NET=mainnet
export DB_ENGINE=leveldb

# (Nginx) Recommended: bind local services and update internal ports to 51001 and 51003 to not get a conflict with Nginx which is listening on 50001 and 50003 already
export SERVICES="tcp://127.0.0.1:51001,ws://127.0.0.1:51003,rpc://"

ulimit -n 10000

exec "$HOME/electrumx/venv/bin/electrumx_server"
```

#### history.sh

This script must run ElectrumX history compaction and exit with status code 0 on success.

Recommended pattern:
- Same environment variables as start.sh
- Use exec for correct exit code propagation

Example:
```bash
#!/usr/bin/env bash
set -euo pipefail

export CACHE_MB=1200
export COIN=ElectraProtocol
export DB_DIRECTORY="$HOME/.electrumx/xep"
export DAEMON_URL="http://user:pass@127.0.0.1:PORT"
export NET=mainnet
export DB_ENGINE=leveldb

ulimit -n 10000

exec "$HOME/electrumx/venv/bin/electrumx_compact_history"
```
---
### Logging

The supervisor writes logs to:
`<workdir>/supervisor.log` (default: /home/ubuntu/electrumx/supervisor.log)

Console output:
- Supervisor info/warnings are printed
- Debug logs (typically ElectrumX stdout/stderr) are printed only with -debug/-d
---
### Notes for production (systemd)
For production deployments, run the supervisor under `systemd` and set:
- `Restart=always`
- `LimitNOFILE=10000`
- `KillMode=control-group`

This ensures the supervisor starts on boot, is restarted on failure, and all child processes are terminated if the service stops.