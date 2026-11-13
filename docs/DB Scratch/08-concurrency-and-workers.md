# Concurrency and Worker Synchronization

## Theory

A multi-user database cannot assume that only one operation is executing
at a time.

Concurrency introduces several distinct synchronization problems.

-   protecting shared execution state
-   coordinating worker threads
-   protecting database pages
-   locking logical database objects
-   maintaining transaction isolation

These should not be collapsed into one generic mutex.

## My implementation reasoning

The project has a worker abstraction that needs synchronization with the
main thread.

A worker mutex protects the worker's own state.

A condition-variable wait uses a lock that is released while the worker
sleeps and reacquired when it wakes.

The important distinction established during implementation was:

``` text
worker mutex
    ≠
condition-variable waiting mechanism
    ≠
database page lock
    ≠
transaction lock
```

Each protects a different invariant.

## Page locking problem

The project encountered a physical representation constraint: page structures were being accessed through reinterpret-casting, leaving no convenient place to embed a mutex directly into the page representation.

This led to consideration of external synchronization around page access.

The important design question is not simply:

> "Where can I put a mutex?"

but:

> "What shared state needs to be protected, and for how long?"

Holding a global buffer-pool lock for an entire page operation can unnecessarily serialize unrelated work.

## Reasoning that led here

Concurrency was not added as an isolated feature.

It emerged from the need to make the existing components safe when multiple workers interact with them.

The project therefore has to separate:

``` text
thread coordination
```

from:

``` text
database correctness
```

and eventually from:

``` text
transaction isolation
```

## Reading / coding path

**Architecture of a Database System** - §2 --- Process Models - §6 ---
Transactional consistency architecture

**Coding path**

``` text
Worker lifecycle
→ worker mutex
→ condition variables
→ concurrent buffer access
→ page/latch protection
→ transaction locks
→ isolation / serializability
→ deadlock handling
```
