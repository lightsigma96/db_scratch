# DB From Scratch --- Project Documentation

This documentation is intentionally not written as a generic database
tutorial.

Each topic has two layers:

1.  **Theory** --- the database-system concept and the role it plays.
2.  **My implementation and reasoning** --- how the concept became a
    concrete part of this C++ database, including design decisions,
    mistakes, and changes in thinking captured during the project
    conversations.

The goal is to preserve not only what the system does, but **why it was
built this way**.

## Project architecture

The implementation evolved roughly as:

``` text
SQL
 ↓
Lexer
 ↓
Parser
 ↓
AST
 ↓
Planner / Optimizer
 ↓
Execution Operators
 ↓
Access Methods
 ↓
Buffer Manager
 ↓
Disk / Storage
```

The catalog supplies metadata to the planning/execution path.

Later systems add:

``` text
Transactions
WAL / Recovery
Locking / Concurrency
```

## Primary references

-   *Architecture of a Database System* --- primary architectural
    reference.
-   Ramakrishnan & Gehrke, *Database Management Systems* ---
    supplementary reference, especially query processing and
    optimization.

## Documentation rule

For every subsystem, the implementation discussion should answer:

-   What problem does this component solve?
-   What does the theory say?
-   Where does it live in my architecture?
-   What design did I choose?
-   Why did I choose it?
-   What alternatives did I consider?
-   What bugs or misunderstanding changed the design?
-   What is still missing?

This keeps the documentation as a record of the project rather than a
generic explanation of databases.
