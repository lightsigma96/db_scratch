as each wal record has rid (which contains pid) that page can be fetched, and then it's LSN can be checked, if less than what is tracked by wal on records for that particular operation was not written before the crash even though the page might have been. 

Hence the operation for this page should be redone.

This also means after every operation on a row in a page that page's LSN should equal to WAL's LSN, sort of shows the recency of page to WAL.

page lsn > wal lsn is violation as that means transaction was never commited.

page lsn < wal lsn means there

---

- Add a lsn state along with pageid and all