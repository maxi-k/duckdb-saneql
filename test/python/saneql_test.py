import duckdb
import os
import pytest

# Get a fresh connection to DuckDB with the saneql extension binary loaded
@pytest.fixture
def duckdb_conn():
    extension_binary = os.getenv('SANEQL_EXTENSION_BINARY_PATH')
    if (extension_binary == ''):
        raise Exception('Please make sure the `SANEQL_EXTENSION_BINARY_PATH` is set to run the python tests')
    conn = duckdb.connect('', config={'allow_unsigned_extensions': 'true'})
    conn.execute(f"load '{extension_binary}'")
    conn.execute(f"create table foo (a int)")
    conn.execute(f"insert into foo values (1), (2), (3), (42)")
    return conn

def test_saneql(duckdb_conn):
    duckdb_conn.execute("foo.filter(a > 2)");
    res = duckdb_conn.fetchall()
    assert(res[0][0] == "1");
