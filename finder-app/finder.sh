#!/bin/sh

if [ $# -ne 2 ]; then
	exit 1
fi

if [ ! -d "$1" ]; then
	exit 1
fi

files_count=$(find "$1" -type f | wc -l)
match_count=$(grep -r "$2" "$1" | wc -l)

echo "The number of files are $files_count and the number of matching lines are $match_count"

exit 0