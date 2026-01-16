#!/bin/bash
# This is a comment: define the source file and destination directory
SOURCE_FILE="./build/MastheadCamera"
DEST_DIR="/usr/bin/"

# Copy the file
cp -f "$SOURCE_FILE" "$DEST_DIR"

# Optional: print a message to confirm the copy was attempted
echo "Copied $SOURCE_FILE to $DEST_DIR"
