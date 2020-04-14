#!/bin/bash

for i in {0..15}
do
    ./ls_router $i “costFile” “logFile” &
done
sleep 5

read -p “Press any key to finish”
pkill ls_router