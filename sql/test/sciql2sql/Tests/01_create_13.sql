-- create an array with multiple dimensions and multiple cell values
--! CREATE ARRAY ary (dimx INT DIMENSION[17:3:27], dimy INT DIMENSION[31:2:40], dimz INT DIMENSION[0:5:19], v1 FLOAT DEFAULT 0.43, v2 FLOAT DEFAULT 3.3, v3 INT DEFAULT 999);
--! SELECT * FROM ary;
--! DROP ARRAY ary;

CREATE TABLE ary (dimx INTEGER CHECK(x >= 17 AND x <27 AND x % 3 =2),
				  dimy INTEGER CHECK(x >= 31 AND x <40 AND x % 2 =1),
				  dimz INTEGER CHECK(x >= 0 AND x <19 AND x % 5 = 0),
				  v1 FLOAT DEFAULT 0.42,
				  v2 FLOAT DEFAULT 3.3,
				  v3 INTEGER DEFAULT 999);
INSERT INTO ary values
(  17, 31, 0, 0.43,3.3,999),
(  17, 31, 5, 0.43,3.3,999),
(  17, 31, 10, 0.43,3.3,999),
(  17, 31, 15, 0.43,3.3,999),

(  17, 33, 0, 0.43,3.3,999),
(  17, 33, 5, 0.43,3.3,999),
(  17, 33, 10, 0.43,3.3,999),
(  17, 33, 15, 0.43,3.3,999),

(  17, 35, 0, 0.43,3.3,999),
(  17, 35, 5, 0.43,3.3,999),
(  17, 35, 10, 0.43,3.3,999),
(  17, 35, 15, 0.43,3.3,999),

(  17, 37, 0, 0.43,3.3,999),
(  17, 37, 5, 0.43,3.3,999),
(  17, 37, 10, 0.43,3.3,999),
(  17, 37, 15, 0.43,3.3,999),

(  17, 39, 0, 0.43,3.3,999),
(  17, 39, 5, 0.43,3.3,999),
(  17, 39, 10, 0.43,3.3,999),
(  17, 39, 15, 0.43,3.3,999),

(  20, 31, 0, 0.43,3.3,999),
(  20, 31, 5, 0.43,3.3,999),
(  20, 31, 10, 0.43,3.3,999),
(  20, 31, 15, 0.43,3.3,999),

(  20, 33, 0, 0.43,3.3,999),
(  20, 33, 5, 0.43,3.3,999),
(  20, 33, 10, 0.43,3.3,999),
(  20, 33, 15, 0.43,3.3,999),

(  20, 35, 0, 0.43,3.3,999),
(  20, 35, 5, 0.43,3.3,999),
(  20, 35, 10, 0.43,3.3,999),
(  20, 35, 15, 0.43,3.3,999),

(  20, 37, 0, 0.43,3.3,999),
(  20, 37, 5, 0.43,3.3,999),
(  20, 37, 10, 0.43,3.3,999),
(  20, 37, 15, 0.43,3.3,999),

(  20, 39, 0, 0.43,3.3,999),
(  20, 39, 5, 0.43,3.3,999),
(  20, 39, 10, 0.43,3.3,999),
(  20, 39, 15, 0.43,3.3,999),

(  23, 31, 0, 0.43,3.3,999),
(  23, 31, 5, 0.43,3.3,999),
(  23, 31, 10, 0.43,3.3,999),
(  23, 31, 15, 0.43,3.3,999),

(  23, 33, 0, 0.43,3.3,999),
(  23, 33, 5, 0.43,3.3,999),
(  23, 33, 10, 0.43,3.3,999),
(  23, 33, 15, 0.43,3.3,999),

(  23, 35, 0, 0.43,3.3,999),
(  23, 35, 5, 0.43,3.3,999),
(  23, 35, 10, 0.43,3.3,999),
(  23, 35, 15, 0.43,3.3,999),

(  23, 37, 0, 0.43,3.3,999),
(  23, 37, 5, 0.43,3.3,999),
(  23, 37, 10, 0.43,3.3,999),
(  23, 37, 15, 0.43,3.3,999),

(  23, 39, 0, 0.43,3.3,999),
(  23, 39, 5, 0.43,3.3,999),
(  23, 39, 10, 0.43,3.3,999),
(  23, 39, 15, 0.43,3.3,999),

(  26, 31, 0, 0.43,3.3,999),
(  26, 31, 5, 0.43,3.3,999),
(  26, 31, 10, 0.43,3.3,999),
(  26, 31, 15, 0.43,3.3,999),

(  26, 33, 0, 0.43,3.3,999),
(  26, 33, 5, 0.43,3.3,999),
(  26, 33, 10, 0.43,3.3,999),
(  26, 33, 15, 0.43,3.3,999),

(  26, 35, 0, 0.43,3.3,999),
(  26, 35, 5, 0.43,3.3,999),
(  26, 35, 10, 0.43,3.3,999),
(  26, 35, 15, 0.43,3.3,999),

(  26, 37, 0, 0.43,3.3,999),
(  26, 37, 5, 0.43,3.3,999),
(  26, 37, 10, 0.43,3.3,999),
(  26, 37, 15, 0.43,3.3,999),

(  26, 39, 0, 0.43,3.3,999),
(  26, 39, 5, 0.43,3.3,999),
(  26, 39, 10, 0.43,3.3,999),
(  26, 39, 15, 0.43,3.3,999);

SELECT * FROM ary;
DROP TABLE ary;
