#!/bin/sh

if [ $# -ne 2 ]; then
	exit 1
fi

mkdir -p "$(dirname "$1")"

echo "$2" > "$1"

if [ ! -f "$1" ]; then
	exit 1
fi