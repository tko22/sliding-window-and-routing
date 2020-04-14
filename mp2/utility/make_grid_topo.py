import networkx as nx
import matplotlib.pyplot as plt

size = 8
file_name = "grid_topo_8x8.txt"


def make_topo():
    topo = []
    with open(file_name, "a") as out:
        id = 0
        for i in range(size):
            row = []
            for j in range(size):
                row.append(id)
                id += 1
                if j > 0:
                    out.write(f"{row[j-1]} {row[j]}\n")
                if i > 0:
                    out.write(f"{topo[i-1][j]} {row[j]}\n")
            topo.append(row)
            print(topo)

def graph_topo():
    G=nx.Graph()
    # for i in range(size*size)
        # G.add_nodes_from([str(i) for i in range(size*size)])

    with open(file_name) as infile:
        for line in infile:
            nodes = line.rstrip("\n").split(" ")
            G.add_edge(nodes[0], nodes[1])

    print("Nodes of graph: ")
    print(G.nodes())
    print("Edges of graph: ")
    print(G.edges())

    nx.draw(G,with_labels = True)
    plt.savefig("grid_topo_8x8.png") # save as png
graph_topo()



