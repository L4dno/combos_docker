#!/bin/bash

# 100,000 hosts, 1 day

	cp analysis/parameters.xml parameters.xml
	./generator
	./execute > experiments/results_from_script.txt
	
# 200,000 hosts, 1 day

	# line="<n_clients>200000<\/n_clients>\t\t\t <!-- Number of clients of the cluster -->" 
	# sed -i "s/<n_clients>.*/${line}/" parameters.xml	
	# ./generator
	# ./execute > performance/1day/0200000

