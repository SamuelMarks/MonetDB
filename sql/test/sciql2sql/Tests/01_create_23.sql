--! CREATE ARRAY ary (i INT DIMENSION[4], v TIMESTAMP);
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v DATE);
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v TIME);
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v CHAR(5));
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v VARCHAR(5) DEFAULT 'v5');
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v CLOB DEFAULT 'abcd');
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v BLOB DEFAULT '1234');
--! SELECT * FROM ary;
--! DROP ARRAY ary;

--! CREATE ARRAY ary (i INT DIMENSION[4], v1 CHAR(5), v2 TIMESTAMP, v3 DATE, v4 TIME, v5 VARCHAR(5) DEFAULT 'v5', v6 CLOB DEFAULT 'abcd', v7 BLOB DEFAULT '1234');
--! SELECT * FROM ary;
--! DROP ARRAY ary;


CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v TIMESTAMP);
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v DATE);
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v TIME);
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v CHAR(5));
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v VARCHAR(5) DEFAULT 'v5');
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v CLOB DEFAULT 'abcd');
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v BLOB DEFAULT '1234');
SELECT * FROM ary;
DROP TABLE ary;

CREATE TABLE ary (i INT CHECK(x>=0 AND x <4), v1 CHAR(5), v2 TIMESTAMP, v3 DATE, v4 TIME, v5 VARCHAR(5) DEFAULT 'v5', v6 CLOB DEFAULT 'abcd', v7 BLOB DEFAULT '1234');
SELECT * FROM ary;
DROP TABLE ary;

