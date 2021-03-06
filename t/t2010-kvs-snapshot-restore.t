#!/bin/sh
#

test_description='Test KVS snapshot/restore'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

test_under_flux 1

CHECKPOINT=${FLUX_BUILD_DIR}/t/kvs/checkpoint

test_expect_success 'store kvs-checkpoint key-val pairs' '
	$CHECKPOINT put foo bar &&
	$CHECKPOINT put foo2 42 &&
	$CHECKPOINT put foo3 "x x x"
'

test_expect_success 'verify kvs-checkpoint key-val pairs' '
	test "$($CHECKPOINT get foo)" = "bar" &&
	test "$($CHECKPOINT get foo2)" = "42" &&
	test "$($CHECKPOINT get foo3)" = "x x x"
'

test_expect_success 'get unknown kvs-checkpoint key fails' '
	test_must_fail $CHECKPOINT get noexist
'

test_expect_success 'put existing kvs-checkpoint key is allowed' '
	$CHECKPOINT put foo zzz
'

test_expect_success 'kvs-checkpoint value was updated' '
	test $($CHECKPOINT get foo) = "zzz"
'

test_expect_success 'empty kvs-checkpoint key is not allowed' '
	test_must_fail $CHECKPOINT put "" xyz
'

test_expect_success 'run instance with content.backing-path set' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux kvs put testkey=42
'

test_expect_success 'content.sqlite file exists after instance exited' '
	test -f content.sqlite &&
	echo Size in bytes: $(stat --format "%s" content.sqlite)
'

test_expect_success 're-run instance with content.backing-path set' '
	flux start -o,--setattr=content.backing-path=$(pwd)/content.sqlite \
	           flux kvs get testkey >get.out
'

test_expect_success 'content from previous instance survived' '
	echo 42 >get.exp &&
	test_cmp get.exp get.out
'

test_done
