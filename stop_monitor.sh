#!/bin/bash

# Find and kill all idf.py monitor processes
echo "Stopping all IDF monitors..."

# Find all python processes running idf.py monitor
pids=$(ps aux | grep "[p]ython.*idf.py.*monitor" | awk '{print $2}')

if [ -z "$pids" ]; then
    echo "No active IDF monitors found."
    exit 0
fi

# Kill each process
for pid in $pids; do
    echo "Stopping monitor process $pid..."
    kill $pid 2>/dev/null
done

echo "All monitors stopped successfully."
