# Directory-Based Cache Coherence Protocol Simulation

In this assignment, you will implement a simple cache simulator. It will consist of nodes, where one node contains the following:
- Processor
- Local L1 cache
- Chunk of memory
- Directory

You will have to parallelize it using OpenMP to simulate multiple nodes. In other words, you will convert the single core cache simulator into a multi-core cache simulator. 
In order to ensure cache coherence between these nodes, you'll have to implement a cache coherence protocol. But instead of a snooping-based protocol, you'll implement a directory-based protocol.

This assignment will be evaluated manually. Any loss of marks will be explained.

## Instruction Types

You will be dealing with two types of instructions in the input files: `RD <address>` and `WR <address> <value>`. More on them under the section "Input Format".

## Input Format

The simulator will be fed input files with a name of the format `core_n.txt`, where `n` represents the core/node number. So `core_0.txt` will represent the input to core 0, for example. 
`n + 1` will be the total number of threads we support. Each of the files will be read by individual threads which will run the instructions.

Consider the following sample content of `core_0.txt`:

```
WR 0x15 100
RD 0x17
```

The first instruction indicates that core 0 is trying to write the value 100 to memory location `0x15`. The second instruction indicates that core 0 is trying to read the value stored at memory location `0x17`. 

Note that, while the memory is distributed across nodes, the memory regions combined act as a global shared memory space. Meaning, core 0 can read content from the memory region in node 2, for example. That being said, each node has its own local cache (which is why we need a cache coherence protocol here in the first place).

## Output Format

