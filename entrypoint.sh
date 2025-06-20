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
ALL_SERVICES="primary backup1 backup2"

# Function to wait for a host and port to be open
wait_for_service() {
    host="$1"
    port="$2"
    echo "INFO: [${SERVICE_NAME}] Waiting for service '$host:$port' to be ready..."
    timeout=30 # seconds
    for i in $(seq $timeout); do
        if nc -z -w 1 "$host" "$port"; then
            echo "INFO: [${SERVICE_NAME}] Service '$host:$port' is ready."
            return 0
        fi
        sleep 1
    done
    echo "Error: [${SERVICE_NAME}] Service '$host:$port' did not become ready within $timeout seconds."
    exit 1 # Exit if the service doesn't become ready
}

# Logic for primary vs. backups
if [ "$SERVICE_NAME" = "primary" ]; then
    # Primary server: It doesn't need to wait for backups to start its own services.
    # It just needs to ensure its own DNS entry is available (which it is by this point).
    echo "INFO: [${SERVICE_NAME}] Primary server, proceeding to start."
else
    # Backup servers: They need to wait for the primary's heartbeat port (5001) to be open.
    wait_for_service "primary" 5001
fi

echo "--- [${SERVICE_NAME}] All services are up. Verifying IPs... ---"
for service in $ALL_SERVICES; do
    IP=$(getent hosts "$service" | awk '{print $1}')
    printf "  %-10s: %s\n" "$service" "$IP"
done
echo "------------------------------------------------------------"

# Execute the command passed from docker-compose.yml (e.g., ["./bin/server_exec", "-p", ...])
echo "INFO: [${SERVICE_NAME}] Starting server with command: $@"
exec "$@"