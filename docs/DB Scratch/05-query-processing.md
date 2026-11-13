# Query Processing: Lexer, Parser, Planner, Executor

## Theory

The database architecture paper describes the relational query processor
as the part of the system that turns a SQL request into an executable
query plan.

The high-level lifecycle is:

``` text
SQL
 ↓
Parser / query processor
 ↓
Query plan
 ↓
Executor
 ↓
Storage access
```

A physical query plan consists of operators such as:

-   selection / filtering
-   projection
-   scans
-   joins
-   aggregation
-   sorting

## My implementation: Lexer

The project started with a simple lexer for the limited SQL grammar
being implemented.

The lexer turns SQL text into tokens:

``` text
SQL string
 ↓
Lexer
 ↓
Token stream
```

The implementation initially considered whitespace splitting, but
special characters and multi-character operators made character-level
handling necessary.

Examples:

``` text
SELECT
IDENT(a)
,
IDENT(b)
FROM
IDENT(T)
WHERE
IDENT(x)
>=
NUMBER(10)
```

This was an incremental implementation rather than attempting to
implement the full SQL language.

## My implementation: Parser

The parser consumes tokens and validates the grammar.

For the initial SELECT grammar:

``` text
SELECT column_list
FROM table
[WHERE predicate]
```

The parser produces an AST.

Conceptually:

``` text
Query
 ├── projection
 ├── table
 └── predicate
```

The important boundary is:

``` text
Lexer → tokens
Parser → AST
```

The parser does not execute the query.

## Planner

The planner consumes the AST and turns it into an execution plan.

The project evolved toward:

``` text
SQL
 ↓
Lexer
 ↓
Parser
 ↓
AST
 ↓
Planner
 ↓
Physical operator tree
 ↓
Executor
```

The planner is also the place where catalog metadata can be resolved and
where physical access choices can eventually be made.

## Executor

The project uses an iterator-style execution model:

``` text
open()
next()
close()
```

Operators pull rows from their children.

For a simple SELECT:

``` text
Projection
    ↓
Filter
    ↓
SeqScan / IndexScan
```

The executor walks this tree and produces tuples.

## Heap Scan vs Index Scan

A major reasoning point was recognizing that choosing between heap scan
and index scan belongs to **plan selection / optimization**.

An index should not automatically be used merely because one exists.

The optimizer can eventually compare:

``` text
SeqScan
```

against:

``` text
IndexScan
```

using estimated cost/selectivity.

For the early project, a simpler rule is sufficient:

``` text
if suitable index exists
    → possible IndexScan
else
    → SeqScan
```

A full cost model can be added later.

## Reasoning that led here

The query processor was built after storage and access methods were
sufficiently usable.

The project deliberately avoided trying to implement the entire SQL
language.

Instead, the implementation progressed through:

``` text
basic SELECT grammar
→ AST
→ simple plan
→ executor
→ INSERT
→ more access-path choices
```

This made it possible to test the entire pipeline before adding
optimizer complexity.

## Reading / coding path

**Architecture of a Database System** - §1.1 --- Relational Systems: The
Life of a Query - §4 --- Relational Query Processor - §4.3 --- Query
Optimizer - §4.5 --- Access Methods

**Coding path**

``` text
Lexer
→ Parser
→ AST
→ Plan
→ Operator interface
→ SeqScan
→ Filter
→ Projection
→ IndexScan
→ optimizer
```
