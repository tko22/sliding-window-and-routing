# Routing Algorithms
this implements the link state and distance vector routing algorithms

To run (64-bit Ubuntu 18.04.3 LTS Desktop)
```
make
```

Then setup the fake network, which uses iptables
```
perl ./utility/make_topology.pl ./utility/testtopo.txt
```

To run nodes for their respective algorithm:
```
./ls_router <nodeid> <initialcostsfile> <logfile>
./vec_router <nodeid> <initialcostsfile> <logfile>
```

for example:
```
./ls_router 5 node5costs logout5.txt
./vec_router 0 costs0.txt test3log0
```

This runs nodes in 10.1.1.<nodeid>. Nodes will then log respective actions in their logfile - (forward, receive, send, unreachable node)


Then, use `./manager_send` to send messages and see the logs get written!

### Network

This network handles partitioning and converges within 5 seconds. 

**Tie breaking rules**
• DV/PV: when two equally good paths are available, your node should choose the one whose next-hop node
ID is lower.
• LS: when choosing which node to move to the finished set next, if there is a tie, choose the lowest node ID.
If a current-best-known path and newly found path are equal in cost, choose the path whose last node
before the destination has the smaller ID. Example:
Source is 1, and the current-best-known path to 9 is 1→4→12→9.
We are currently adding node 10 to the finished set.
1→2→66→34→5→10→9 costs the same as path 1→4→12→9.
We will switch to the new path, since 10<12.