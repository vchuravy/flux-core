#!/bin/sh

test_description='Test content-sqlite service'

. `dirname $0`/sharness.sh

# Size the session to one more than the number of cores, minimum of 4
SIZE=$(test_size_large)
test_under_flux ${SIZE} minimal
echo "# $0: flux session size will be ${SIZE}"

BLOBREF=${FLUX_BUILD_DIR}/t/kvs/blobref
RPC=${FLUX_BUILD_DIR}/t/request/rpc

MAXBLOB=`flux getattr content.blob-size-limit`
HASHFUN=`flux getattr content.hash`


store_junk() {
    local name=$1
    local n=$2
    for i in `seq 1 $n`; do \
        echo "$name:$i" | flux content store >/dev/null || return 1
    done
}

test_expect_success 'load content-sqlite module on rank 0' '
	flux module load content-sqlite
'

test_expect_success 'verify content.backing-module=content-sqlite' '
	test "$(flux getattr content.backing-module)" = "content-sqlite"
'

test_expect_success 'store 100 blobs on rank 0' '
	store_junk test 100 &&
        TOTAL=`flux module stats --type int --parse count content` &&
        test $TOTAL -ge 100
'

# Store directly to content service
# Verify directly from content service

test_expect_success 'store blobs bypassing cache' '
	cat /dev/null >0.0.store &&
        flux content store --bypass-cache <0.0.store >0.0.hash &&
        dd if=/dev/urandom count=1 bs=64 >64.0.store 2>/dev/null &&
        flux content store --bypass-cache <64.0.store >64.0.hash &&
        dd if=/dev/urandom count=1 bs=4096 >4k.0.store 2>/dev/null &&
        flux content store --bypass-cache <4k.0.store >4k.0.hash &&
        dd if=/dev/urandom count=256 bs=4096 >1m.0.store 2>/dev/null &&
        flux content store --bypass-cache <1m.0.store >1m.0.hash
'

test_expect_success LONGTEST "cannot store blob that exceeds max size of $MAXBLOB" '
        dd if=/dev/zero count=$(($MAXBLOB/4096+1)) bs=4096 \
			skip=$(($MAXBLOB/4096)) >toobig 2>/dev/null &&
        test_must_fail flux content store --bypass-cache <toobig
'

test_expect_success 'load 0b blob bypassing cache' '
        HASHSTR=`cat 0.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >0.0.load &&
        test_cmp 0.0.store 0.0.load
'

test_expect_success 'load 64b blob bypassing cache' '
        HASHSTR=`cat 64.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >64.0.load &&
        test_cmp 64.0.store 64.0.load
'

test_expect_success 'load 4k blob bypassing cache' '
        HASHSTR=`cat 4k.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >4k.0.load &&
        test_cmp 4k.0.store 4k.0.load
'

test_expect_success 'load 1m blob bypassing cache' '
        HASHSTR=`cat 1m.0.hash` &&
        flux content load --bypass-cache ${HASHSTR} >1m.0.load &&
        test_cmp 1m.0.store 1m.0.load
'

# Verify same blobs on all ranks
# forcing content to fault in from the content backing service

test_expect_success 'load and verify 64b blob on all ranks' '
        HASHSTR=`cat 64.0.hash` &&
        flux exec -n echo ${HASHSTR} >64.0.all.expect &&
        flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >64.0.all.output &&
        test_cmp 64.0.all.expect 64.0.all.output
'

test_expect_success 'load and verify 4k blob on all ranks' '
        HASHSTR=`cat 4k.0.hash` &&
        flux exec -n echo ${HASHSTR} >4k.0.all.expect &&
        flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >4k.0.all.output &&
        test_cmp 4k.0.all.expect 4k.0.all.output
'

test_expect_success 'load and verify 1m blob on all ranks' '
        HASHSTR=`cat 1m.0.hash` &&
        flux exec -n echo ${HASHSTR} >1m.0.all.expect &&
        flux exec -n sh -c "flux content load ${HASHSTR} | $BLOBREF $HASHFUN" \
                                                >1m.0.all.output &&
        test_cmp 1m.0.all.expect 1m.0.all.output
'

test_expect_success 'exercise batching of synchronous flush to backing store' '
	flux setattr content.flush-batch-limit 5 &&
        store_junk loadunload 200 &&
    	flux content flush &&
	NDIRTY=`flux module stats --type int --parse dirty content` &&
	test ${NDIRTY} -eq 0
'

kvs_checkpoint_put() {
        jq -j -c -n  "{key:\"$1\",value:\"$2\"}" | $RPC kvs-checkpoint.put
}
kvs_checkpoint_get() {
        jq -j -c -n  "{key:\"$1\"}" | $RPC kvs-checkpoint.get
}

test_expect_success HAVE_JQ 'kvs-checkpoint.put foo=bar' '
	kvs_checkpoint_put foo bar
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo returned bar' '
	echo bar >value.exp &&
	kvs_checkpoint_get foo | jq -r .value >value.out &&
	test_cmp value.exp value.out
'

test_expect_success HAVE_JQ 'kvs-checkpoint.put updates foo=baz' '
	kvs_checkpoint_put foo baz
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo returned baz' '
	echo baz >value2.exp &&
	kvs_checkpoint_get foo | jq -r .value >value2.out &&
	test_cmp value2.exp value2.out
'

test_expect_success 'reload content-sqlite module on rank 0' '
	flux module reload content-sqlite
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get foo still returns baz' '
	echo baz >value3.exp &&
	kvs_checkpoint_get foo | jq -r .value >value3.out &&
	test_cmp value3.exp value3.out
'

test_expect_success HAVE_JQ 'kvs-checkpoint.get noexist fails with No such...' '
        test_must_fail kvs_checkpoint_get noexist 2>badkey.err &&
        grep "No such file or directory" badkey.err
'

test_expect_success 'content-backing.load invalid blobref fails' '
        echo -n sha999-000 >bad.blobref &&
        $RPC content-backing.load 2 <bad.blobref 2>load.err
'

test_expect_success 'remove content-sqlite module on rank 0' '
	flux module remove content-sqlite
'


test_done
