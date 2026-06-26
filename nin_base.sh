#!/bin/sh

printf "Rebuilding compilation database..."

ninja -t compdb > compile_commands.json

printf "\rRebuilding compilation database... done"
