from MonetDBtesting.sqltest import SQLTestCase

with SQLTestCase() as mdb1:
    with SQLTestCase() as mdb2:
        mdb1.connect(username="monetdb", password="monetdb")
        mdb2.connect(username="monetdb", password="monetdb")

        mdb1.execute("create table myt (i int, j int);").assertSucceeded()
        mdb1.execute("insert into myt values (1, 1), (2, 2)").assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("alter table myt add constraint pk1 primary key (i);").assertSucceeded()
        mdb2.execute("alter table myt add constraint pk2 primary key (j);").assertFailed(err_code="42000", err_message="NOT NULL CONSTRAINT: transaction conflict detected") # only one pk per table
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('rollback;').assertSucceeded()

        mdb1.execute('CREATE schema mys;').assertSucceeded()
        mdb1.execute("CREATE ROLE myrole;").assertSucceeded()
        mdb1.execute("CREATE USER duser WITH PASSWORD 'ups' NAME 'ups' SCHEMA mys;").assertSucceeded()
        mdb1.execute("GRANT myrole to duser;").assertSucceeded()
        mdb1.execute("create table mys.myt2 (i int, j int);").assertSucceeded()

        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("GRANT SELECT on table mys.myt2 to myrole;").assertSucceeded()
        mdb2.execute('drop role myrole;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("analyze sys.myt").assertSucceeded()
        mdb2.execute('drop table myt;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute('comment on table "sys"."myt" is \'amifine?\';').assertSucceeded()
        mdb2.execute('drop table myt;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('CREATE schema mys2;').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("CREATE USER duser2 WITH PASSWORD 'ups' NAME 'ups' SCHEMA mys2;").assertSucceeded()
        mdb2.execute('drop schema mys2;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('create merge table parent1(a int) PARTITION BY RANGE ON (a);').assertSucceeded()
        mdb1.execute('create merge table parent2(a int);').assertSucceeded()
        mdb1.execute('create table child1(a int);').assertSucceeded()
        mdb1.execute('alter table parent2 add table child1;').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("ALTER TABLE parent1 ADD TABLE parent2 AS PARTITION FROM '1' TO '2';").assertSucceeded()
        mdb2.execute("insert into child1 values (3);").assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('create merge table parent3(a int) PARTITION BY RANGE ON (a);').assertSucceeded()
        mdb1.execute('create merge table parent4(a int);').assertSucceeded()
        mdb1.execute('create table child2(a int);').assertSucceeded()
        mdb1.execute('alter table parent4 add table child2;').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("ALTER TABLE parent3 ADD TABLE parent4 AS PARTITION FROM '1' TO '2';").assertSucceeded()
        mdb2.execute("alter table parent3 set schema mys2;").assertFailed(err_code="42000", err_message="ALTER TABLE: transaction conflict detected")
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('rollback;').assertSucceeded()

        mdb1.execute('create merge table parent5(a int);').assertSucceeded()
        mdb1.execute('create merge table parent6(a int);').assertSucceeded()
        mdb1.execute('create table child3(a int);').assertSucceeded()
        mdb1.execute('alter table parent6 add table child3;').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("ALTER TABLE parent5 ADD TABLE parent6;").assertSucceeded()
        mdb2.execute("insert into child3 values (10);").assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertSucceeded()

        mdb1.execute('create merge table parent7(a int primary key);').assertSucceeded()
        mdb1.execute('create table child4(a int primary key);').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("ALTER TABLE parent7 ADD TABLE child4;").assertSucceeded()
        mdb2.execute("alter table child4 add constraint ugh foreign key(a) references child4(a);").assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('create merge table parent8(a int, b int);').assertSucceeded()
        mdb1.execute('create table child5(a int, b int);').assertSucceeded()
        mdb1.execute('start transaction;').assertSucceeded()
        mdb2.execute('start transaction;').assertSucceeded()
        mdb1.execute("ALTER TABLE parent8 ADD TABLE child5;").assertSucceeded()
        mdb2.execute("alter table child5 drop column b;").assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('commit;').assertFailed(err_code="40000", err_message="COMMIT: transaction is aborted because of concurrency conflicts, will ROLLBACK instead")

        mdb1.execute('start transaction;').assertSucceeded()
        mdb1.execute('drop table myt;').assertSucceeded()
        mdb1.execute('drop user duser;').assertSucceeded()
        mdb1.execute('drop role myrole;').assertSucceeded()
        mdb1.execute('drop schema mys cascade;').assertSucceeded()
        mdb1.execute('drop user duser2;').assertSucceeded()
        mdb1.execute('alter table parent1 drop table parent2;').assertSucceeded()
        mdb1.execute('alter table parent2 drop table child1;').assertSucceeded()
        mdb1.execute('alter table parent3 drop table parent4;').assertSucceeded()
        mdb1.execute('alter table parent4 drop table child2;').assertSucceeded()
        mdb1.execute('alter table parent5 drop table parent6;').assertSucceeded()
        mdb1.execute('alter table parent6 drop table child3;').assertSucceeded()
        mdb1.execute('alter table parent7 drop table child4;').assertSucceeded()
        mdb1.execute('alter table parent8 drop table child5;').assertSucceeded()
        mdb1.execute('drop table child1;').assertSucceeded()
        mdb1.execute('drop table child2;').assertSucceeded()
        mdb1.execute('drop table child3;').assertSucceeded()
        mdb1.execute('drop table child4;').assertSucceeded()
        mdb1.execute('drop table child5;').assertSucceeded()
        mdb1.execute('drop table parent1;').assertSucceeded()
        mdb1.execute('drop table parent2;').assertSucceeded()
        mdb1.execute('drop table parent3;').assertSucceeded()
        mdb1.execute('drop table parent4;').assertSucceeded()
        mdb1.execute('drop table parent5;').assertSucceeded()
        mdb1.execute('drop table parent6;').assertSucceeded()
        mdb1.execute('drop table parent7;').assertSucceeded()
        mdb1.execute('drop table parent8;').assertSucceeded()
        mdb1.execute('drop schema mys2;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()

with SQLTestCase() as mdb1:
    with SQLTestCase() as mdb2:
        mdb1.connect(username="monetdb", password="monetdb")
        mdb1.execute('CREATE schema mys3;').assertSucceeded()
        mdb1.execute("CREATE USER duser3 WITH PASSWORD 'ups' NAME 'ups' SCHEMA mys3;").assertSucceeded()

        mdb2.connect(username="duser3", password="ups")
        mdb2.execute('select 1;').assertSucceeded()

        mdb1.execute('start transaction;').assertSucceeded()
        mdb1.execute('drop user duser3;').assertSucceeded()
        mdb1.execute('drop schema mys3;').assertSucceeded()
        mdb1.execute('commit;').assertSucceeded()
        mdb2.execute('start transaction;').assertFailed(err_code="42000", err_message="The user was not found in the database, this session is going to terminate")
        # mbd2 cannot do anything else, the connection was terminated
