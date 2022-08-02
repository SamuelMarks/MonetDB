START TRANSACTION;

CREATE TABLE REGION  ( R_REGIONKEY  INTEGER NOT NULL,
                            R_NAME       CHAR(25) NOT NULL,
                            R_COMMENT    VARCHAR(152));

CREATE TABLE NATION  ( N_NATIONKEY  INTEGER NOT NULL,
                            N_NAME       CHAR(25) NOT NULL,
                            N_REGIONKEY  INTEGER NOT NULL,
                            N_COMMENT    VARCHAR(152));

CREATE TABLE PART  ( P_PARTKEY     INTEGER NOT NULL,
                          P_NAME        VARCHAR(55) NOT NULL,
                          P_MFGR        CHAR(25) NOT NULL,
                          P_BRAND       CHAR(10) NOT NULL,
                          P_TYPE        VARCHAR(25) NOT NULL,
                          P_SIZE        INTEGER NOT NULL,
                          P_CONTAINER   CHAR(10) NOT NULL,
                          P_RETAILPRICE DECIMAL(15,2) NOT NULL,
                          P_COMMENT     VARCHAR(23) NOT NULL );

CREATE TABLE SUPPLIER ( S_SUPPKEY     INTEGER NOT NULL,
                             S_NAME        CHAR(25) NOT NULL,
                             S_ADDRESS     VARCHAR(40) NOT NULL,
                             S_NATIONKEY   INTEGER NOT NULL,
                             S_PHONE       CHAR(15) NOT NULL,
                             S_ACCTBAL     DECIMAL(15,2) NOT NULL,
                             S_COMMENT     VARCHAR(101) NOT NULL);

CREATE TABLE PARTSUPP ( PS_PARTKEY     INTEGER NOT NULL,
                             PS_SUPPKEY     INTEGER NOT NULL,
                             PS_AVAILQTY    INTEGER NOT NULL,
                             PS_SUPPLYCOST  DECIMAL(15,2)  NOT NULL,
                             PS_COMMENT     VARCHAR(199) NOT NULL );

CREATE TABLE CUSTOMER ( C_CUSTKEY     INTEGER NOT NULL,
                             C_NAME        VARCHAR(25) NOT NULL,
                             C_ADDRESS     VARCHAR(40) NOT NULL,
                             C_NATIONKEY   INTEGER NOT NULL,
                             C_PHONE       CHAR(15) NOT NULL,
                             C_ACCTBAL     DECIMAL(15,2)   NOT NULL,
                             C_MKTSEGMENT  CHAR(10) NOT NULL,
                             C_COMMENT     VARCHAR(117) NOT NULL);

CREATE TABLE ORDERS  ( O_ORDERKEY       INTEGER NOT NULL,
                           O_CUSTKEY        INTEGER NOT NULL,
                           O_ORDERSTATUS    CHAR(1) NOT NULL,
                           O_TOTALPRICE     DECIMAL(15,2) NOT NULL,
                           O_ORDERDATE      DATE NOT NULL,
                           O_ORDERPRIORITY  CHAR(15) NOT NULL,  
                           O_CLERK          CHAR(15) NOT NULL, 
                           O_SHIPPRIORITY   INTEGER NOT NULL,
                           O_COMMENT        VARCHAR(79) NOT NULL);

CREATE TABLE LINEITEM ( L_ORDERKEY    INTEGER NOT NULL,
                             L_PARTKEY     INTEGER NOT NULL,
                             L_SUPPKEY     INTEGER NOT NULL,
                             L_LINENUMBER  INTEGER NOT NULL,
                             L_QUANTITY    DECIMAL(15,2) NOT NULL,
                             L_EXTENDEDPRICE  DECIMAL(15,2) NOT NULL,
                             L_DISCOUNT    DECIMAL(15,2) NOT NULL,
                             L_TAX         DECIMAL(15,2) NOT NULL,
                             L_RETURNFLAG  CHAR(1) NOT NULL,
                             L_LINESTATUS  CHAR(1) NOT NULL,
                             L_SHIPDATE    DATE NOT NULL,
                             L_COMMITDATE  DATE NOT NULL,
                             L_RECEIPTDATE DATE NOT NULL,
                             L_SHIPINSTRUCT CHAR(25) NOT NULL,
                             L_SHIPMODE     CHAR(10) NOT NULL,
                             L_COMMENT      VARCHAR(44) NOT NULL);

ALTER TABLE REGION
ADD PRIMARY KEY (R_REGIONKEY);

-- For table NATION
ALTER TABLE NATION
ADD PRIMARY KEY (N_NATIONKEY);

ALTER TABLE NATION
ADD CONSTRAINT NATION_FK1 FOREIGN KEY (N_REGIONKEY) references REGION;

-- For table PART
ALTER TABLE PART
ADD PRIMARY KEY (P_PARTKEY);

-- For table SUPPLIER
ALTER TABLE SUPPLIER
ADD PRIMARY KEY (S_SUPPKEY);

ALTER TABLE SUPPLIER
ADD CONSTRAINT SUPPLIER_FK1 FOREIGN KEY (S_NATIONKEY) references NATION;

-- For table PARTSUPP
ALTER TABLE PARTSUPP
ADD PRIMARY KEY (PS_PARTKEY,PS_SUPPKEY);

-- For table CUSTOMER
ALTER TABLE CUSTOMER
ADD PRIMARY KEY (C_CUSTKEY);

ALTER TABLE CUSTOMER
ADD CONSTRAINT CUSTOMER_FK1 FOREIGN KEY (C_NATIONKEY) references NATION;

-- For table LINEITEM
ALTER TABLE LINEITEM
ADD PRIMARY KEY (L_ORDERKEY,L_LINENUMBER);

-- For table ORDERS
ALTER TABLE ORDERS
ADD PRIMARY KEY (O_ORDERKEY);

-- For table PARTSUPP
ALTER TABLE PARTSUPP
ADD CONSTRAINT PARTSUPP_FK1 FOREIGN KEY (PS_SUPPKEY) references SUPPLIER;

ALTER TABLE PARTSUPP
ADD CONSTRAINT PARTSUPP_FK2 FOREIGN KEY (PS_PARTKEY) references PART;

-- For table ORDERS
ALTER TABLE ORDERS
ADD CONSTRAINT ORDERS_FK1 FOREIGN KEY (O_CUSTKEY) references CUSTOMER;

-- For table LINEITEM
ALTER TABLE LINEITEM
ADD CONSTRAINT LINEITEM_FK1 FOREIGN KEY (L_ORDERKEY) references ORDERS;

ALTER TABLE LINEITEM
ADD CONSTRAINT LINEITEM_FK2 FOREIGN KEY (L_PARTKEY,L_SUPPKEY) references
        PARTSUPP;

SELECT
   DM_CustomerNation_N_NAME AS CustomerNation,
   DM_CustomerRegion_R_NAME AS CustomerRegion,
   avg(DM_Li_L_DISCOUNT) AS L_DISCOUNT_Customer_Nation_Supplier_Nation,
   CAST(sum(DM_Li_L_QUANTITY) AS DECIMAL(10,5)) AS L_QUANTITY_Customer_Nation_Supplier_Nation,
   O_ORDERDATE_Year,
   DM_Nation_N_NAME AS SupplierNation,
   DM_Region_R_NAME AS SupplierRegion 
FROM
   CUSTOMER 
   INNER JOIN
     (
         SELECT
            DM_Li_L_DISCOUNT,
            DM_Li_L_ORDERKEY,DM_Li_L_PARTKEY,
            DM_Li_L_QUANTITY,
            DM_Nation_N_NAME,
            DM_Nation_N_NATIONKEY,DM_Nation_N_REGIONKEY,
            DM_Orders_O_CUSTKEY,DM_Orders_O_ORDERDATE,
            DM_Orders_O_ORDERKEY,
            DM_Partsupp_PS_PARTKEY,
            DM_Partsupp_PS_SUPPKEY,
            DM_Region_R_NAME,
            DM_Region_R_REGIONKEY,
            S_NATIONKEY AS DM_Supplier_S_NATIONKEY,
            S_SUPPKEY AS DM_Supplier_S_SUPPKEY,O_ORDERDATE_Year
         FROM
            SUPPLIER 
            INNER JOIN 
               (
                  SELECT
                     DM_Li_L_DISCOUNT,
                     DM_Li_L_ORDERKEY,DM_Li_L_PARTKEY,
                     DM_Li_L_QUANTITY,
                     PS_PARTKEY AS DM_Partsupp_PS_PARTKEY,PS_SUPPKEY AS DM_Partsupp_PS_SUPPKEY
                  FROM
                     PARTSUPP 
                     INNER JOIN
                        (
                           SELECT
                              L_DISCOUNT AS DM_Li_L_DISCOUNT,
                              L_ORDERKEY AS DM_Li_L_ORDERKEY,
                              L_PARTKEY AS DM_Li_L_PARTKEY,
                              L_QUANTITY AS DM_Li_L_QUANTITY,
                              L_SUPPKEY AS DM_Li_L_SUPPKEY
                           FROM
                              LINEITEM
                        ) AS LINEITEM
                        ON PS_SUPPKEY = DM_Li_L_SUPPKEY 
                        AND PS_PARTKEY = DM_Li_L_PARTKEY
               ) 
               AS LINEITEMPARTSUPP
               ON S_SUPPKEY = DM_Partsupp_PS_SUPPKEY 
            INNER JOIN
               (
                  SELECT
                     N_NAME AS DM_Nation_N_NAME,
                     N_NATIONKEY AS DM_Nation_N_NATIONKEY,
                     N_REGIONKEY AS DM_Nation_N_REGIONKEY 
                  FROM
                     NATION
               )
               AS NATION
               ON S_NATIONKEY = DM_Nation_N_NATIONKEY 
            INNER JOIN
               (
                  SELECT
                     R_NAME AS DM_Region_R_NAME,
                     R_REGIONKEY AS DM_Region_R_REGIONKEY 
                  FROM
                     REGION
               ) 
               AS REGION
               ON DM_Nation_N_REGIONKEY = DM_Region_R_REGIONKEY 
            INNER JOIN
               (
                  SELECT
                     O_CUSTKEY AS DM_Orders_O_CUSTKEY,
                     O_ORDERDATE AS DM_Orders_O_ORDERDATE,
                     O_ORDERKEY AS DM_Orders_O_ORDERKEY,
                     O_TOTALPRICE AS DM_Orders_O_TOTALPRICE,
                     Extract(YEAR FROM O_ORDERDATE) AS O_ORDERDATE_Year
                  FROM
                     ORDERS
               )
               AS ORDERS
               ON DM_Li_L_ORDERKEY = DM_Orders_O_ORDERKEY
      )
      AS CUSTOMERORDERDETAILS
      ON C_CUSTKEY = DM_Orders_O_CUSTKEY 
   INNER JOIN
      (
         SELECT
            N_NAME AS DM_CustomerNation_N_NAME,
            N_NATIONKEY AS DM_CustomerNation_N_NATIONKEY,
            N_REGIONKEY AS DM_CustomerNation_N_REGIONKEY 
         FROM
            NATION
      )
      AS NATION
      ON C_NATIONKEY = DM_CustomerNation_N_NATIONKEY 
   INNER JOIN
      (
         SELECT
            R_NAME AS DM_CustomerRegion_R_NAME,
            R_REGIONKEY AS DM_CustomerRegion_R_REGIONKEY 
         FROM
            REGION
      )
      AS REGION
      ON DM_CustomerNation_N_REGIONKEY = DM_CustomerRegion_R_REGIONKEY 
WHERE
   O_ORDERDATE_Year >= 1996 
   AND DM_CustomerRegion_R_NAME IN 
   (
      'EUROPE',
      'AMERICA' 
   )
   AND DM_Region_R_NAME IN 
   (
      'EUROPE',
      'AMERICA' 
   )
GROUP BY CUBE(O_ORDERDATE_Year, CustomerRegion, CustomerNation, SupplierRegion, SupplierNation);

ROLLBACK;
