# B+ Tree Index

## Theory

An index provides an alternative access path to table data.

A B+ tree maintains ordered keys and supports efficient lookup and range
traversal.

The basic structure is:

``` text
             Root
            /    \
       Internal  Internal
        /   \      /   \
      Leaf Leaf   Leaf Leaf
```

Leaf entries associate keys with references to the underlying records.

For this project, the important connection is:

``` text
B+ tree key
     ↓
RID
     ↓
Heap record
```

This keeps the index separate from the actual tuple storage.

## My implementation

The project implements a page-backed B+ tree rather than keeping the
entire tree as an ordinary in-memory C++ structure.

Index pages therefore use the same buffer manager as heap pages.

The tree contains:

-   internal nodes
-   leaf nodes
-   keys
-   leaf values / RIDs
-   leaf-to-leaf navigation

Splits create new pages and may propagate changes upward.

A root split can require creation of a new root.

## Why use the buffer pool for the tree?

The project explicitly considered the fact that index pages are database
pages too.

Therefore:

``` text
Heap page ─┐
           ├── Buffer Pool
Index page ─┘
```

The buffer manager remains generic.

During a split, multiple pages may need to be resident simultaneously:

``` text
old leaf
new leaf
parent
possibly new root
```

They must be marked dirty and unpinned correctly.

## Reasoning around node representation

One implementation challenge came from the physical node representation
and the use of unions for leaf/internal data.

This exposed an important distinction:

``` text
logical ordering of keys
```

versus

``` text
physical movement of associated values
```
Sorting keys independently is incorrect when each key is paired with a
RID or child pointer.

The key and its associated value must move as one logical entry.

This became a concrete example of why database index implementation is
not just an application of `std::sort``; the physical representation
matters.

## Clustered vs unclustered thinking

The query-processing discussions also introduced the distinction between
indexes that provide good physical locality and indexes that require
many random heap fetches.

The important project-level conclusion was not that an index is always
faster.

Instead:

``` text
available index
      ↓
possible access path
      ↓
optimizer compares alternatives
```

## Reading / coding path

**Architecture of a Database System** - §4.5 --- Access Methods - §4.3
--- Query Optimizer, for the optimizer's role in choosing access paths

**Supplementary** - Ramakrishnan & Gehrke --- query evaluation /
indexing and access paths

**Coding path**

``` text
RID
→ B+ tree page format
→ leaf lookup
→ insertion
→ split
→ parent propagation
→ root split
→ Index Scan
→ optimizer selection
```
