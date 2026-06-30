#!/bin/bash

printf "create %s\n" "${1}"
for n in "${@:2}" ; do
  printf "addlib %s\n" "${n}"
done
printf "save\n"
printf "end\n"
