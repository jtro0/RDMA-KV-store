# RDMA based key-value store

## KV-store implementation
A simple KV-store implementation, consisting of a hashtable with 256 buckets. Within each bucket is a linked list. The KV-store is multithreading, each request spawns a thread, and uses a simple coarse grained locking on each bucket.

There are 9 commands:
1. `SET (key, value)`, stores value into the key corresponding slot.
2. `GET (key)`, retrieves the value in the key corresponding slot.
3. `DEL (key)`, deletes/empties the key corresponding slot.
4. `RST`, resets the table (this is mainly used for testing).
5. `PING`, test the connection.
6. `DUMP`, dumps the contents of the hashtable (again, used for testing purposes).
7. `EXIT`, stops the server.
8. `SETOPT`, sets the options for the server (only used for testing).
9. `UNK`, which is an empty request.

Requests are sent by first sending `<command> [<key>] [<payload_len>]\n` followed by `[<payload>\n]`. This implies that at first a message is sent with length of the value, which allocates enough space for the value. (***this might change***)