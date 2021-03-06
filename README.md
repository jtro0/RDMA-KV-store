# RDMA based key-value store
A multithreaded key-value server using RDMA technology, and accommodating benchmark, used for my Bachelor Thesis: Scalability of RDMA transport types in a Key-Value store application. The thesis report can be found in `write_up/thesis.pdf`.

The server and benchmarking support RDMA transport types RC, UC, and UD, along with TCP. For RDMA, this program makes use of two-sided channel semantics, `SEND` and `RECV`.
Benchmarking is done with 10 million key-value store operations (GET and SET). 

## Dependencies
### Building
- CMake 3.15 or higher
### Graphing
- Python 3
- Matplotlib
- Pandas

To install, use `pip3 install`, and add option `--user` to install without sudo (required on DAS-5).
### Hardware
- RDMA capable NIC 

## Build
1) To build this project, use `cmake .` to generate a Makefile.
2) Use `make all` to compile `kv_server` and `benchmark`. Or use `make kv_server` or `make benchmark` to compile separately.
3) The compiled binary executables should now be in `bin/`.

## Configuration
### DAS-5
To run this program on DAS-5 (see links below for more information), some configuration has to be made. First, one needs to load modules, and or correct version of modules. This can be done either through terminal, or by editing `~/.bashrc` file. Include the following lines:
```text
module load prun
module load python/3.6.0
module load cmake/3.15.4
```

Secondly, make a folder structure for benchmark data. From the root of this project, create the following folder: `benchmarking`. On DAS-5, home folder has limited space, and therefore the scratch folder has to be used, which can hold up to 50GB. This is located in `/var/scratch/<das5_username>`. Create a symbolic link: 
```text
ln -s /var/scratch/<das5_username> benchmarking/data
```
This is where the data generated by benchmarks will be saved. 

## Run benchmarks
### DAS-5 Reserving
On DAS-5, timeslots have to reserved. This can be done with: `preserve -np <number of nodes> -t <time requested>`. For this project, two nodes are used, one for server and one for benchmarks. 

After reserving, your DAS-5 username should be visible in the list: `preserve -llist` to view. If nodes are available these are visible in your row. This can now be connected to with `ssh node0#`, where `#` is the node number.

The IP address of the RNIC is `10.149.0.#`, with `#` corresponding to the node number. 

### Server
By running `./bin/kv_server`, a server is started. By default, this runs a TCP server. Server has the following options to change its behavior:
- `--rdma -r <type>` Use RDMA, requires type argument:
    - `RC`
    - `UC`
    - `UD`
- `--port -p <port>` Change the port to a given port number. Default: 35304.
- `--blocking` Use blocking design, which waits for completion event.
- `--help -h` Print help message
- `--verbose -v` Print info messages.
- `--debug -d` Print debug messages

### Benchmark
Benchmark can be run with `./bin/benchmark -a <server ip address>`. By default this runs with the following configuration:
- TCP connection
- 10 million operations
- 1 client

This can be altered with the following options:
- `-a <server ip address>` IP address of the listening server.
- `--clients -n <# clients>` Number of clients created.
- `--ops -o <# of opperations` Total number of operations executed among n-clients.
- `--rdma -r <type>` Use RDMA, requires type argument:
    - `RC`
    - `UC`
    - `UD`
- `--port -p <port>` Change the port to a given port number. Default: 35304.
- `--no-save` Do not save data.
- `--blocking` Use blocking design, which waits for completion event.
- `--help -h` Print help message
- `--verbose -v` Print info messages.
- `--debug -d` Print debug messages.

## Graphing
There are 4 graphing scripts available in `graphing/`: Average latency, latency box plot, latency CDF, and throughput. Note, when running, current directory must be in `graphing/`
- Average latency can be run as follows: `python3 latency_avg.py <max number of clients> blocking`, with `blocking` is optional, and will use data in `benchmarking/data/blocking/`. This will generate a graph in `benchmarking/graphs/Latency_avg_<max number of clients>_blocking.pdf` (or without `_blocking` if not given)
- Latency box plot can be run as follows: `python3 latency_box.py <number of clients> blocking`, where number of clients which client number shall be drawn.  This will generate a graph in `benchmarking/graphs/Latency_box_no_out_<number of clients>_blocking.pdf`
- Latency CDF plot can be run as follows: `python3 latency_cdf.py <number of clients>`, where number of clients which client number shall be drawn.  This will generate a graph in `benchmarking/graphs/Latency_cdf_<number of clients>.pdf` 
- Throughput can be run as follows: `python3 throughput.py <max number of clients> blocking`.  This will generate a graph in `benchmarking/graphs/Throughput_<max number of clients>_blocking.pdf`

## Useful links
- For more information on DAS-5 and how to sign up: https://www.cs.vu.nl/das5/
    - For a more in-depth guid to using DAS-5: https://animeshtrivedi.github.io/das-readme
- Example RDMA server-client, and SoftiWARP setup: https://github.com/animeshtrivedi/blog/blob/master/post/2019-06-26-siw.md
- RDMA blog, with useful comments and explanations: https://www.rdmamojo.com/
