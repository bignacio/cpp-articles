# [DRAFT] Where did my assembly code go?

In my last post about [pipelined processors and instructions](instructions-01.md) I briefly talked about processor ports and how they affect instruction execution.

Let's talk some more about the pipeline and its components. I'll focus on relatively recent Intel architectures, namely Haswell and up - where some key architecture changes were introduced.

## Processor architecture

Generally speaking, these processor architectures can be divided into frontend and backend. 

When a program (*i.e* instructions and data) is ready to execute, the instructions make their way into the L1 instruction cache, located in the frontend.

The data is loaded into the L1 data cache, in its turn located in the processor backend.

From there, a lot of complicated and cool things start to happen.

### The processor frontend

A number of complicated things happen on the frontend of the pipeline so I'll summarize the most significant and relevant ones in the context of this post.


Here is where the instruction fetching and all the complex decoding happens. On most, if not all modern Intel processor, instructions are fetched at a rate of 16 bytes per clock cycle and put into an the instruction queue. 

This is also where instruction [macro and micro fusion](instructions-01.md) happen; where the branch predictor, the stack engine, the loop stream detector (LSD) and the decoded stream buffer (DSB) live.

Quickly trying to explain some of these things, the LSD helps the execution of instructions in a code loop while the DSB contains already decoded micro operations. 

The branch predictor is also an awesome optimization mechanism optimizes execution by, as the name says, predicting which branch the code execution will take.
There are a number of things us developers can do to help the branch predictor and I promise I'll write a full post about it one day.

At the end of all that is the decoded instruction queue or allocation queue, where all the micro ops are stored and then fed to the backend.

### The processor backend

We can look the backend as two sets of components: the **execution engine** and the **execution units**.

The execution engine contains the register renaming circuitry, the ordering buffers and the instruction scheduler.

It turns out the registers we are used and have access to are not the only ones the processor can use. Internally, more registers can be allocated to accommodate data and instruction execution. 

Together with the ordering buffers, micro operations can be executed more independently and in an order that can make use of the processor resources more efficiently. We'll talk more about these benefits in the sections below.

Finally, from renaming to re-ordering, the instructions in form of micro operations are scheduled for execution and sent to the execution units appropriately. After the operations are executed, they are retired and removed from execution.

The remaining of the hard work happens in the execution units. 

Here's where all the addressing, arithmetic, vector operations, memory read/write and a lot more happen.
Each processor core has a finite number of dedicated execution units, solely responsible for one particular task.

For example, the ALU (Arithmetic Logical Unit) as the name says, is responsible for all logical operations as well as summation and subtraction. The ALU is usually the most frequent unit in a processor core.

To illustrate a bit more, the Skylake architecture contains 4 ALUs, 1 integer multiplication unit and one integer division unit.

There are also unit specialized for floating point and vector operations.

The image below depicts the backend and frontend parts of a *generic* architecture and while it should not be taken as the norm for all modern Intel processor, it does provide a better understanding of all the concepts we've seen so far.

(from [Intel® VTune™ Amplifier 2019](https://software.intel.com/en-us/download/intel-vtune-amplifier-2019u2-help) documentation)
![macrobench](images/processor-ports-01-processor.png)


## Out of order execution

We saw that on the backend, with all the micro operations in hand, the processor scheduler starts assigning operations to the processing units.

As we also saw, there is a limited number of processing units per function, which means a processor with only one vector multiplication unit cannot schedule 2 concurrent vector multiplications at the same time.

But what if it could execute another operation in a different processing unit while it waits for the vector multiplication unit to be available?

That's where the out of order execution technique comes in. Scheduled operations can be execute not in the order in which they were programmed (written by a person or compiler) but instead, in the order that suits better the data that needs to be processed.

IF the scheduler sees operations in the queue that have no data dependency and that can be executed in different units, it can schedule the operations for execution. 

Given the fact that instructions (and consequently micro ops) have different latency (number of clock cycles required to execute), one interesting effect of that is that operations there were further down in the queue can finish before the first scheduled operations. 

After executed, the operations are then reordered in the ordering buffers and then can be retired - committed or discarded. Operations (instructions really) are discarded if the speculative execution turned out to be wrong. Otherwise, the result of the operation is considered valid and committed.

Speculative execution is part of the branch prediction functionality I mentioned before and sufficiently interesting to be a whole new post.

