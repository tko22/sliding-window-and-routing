#!/bin/bash
for i in {0..7}
do
    ./vec_router $i ./testtopo_initial_link_cost_files/testinitcosts$i ./logs/log$i.txt &
done
./vec_router 255 ./testtopo_initial_link_cost_files/testinitcosts255 ./logs/log255.txt &
sleep 5

read -p "Press enter to finish"
pkill vec_router