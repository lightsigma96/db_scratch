~- transactions currently for SELECT and INSERT only.~

~- make db changing functions return something to let know that transaction completed or not (Added for insert look out for protobuf filling)~

~- transaction starts before DB_Pipeline and ends right after it, transaction is only made up of transaction_id for now, propagate transaction_id down and then associate it.~

- add undo and redo.
