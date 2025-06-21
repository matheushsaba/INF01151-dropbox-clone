#!/bin/sh
# This script ensures services start in the correct order by waiting for
# critical ports to be open, prints the IP of all services, and then
# executes the main server command.
set -e

# The SERVICE_NAME environment variable must be set in docker-compose.yml
# (e.g., SERVICE_NAME=primary) so the script knows which container it is.
if [ -z "$SERVICE_NAME" ]; then
  echo "Error: SERVICE_NAME environment variable is not set."
  exit 1 # Exit if SERVICE_NAME is not provided
fi

# List of all services in the cluster
ALL_SERVICES="frontend primary backup1 backup2"

echo "--- [${SERVICE_NAME}] All services are up. Verifying IPs... ---"
for service in $ALL_SERVICES; do
    # IP=$(getent hosts "$service" | awk '{print $1}')
    # Use timeout to prevent long waits for services that are not yet up.
    # Redirect stderr to /dev/null to suppress errors if the host is not found.
    IP=$(timeout 0.5 getent hosts "$service" 2>/dev/null | awk '{print $1}')
    printf "  %-10s: %s\n" "$service" "$IP"
done
echo "------------------------------------------------------------"

# Execute the command passed from docker-compose.yml (e.g., ["./bin/server_exec", "-p", ...])
echo "INFO: [${SERVICE_NAME}] Starting server with command: $@"

if [ "$SERVICE_NAME" = "primary" ]; then
    # Start the server in the background so we can wait for it
    "$@" &
    SERVER_PID=$!

    echo "========================================================"
    echo "✅  PRIMARY SERVER IS READY. You can now start backups."
    echo "========================================================"

    wait $SERVER_PID # Wait for the server process to exit
else
    exec "$@" # Backups can start directly

    echo "========================================================"
    echo "✅  BACKUP SERVER IS READY."
    echo "========================================================"
fi