# Buffer Manager

## Theory

The buffer manager manages the movement of database pages between
persistent storage and memory.

Its responsibilities include:

-   locating pages already resident in memory
-   loading pages on misses
-   tracking pinning
-   tracking dirty state
-   deciding when pages can be evicted
-   writing dirty pages back when required

The key abstraction is:

``` text
Disk
  ↕
Buffer Pool
  ↕
Database components
```

A page table maintained by the buffer manager maps:

``` text
page_id → frame
```

It is an in-memory structure, not part of the persistent database.

## My implementation

The project uses a buffer pool containing frames. A frame contains page
data plus metadata such as:

-   page ID
-   dirty bit
-   pin count

The buffer pool also maintains an in-memory hash table for locating
resident pages.

The basic lookup is therefore:

``` text
fetch(page_id)
    ↓
page table lookup
    ├── hit  → existing frame
    └── miss → find frame → load page
```

The replacement policy implemented initially was FIFO.

Pinned pages cannot be evicted. Dirty pages must be written back before
eviction.

## Reasoning that led here

The important architectural decision was to keep the buffer manager
**generic**.

It should not know about:

-   SQL
-   tuples
-   SARGs
-   indexes
-   query predicates

Those belong to higher layers.

The buffer manager only understands pages and their lifecycle.

This became particularly important once B+ tree index pages also needed
buffering. The same buffer pool can manage heap pages and index pages
instead of creating a separate in-memory tree.

## Pinning and dirty state

Pinning expresses that a component is currently using a page.

The conceptual relationship is:

``` text
pin_count > 0
    → page cannot be evicted
```

Dirty state expresses:

``` text
memory contents != persistent contents
```

These are separate concepts.

A page can be:

-   pinned and cleanb
-   pinned and dirty
-   unpinned and clean
-   unpinned and dirty

## Important design lesson

Buffer management and transaction correctness are related, but they are not the same responsibility.

The buffer manager controls physical page residency and flushing.

WAL/recovery later determines whether those flushes are safe from a
transactional correctness perspective.

## Reading / coding path

**Architecture of a Database System** - §5 --- Storage and Buffer
Management

**Coding path**

``` text
Page
→ Frame
→ map page table
→ fetch/unpin
→ pin count
→ dirty tracking
→ replacement
→ flushing
```

This component should remain below access methods and query execution.
