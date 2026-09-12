ACID property stands for Atomicity, Consistency, Isolation, Durability.

Everything here tries to ensure above in a database.

**Transaction** : Multiple read, write, other operations can be considered as one *transaction*, this ensures **atomicity**. 

**Serializability** : If transactions are taking place concurrently, their internal operation can interleave and so can this produce same output as if both transaction had run one-after-other, if yes then it is Serializable, this ensures **consistency**.

Serializability can be implemented via *2PL, MVCC, OCC*:

- 2PL : Uses locks over data structures to commit transactions and releases atomically.
- MVCC : Does not use locks, but use state of database to ensure consistency.
- OCC : Does not use locks or states, ensures safety by rolling back operations causing harm.

##### Locking and Latching

Every lock is associated with a transaction.

Every lock comes in a different modes.

Hierarchical locking allows single lock to be used on table as well as on each row.

Lock Manager supports 2 calls:
- lock (lockname, id, mode)
- remove_transaction (id)
- (optional) upgrade lock

There is no need for different unlock, remove transaction does that, but sql allows for lower degree of transaction so it might be needed.

There is also lock_upgrade to upgrade lock without re-locking and condtion_lock telling if lock was acquired, if not then calling thread in not queued.

To support above calls lock manager maintains 2 DS:

- Global Lock Table : Handles locks on rows, etc.
- Transaction Table : 
- Deadlock detector :  Deadlocks can occur when two threads tries to acquire each other's resource, this is periodically called on main thread to check for such condition.

### WAL

A commit is considered when log records are flushed to disk before dirty page can be flushed to disk.

Practically only those changes will be logged which are actually changing database unlike SELECT.

As WAL is a append only log, there is no need of paging for this, just a method to append to WAL.

What a LOG will be : 
- page id, page offset.
- operation performed.

DB currently flushes only on eviction and shutdown, so WAL will also perform flush now after it has written to wal file.

As DB is threaded, shared access can be allowed as no 2 rows can be accessed by *2 threads* at same time (lock manager), similar pattern can be followed where necessary.

Lock Manger starts a transaction, so it should be the one to also log changes via WAL when transaction compelete.

