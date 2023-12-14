var duckdb = require('../../duckdb/tools/nodejs');
var assert = require('assert');

describe(`saneql extension`, () => {
    let db;
    let conn;
    before((done) => {
        db = new duckdb.Database(':memory:', {"allow_unsigned_extensions":"true"});
        conn = new duckdb.Connection(db);
        conn.exec(`LOAD '${process.env.SANEQL_EXTENSION_BINARY_PATH}';`, function (err) {
            if (err) throw err;
            done();
        });
        // create table foo (a int); and populate it with some data
        conn.exec(`create table foo (a int);`, function (err) {
            if (err) throw err;
            done();
        });

        conn.exec(`insert into foo values (1), (2), (3);`, function (err) {
            if (err) throw err;
            done();
        });
    });

    it('saneql function should return expected string', function (done) {
        db.all("foo.filter(a = 1)", function (err, res) {
            if (err) throw err;
            assert.deepEqual(res, [{value: 1}]);
            done();
        });
    });
});
