#!/bin/sh

php -d include_path=$PHP_INCPATH -f $TSTSRCDIR/php_dec38.php $MAPIPORT $TSTDB
