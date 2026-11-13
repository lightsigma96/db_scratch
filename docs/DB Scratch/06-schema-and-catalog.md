# Schema and Catalog

## Theory

A database needs metadata describing its stored objects.

The catalog provides information such as:

-   tables
-   columns
-   types
-   physical storage information
-   schema definitions

The catalog is different from the execution operators that consume this
metadata.

A useful architectural boundary is:

``` text
Planner
   ↓
Catalog
   ↓
resolved metadata
   ↓
Execution plan
   ↓
Executor / Access Method
```

## My implementation

The project introduced a schema/catalog layer so query processing could
work with dynamically defined tables rather than hard-coded layouts.

The schema became responsible for physical information needed to
interpret rows.

This matters because a heap scan needs to know how to interpret bytes in
a record.

## Serialization problem

One concrete implementation issue was serializing C++ strings into
metadata files.

A `std::string` contains internal pointer-based state and cannot simply
be dumped as a durable binary object.

The project therefore moved toward explicit serialization such as:

``` text
[string size][string bytes]
```

and reconstructing the string during deserialization.

This was another case where the C++ object representation could not
simply be treated as the database's on-disk representation.

## Planner vs lower layers

A key architectural discussion concerned whether heap scans should query
the catalog directly.

The chosen model is:

``` text
Catalog lookup
      ↓
Planner
      ↓
Resolved schema
      ↓
Plan
      ↓
Executor
      ↓
Heap Scan
```

The heap scan consumes the resolved schema rather than owning catalog
access.

For SARGs, the same idea allows the planner to resolve a column into
physical information such as:

``` text
column offset
column type
operator
constant
```

## Reasoning that led here

The catalog initially looked like a simple metadata store.

Its importance became clearer once rows became dynamically structured.

Without a resolved schema, lower-level components would need to
repeatedly answer questions such as:

``` text
Where is column X?
What type is it?
How many bytes does it occupy?
```

Pushing this resolution upward keeps the execution path simpler and more
deterministic.

## Reading / coding path

**Architecture of a Database System** - §4 --- Relational Query
Processor, especially the query compilation/planning boundary - §5 ---
Storage architecture for the connection between logical metadata and
physical storage

**Coding path**

``` text
Schema representation
→ Catalog
→ serialization/deserialization
→ planner lookup
→ resolved schema in plan
→ executor/access methods consume it
```