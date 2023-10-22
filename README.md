# CRamDB

Educational project for creating concurrent and transactional in-memory data structures.

## Roadmap

- [x] Sorted list.
- [ ] Hash table.
- [ ] Binary tree.
- [ ] B+ tree.

### Sorted list

- [x] Insertion.
- [x] Deletion.

## Transactions
### Snapshot isolation
#### Serializeability (S)

Everything should look like executed serially, no requirements regarding the ordering of transactions.

#### Strict serializeability (Z)

If transaction A committed before transaction B started, their results should be as if they were executed in A - B order. No requirements regarding concurrent transactions, they can complete in any order.

#### External serializeability (E)

If transaction A commits before transaction B commits, their results should be as if they were executed in A - B order. For example, if B reads the tuple later overwritten by A, if A commits first, B should be rolled back.

### Management system

- Algorithm: the statements implementing the management system.
- Consequences: the statements that are true for the database working under the management system.
- Characteristics:
  - Snapshot isolation: the guarantees the management system provides.
    - S - serializeability.
    - Z - strict serializeability.
    - E - external serializeability.
  - Memory overhead: the minimal additional memory required fot the system to function.
  - Concurrency: the amount of concurrent transactions possible with this system.
    - N - unlimited value.
    - K - the size of the set of keys used by transactions.
    - W - writing transactions.
    - RO - read-only transactions.
    - WO - write-only transactions.
    - RW - read-write transactions.
    - ^ - either left or right.

#### Optimistic Stupid (Os)

Algorithm:
1. There's 'used' mark for each tuple, which is set when the tuple is read or modified and reset on transaction commit.
2. If a transaction attempted to iteract with a marked tuple in any way - it's aborted.

Consequences:
1. Only non-overlapping concurrent transactions can succeed, once a transaction overlaps with another one - it's aborted.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: 1 bit.
- Concurrency: K.

#### Optimistic (O)

Algorithm:
1. There're 'read' and 'written' marks wrich are set on corresponding events and reset on transaction commits.
2. If a transaction attempted to read a key someone else had written - abort it.
3. If a transaction attempted to write a key someone else had read - abort it.
4. If a transaction attempted to write a key someone else had written - abort it.

Consequences:
1. Writing transactions can't overlap with any other transaction.
2. Reading transactions can freely overlap.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: 2 bits.
- Concurrency: (N * RO) ^ (K * WO) ^ (???)

#### Locking Stupid (Ls)

Algorithm:
1. Each transaction waits for previous one to complete.

Consequences:
1. The database is fully serialized.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: none.
- Concurrency: 1.

#### Locking (L)

Algorithm:
1. Each writing transaction wait for previous read transactions or write transaction to complete.
2. Each reading transaction waits for previous write transaction to complete.

Consequences:
1. Writes are serialized against writes and reads.
2. Reads are serialized against writes.
3. Unlimited amount of reads can execute in parallel.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: none.
- Concurrency: (1 * W) ^ (N * RO).

#### Optimistic Locking Stupid (Ols)

Algorithm:
1. There're 'read' and 'written' marks wrich are set on corresponding events and reset on transaction commits.
2. If a transaction attempted to read a key someone else had written - roll both back, reorder and re-execute them in order of coming.
3. If a transaction attempted to write a key someone else had read - roll both back, reorder and re-execute them in order of coming.
4. If a transaction attempted to write a key someone else had written - toll both back, reorder and re-execute them in order of coming.

Consequences:
1. Non-conflicting transactions run concurrently.
2. Conflicted transactions are rolled back and serialized in a defined order.
3. Additional overhead of rolling back and re-executing is introduced, which can cause the conflict stacking.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: ???
- Concurrency: ???

#### Optimistic Locking (Ol)

Algorithm:
1. There're 'read' and 'written' marks wrich are set on corresponding events and reset on transaction commits.
2. If a transaction attempted to read a key someone else had written - roll back the newer one, make the older one wait for the rollback and continue, make the newer one wait for completion of the older one and re-execute.
3. If a transaction attempted to write a key someone else had read - roll back the newer one, make the older one wait for the rollback and continue, make the newer one wait for completion of the older one and re-execute.
4. If a transaction attempted to write a key someone else had written - roll back the newer one, make the older one wait for the rollback and continue, make the newer one wait for completion of the older one and re-execute.

Consequences:
1. Non-conflicting transactions run concurrently.
2. Conflicted transactions are rolled back when it's required and serialized in a defined order.
3. Additional overhead of rolling back and re-executing is introduced, which can cause the conflict stacking.

Characteristics:
- Snapshot isolation: E.
- Memory overhead: ???
- Concurrency: ???

#### Straightforward (S)

Algorithm:
1. If a transaction creates a tuple it sets its creation TX ID.
2. If a transaction removes a tuple it sets its deletion TX ID.
3. If a transaction accesses a tuple it checks if the tuple is visible for it.
4. A transaction only sees the results of transactions committed before it started.
5. If a transaction writes the tuple one of active transactions had read, one of them is rolled back.

Consequences:
1. Huge memory consumption.
2. Long transactions increase the memory consumption even more.

Characteristics:
- Snapshot isolation: ???
- Memory overhead: ???
- Concurrency: ???

Examples:
- PostgreSQL

#### Head-Jumping (Hj)

Examples:
- Tarantool

<!--

##### Conflict management

If a TX is distributed then the fact it's ended should be informed (and accepted) to each server.

###### ???

TX only conflicts if it writes the tuple someone else had read.

###### Serialized

Each next transaction waits for the previous one transaction to complete.

Actually one doesn't have to wait for each of previous ones, he can simply wait for writing ones. But then read-only transactions should only see the data

##### Fixed LSN

Once a transaction comes it fixes an LSN for itself. And works with the freezed state of the database. If it wants to insert - it looks if noone changed the key with higher LSN, and desides to keep or abort. BUT! Ok, he read/wrote stuff, he desided to finish. But after it has read TX2 wrote the data he watched and commited. What now? From the TX1 perspective everything is fine: it sees some LSN. TX2 had seen the same LSN and wrote the key. Well the TX1 could finish before TX2 with the same result, couldn't it? It would be a problem if TX2 also read the item the TX1 wrote, but he won't, because it's out of his LSN. But this feels buggy anyways. And it is: A = 0, B = 0; TX1: get A, set B = 1; TX2: set A = 2, get B; If both commit (like simultaneously), we have: TX1 -> A = 0, TX2 -> B = 0. If we commit them serially: TX1 -> A = 0, TX2 -> B = 2 or TX1 -> A = 1, TX2 -> B = 0. The idea of MVCC is to detect when transaction results depend on their order. This is the case. How the transaction isolation works in MySQL?

Adding a TX ID to each inserted item. Requests can't see the items with TX ID less than the first one which is less than its TX ID (TX5 can see TX4, but can't see TX3). Also they can't see items with TX ID greater than ID of themselves.

The transactions can be finished and unfinished. Once can only see the finished (commited) transactions. What if TX2 reads the data inserted by TX1, but TX1 hadn't been commited yet? Should he wait for it to be committed or he can just ignore the transaction until it's done? If he waits we have a sequential consistency. If we don't - then we have to say that the TX2 had been before the TX1 and so it can't read its changes. Like the TX ID is 2, but it only sees TX0's data, so its sequential ID is greater than 0 and less than 1. Maybe we should only assign the IDs to modifying transactions? Read-only transactions can assign themselves to any ID they want to (?).

Detecting and resolving TX conflicts is easy: if we attempted to write the key had been written by someone newer - we are aborted.

The caveat of the approach is potentially increased memory overhead: while a transaction is reading data, other transactions are writing the same keys are increasing memory consumption because of maintaining read views.

Deletion of the non-required items can be done by the last transactions that can read the items. When a TX writes a new version of the item, it looks at the old version and detects the last transaction that can see the item further and links the item to the transaction. Once the TX finished, it looks at the list of stuff it should delete and performs the deletion.

##### Tarantool approach

A TX has no ID in the beginning (it always reads the newest data), but once someone commits the write by the key the TX had read, it goes to the read view or aborts if it had writes.

Detecting and resolving TX conflicts becomes harder. We have to detect if someone has written the tuple soneone had read and abort it if he had writes. And do it in a thread-safe manner. Challenging, if possible.

But as the upside we have lesser memory consumption: no reason to create a read view for some transactions.

-->
