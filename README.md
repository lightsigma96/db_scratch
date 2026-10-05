A single-threaded relational database written in C++ to explore database internals from storage to query execution.

Database Implements:

- The database implements buffer pool manager for maintaining pages from db file into memory.
- It also supports heap scan and index scan (B+ Tree).
- It has lexer and parser for parsing SQL queries and checking grammar.
- A query planner creating a plan for execution and executor executing the plan in a volcano iterator style.

It supports basic operations such as:
- Creating schemas
- Creating tables
- Inserting rows

Supported data types:
- INT
- STRING
- FLOAT

The next goal is to support more SQL quires (like `JOINS` etc) and make this durable and robust

---

## Build and Run (Local)

Build the project using CMake:

    cmake -S . -B build
    cmake --build build

Run the database:

    ./build/db_scratch

---

## Using Docker

A Dockerfile is provided to build and run the project inside a container.

### Build the image

    docker build -t db_scratch .

### Run the container (interactive)

    docker run -it db_scratch

Inside the container:

    ./build/db_scratch

---

## Notes

- The Docker image includes build tools and is intended for both development and debugging.
- If you modify the source code, rebuild the image:

    docker build -t db_scratch .
