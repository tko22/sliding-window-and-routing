use warnings;
use strict;

my $ifName = "enp0s3";

die ("Usage: perl make_topology.pl topology_filename\n") unless (defined $ARGV[0]);

my $topoFileName = $ARGV[0];

# Reset the interface
#`sudo ifconfig $ifName down`;	# Doing so will remove all the virtual interfaces.
#`sudo ifconfig $ifName up`;

# NOTE: this next command is what you want to run if you want all the iptables stuff to go away
`sudo iptables --flush`;

`sudo ifconfig $ifName:manager up 10.0.0.10`;

open(TOPOFILE, "<$topoFileName") or die("$topoFileName does not exist.\n");
while(my $curLine = <TOPOFILE>)
{
	$curLine =~ s/\n//;
	next unless($curLine =~ m/[0-9]/);
	
	my @nodes = split(/ /, $curLine);
	my $addr0 = "10.1.1.$nodes[0]";
	my $addr1 = "10.1.1.$nodes[1]";
	
	#note: despite giving SIOCSIFFLAGS: Cannot assign requested address, it's fine
	`sudo ifconfig $ifName:$nodes[0] up $addr0 2>/dev/null`;
	`sudo ifconfig $ifName:$nodes[1] up $addr1 2>/dev/null`;
	
	`sudo iptables -A OUTPUT -s $addr0 -d $addr1 -j ACCEPT`;
	`sudo iptables -A OUTPUT -s $addr1 -d $addr0 -j ACCEPT`;
}

`sudo iptables -A OUTPUT -s 10.0.0.10 -p udp --dport 7777 -j ACCEPT`;
#NOTE: if you have your VM configured with the NAT interface that lets
#	it access the internet, this next line will cut it off.
#	Can you figure out how to tell iptables to keep that address online?
`sudo iptables -A OUTPUT -s 10.0.0.0/8 -j DROP`;
