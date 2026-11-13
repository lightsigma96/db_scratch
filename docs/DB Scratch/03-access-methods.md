# Access Methods: Heap Storage, Heap Scan, SARGs, and RIDs

## Theory

The database architecture paper describes access methods as the
algorithms and data structures used to organize and access data on disk.

They include basic structures such as:

-   tables / heap storage
-   indexes

The access method layer sits above the buffer manager:

``` text
Executor
   ↓
Access Method
   ↓
Buffer Manager
   ↓
Disk
```

## Heap storage

A heap is a general-purpose organization in which records are stored
without requiring an ordering by a search key.

A heap page can use a slot directory to locate individual records.

A record can then be referenced by a physical identifier such as:

``` text
RID = page + slot
```

The RID becomes important because indexes need a way to point back to
the actual row.

## Heap scan

A heap scan walks the heap storage and examines records.

Conceptually:

``` text
page
 ↓
slot directory
 ↓
record
 ↓
predicate
 ↓
match / reject
```

A heap scan is therefore the straightforward fallback access path when
no more selective access path is available.

## SARG

A SARG (Search ARGument) represents a simple search condition used by an
access method.

The project used the model:

``` text
column + operator + constant
```

For example:

``` sql
age = 20
```

The SARG lets the access method evaluate the predicate while retrieving
rows rather than requiring every row to be copied upward first.

## My implementation

The project moved from page-level storage toward:

``` text
Heap Page
→ Heap File
→ Heap Scan
→ SARG evaluation
→ RID
```

The heap scan uses the slot directory rather than blindly walking raw
page memory.

This distinction became important because treating the page as a flat
array of rows caused incorrect access to physical records.

## Schema interaction

A major design question was where schema information should be obtained.

The chosen direction is:

``` text
Planner
    ↓
Catalog lookup
    ↓
Resolved schema / physical information
    ↓
Plan
    ↓
Executor
    ↓
Heap Scan
```

The heap scan should not repeatedly query the catalog.

Similarly, SARGs can carry resolved information such as:

``` text
column offset
type
operator
constant
```

so lower layers do not need to perform metadata lookups.

## Reasoning that led here

The project initially treated access methods mainly as mechanisms for
finding rows.

The conversations gradually made the boundary clearer:

-   buffer manager handles pages
-   access methods understand rows and indexes
-   planner decides which access path to use
-   executor runs the chosen operator

This separation is one of the central architectural decisions of the project.

## Reading / coding path

**Architecture of a Database System** - §1.1 --- query lifecycle and
storage manager context - §4.5 --- Access Methods

**Coding path**

``` text
Slotted Page
→ Heap File
→ RID
→ Heap Scan
→ SARG
→ Index Access
```
