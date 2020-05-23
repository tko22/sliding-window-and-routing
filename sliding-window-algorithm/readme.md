# Sliding Window Algorithm

This implements the cumulative ack sliding window algorithm. It detects packet loss, increase in RTT, bandwidth x delay product and adapts window size appropriately - similar to TCP.

It utilizes UDP. Both sender and receiver will exit once packet is fully sent and received.

### Environment

Set drop % of packets.
```
sudo tc qdisc del dev eth0 root 2>/dev/null
sudo tc qdisc add dev eth0 root handle 1:0 netem delay 20ms loss 5%
sudo tc qdisc add dev eth0 parent 1:1 handle 10: tbf rate 100Mbit burst
40mb latency 25ms
```

Then to run,
```
make

./reliable_sender <rcv_hostname> <rcv_port> <file_path_to_read> <bytes_to_send>
./reliable_receiver <rcv_port> <file_path_to_write>
```


### Other
- Two instances of your protocol competing with each other must converge to roughly fair sharing the link (same throughputs +/- 10%) within 100 RTTs.
- somewhat TCP friendly - kind of competes with TCP
- The MTU on the test network is 1500