#!/usr/bin/env bash

set -euo pipefail

SESSION_NAME="DB"
PROJECT="$HOME/db_scratch"

# If the session already exists, just attach to it.
if tmux has-session -t "$SESSION_NAME" 2>/dev/null; then
    exec tmux attach -t "$SESSION_NAME"
fi

# Create the session and first window.
tmux new-session \
    -d \
    -s "$SESSION_NAME" \
    -n "main1" \
    -c "$PROJECT"

tmux send-keys -t "$SESSION_NAME:main1" "nvim" C-m

# Second source window.
tmux new-window \
    -t "$SESSION_NAME" \
    -n "main2" \
    -c "$PROJECT"

tmux send-keys -t "$SESSION_NAME:main2" "nvim" C-m

# Header/source browsing window.
tmux new-window \
    -t "$SESSION_NAME" \
    -n "headers" \
    -c "$PROJECT"

tmux send-keys -t "$SESSION_NAME:headers" "nvim" C-m

# Build window.
tmux new-window \
    -t "$SESSION_NAME" \
    -n "compile" \
    -c "$PROJECT"

# perf
tmux new-window \
    -t "$SESSION_NAME" \
    -n "perf" \
    -c "$HOME/valkey/"


# Return to the main editing window.
tmux select-window -t "$SESSION_NAME:main1"

# Attach.
exec tmux attach -t "$SESSION_NAME"
