# Storage, Pages, and Disk Management

## Theory

A disk-oriented database does not normally treat a database file as one
giant object. Data is organized into fixed-size pages that can be
transferred between persistent storage and memory.

In this project, the page size is **4 KB**.

The storage layer therefore provides the physical foundation for higher
layers:

``` text
Disk files
   ↕
Pages
   ↕
Buffer Manager
```

The important separation is that the storage layer deals with physical
persistence, while the buffer manager decides which pages are resident
in memory.

### Page organization

A page is a fixed-size region of storage. Higher-level structures can
impose their own layout inside that region.

For heap data, this led to the slotted-page model:

``` text
+----------------------+
| Page Header          |
+----------------------+
| Slot Directory       |
|                      |
+-----------------------+
| Free Space             |
|                      |
+-----------------------+
| Record Data          |
|                      |
+-------------------------+
```

The slot directory provides stable references to records without
requiring the record itself to stay at a fixed byte offset after every
modification.

## My implementation

The project uses 4 KB pages and a disk manager to read and write those
pages.

The buffer manager sits above this layer rather than allowing query
operators to perform abitrary disk I/O.

The page layout became important when implementing heap storage because
rows cannot simply be treated as a sequence of C++ objects in memory.

As recurring implementation problem was therefore:

``` text
logical row
    ↓
physical bytes inside a page
```

rather than assuming the C++ representation itself is a valid on-disk
representation.

## Reasoning that led here

The project started from the idea that the database needs a concrete
storage unit before higher-level structures can be implemented.

This led to the progression:

``` text
disk manager
→ fixed-size pages
→ page layout
→ heap storage
→ buffer management
→ access methods
```

One important lesson was that **memory layout and persistent layout are
different problems**. C++ structs, padding, pointers, and object
representations cannot automatically be treated as durable database
formats.

## Implementation lessons

The project repeatedly exposed bugs involving:

-   offsets
-   slot positions
-   pointer arithmetic
-   struct layout
-   serialization
-   page boundaries

These were not incidental details. They became part of understanding how
a DBMS actually represents data.

## Reading / coding path

**Architecture of a Database System** - §5 --- Storage and Buffer
Management - Earlier architectural context: §1.1 --- Relational Systems:
The Life of a Query

**Coding path**

``` text
Disk Manager
→ Page abstraction
→ Page layout
→ Heap/slotted page
→ Buffer Manager
→ Access Methods
```
