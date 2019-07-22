#!/bin/sh
#
test_description='Test flux-shell'

. `dirname $0`/sharness.sh

test_under_flux 4 job

flux setattr log-stderr-level 1

PMI_INFO=${FLUX_BUILD_DIR}/src/common/libpmi/test_pmi_info
KVSTEST=${FLUX_BUILD_DIR}/src/common/libpmi/test_kvstest
LPTEST=${SHARNESS_TEST_DIRECTORY}/shell/lptest

hwloc_fake_config='{"0-3":{"Core":2,"cpuset":"0-1"}}'

test_expect_success 'job-shell: load barrier,job-exec,sched-simple modules' '
        #  Add fake by_rank configuration to kvs:
        flux kvs put resource.hwloc.by_rank="$hwloc_fake_config" &&
        flux module load barrier &&
        flux module load -r 0 sched-simple &&
        flux module load -r 0 job-exec
'
test_expect_success 'job-shell: execute across all ranks' '
        id=$(flux jobspec srun -N4 bash -c \
            "flux kvs put test1.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id) &&
	sort >test1.exp <<-EOT &&
	${kvsdir}.guest.test1.0 = 0
	${kvsdir}.guest.test1.1 = 0
	${kvsdir}.guest.test1.2 = 0
	${kvsdir}.guest.test1.3 = 0
	EOT
	flux kvs dir ${kvsdir}.guest.test1 | sort >test1.out &&
	test_cmp test1.exp test1.out
'
test_expect_success 'job-shell: execute 2 tasks per rank' '
        id=$(flux jobspec srun -N4 -n8 bash -c \
            "flux kvs put test2.\$FLUX_TASK_RANK=\$FLUX_TASK_LOCAL_ID" \
            | flux job submit) &&
        flux job attach --show-events $id &&
        kvsdir=$(flux job id --to=kvs $id) &&
	sort >test2.exp <<-EOT &&
	${kvsdir}.guest.test2.0 = 0
	${kvsdir}.guest.test2.1 = 1
	${kvsdir}.guest.test2.2 = 0
	${kvsdir}.guest.test2.3 = 1
	${kvsdir}.guest.test2.4 = 0
	${kvsdir}.guest.test2.5 = 1
	${kvsdir}.guest.test2.6 = 0
	${kvsdir}.guest.test2.7 = 1
	EOT
	flux kvs dir ${kvsdir}.guest.test2 | sort >test2.out &&
	test_cmp test2.exp test2.out
'
test_expect_success 'job-shell: /bin/true exit code propagated' '
        id=$(flux jobspec srun -n1 /bin/true | flux job submit) &&
	flux job wait-event $id finish >true.finish.out &&
	grep status=0 true.finish.out
'
test_expect_success 'job-shell: /bin/false exit code propagated' '
        id=$(flux jobspec srun -n1 /bin/false | flux job submit) &&
	flux job wait-event $id finish >false.finish.out &&
	grep status=256 false.finish.out
'
test_expect_success 'job-shell: PMI works' '
        id=$(flux jobspec srun -N4 ${PMI_INFO} | flux job submit) &&
	flux job attach $id >pmi_info.out 2>pmi_info.err &&
	grep size=4 pmi_info.out
'
test_expect_success 'job-shell: PMI KVS works' '
        id=$(flux jobspec srun -N4 ${KVSTEST} | flux job submit) &&
	flux job attach $id >kvstest.out 2>kvstest.err &&
	grep "t phase" kvstest.out
'

test_expect_success 'job-shell: create expected I/O output' '
	${LPTEST} | sed -e "s/^/0: /" >lptest.exp &&
	(for i in $(seq 0 3); do \
		${LPTEST} | sed -e "s/^/$i: /"; \
	done) >lptest4.exp
'
test_expect_success 'job-shell: verify output of 1-task lptest job' '
        id=$(flux jobspec srun -n1 ${LPTEST} | flux job submit) &&
	flux job wait-event $id finish &&
	flux job attach -l $id >lptest.out &&
	test_cmp lptest.exp lptest.out
'
test_expect_success 'job-shell: verify output of 1-task lptest job on stderr' '
        id=$(flux jobspec srun -n1 bash -c "${LPTEST} >&2" \
		| flux job submit) &&
	flux job attach -l $id 2>lptest.err &&
	test_cmp lptest.exp lptest.err
'
test_expect_success 'job-shell: verify output of 4-task lptest job' '
        id=$(flux jobspec srun -N4 ${LPTEST} | flux job submit) &&
	flux job attach -l $id >lptest4_raw.out &&
	sort -snk1 <lptest4_raw.out >lptest4.out &&
	test_cmp lptest4.exp lptest4.out
'
test_expect_success 'job-shell: verify output of 4-task lptest job on stderr' '
        id=$(flux jobspec srun -N4 bash -c "${LPTEST} 1>&2" \
		| flux job submit) &&
	flux job attach -l $id 2>lptest4_raw.err &&
	sort -snk1 <lptest4_raw.err >lptest4.err &&
	test_cmp lptest4.exp lptest4.err
'
test_expect_success 'job-exec: unload job-exec & sched-simple modules' '
        flux module remove -r 0 job-exec &&
        flux module remove -r 0 sched-simple &&
        flux module remove barrier
'

test_done