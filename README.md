# DuckDB SaneQL Plugin

<!--  [![Build](https://github.com/maxi-k/duckdb-saneql/actions/workflows/linux.yml/badge.svg)](https://github.com/maxi-k/duckdb-saneql/actions/workflows/linux.yml) -->

Allows you to type [saneql](https://github.com/neumannt/saneql) directly into a [duckdb](https://github.com/duckdb/duckdb) shell.

## Building

### Build steps
Now to build the extension, run:
```sh
make
```
alternatively, pass extra cmake flags like this:

``` sh
CMAKE_FLAGS="-DCMAKE_CXX_COMPILER=g++" make
```

The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/saneql/saneql.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded.
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `saneql.duckdb_extension` is the loadable binary as it would be distributed.

## Running the extension
To run the extension code, simply start the shell with `./build/release/duckdb`.

Now we can use saneql directly in DuckDB: 
```
D create table foo(a int);              -- regular sql
D insert into foo values (1), (2), (3); -- regular sql
D foo.filter(a >= 2)                    -- saneql
┌───────┐
│   a   │
│ int32 │
├───────┤
│     2 │
│     3 │
└───────┘
```

For a full list of saneql operations, see [the saneql repo](https://github.com/neumannt/saneql/blob/main/algebra.md),
or checkout the saneql [TPC-H queries](https://github.com/neumannt/saneql/tree/main/examples/tpch).

## Running the tests
Different tests can be created for DuckDB extensions. The primary way of testing DuckDB extensions should be the SQL tests in `./test/sql`. These SQL tests can be run using:
```sh
make test
```

### Loading in an existing duckdb binary
To install load the extension into a duckdb instance that doesn't have it, you will need to do two things. 
Firstly, DuckDB should be launched with the `allow_unsigned_extensions` option set to true. 
How to set this will depend on the client you're using. Some examples:

CLI:
```shell
duckdb -unsigned
```

Python:
```python
con = duckdb.connect(':memory:', config={'allow_unsigned_extensions' : 'true'})
```

NodeJS:
```js
db = new duckdb.Database(':memory:', {"allow_unsigned_extensions": "true"});
```

After running these steps, you can install and load your extension using the regular LOAD command in DuckDB:
```sql
LOAD '/path/to/saneql.duckdb_extension'
```

### Always load on the command line
To always load this plugin into a duckdb instance other than the one built by this repo, create a shell alias:
``` sh
alias duckdb='duckdb -unsigned -cmd "LOAD '\''/path/to/saneql.duckdb_extension\''"'
```

## Links

You should also check out [PRQL](https://prql-lang.org/) as an SQL alternative.
Thanks to the [duckdb prql](https://github.com/ywelsch/duckdb-prql) repo, which served as an example of how to put another language into duckdb.
