fluxcap cc buffer library

Originally part of [fluxcap](https://github.com/troydhanson/fluxcap).

High level concept

SHR: shared ring (dependency)

SHR is a bounded queue implemented in the form a ring buffer. The items
in the queue are opaque, arbitrary binary data. SHR provides read/write
and data-availability notification via descriptor readiness for programs
that produce or consume buffers through the ring.

CC: capture/convert

CC is a basic buffer mechanism to capture C variables to a flat buffer, and
optionally subsequently restore them or convert to JSON. It only produces a 
buffer, leaving the program to choose what to do with it e.g. transmit, etc.

CCR: capture/convert+ring

CCR is the combination of CC for capturing data from a C program and SHR
for writing the flattened buffer to a ring. Whereas SHR is a ring of opaque
items, CCR is a ring of items having a known binary format and named fields.
