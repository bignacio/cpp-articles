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

