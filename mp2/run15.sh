#!/bin/bash

for i in {0..64}
do
    ./ls_router $i ./testtopo_initial_link_cost_files/testinitcosts ./log$i.txt &
done
sleep 5

read -p “Press any key to finish”
pkill ls_router