/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <json.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

void test_null (flux_t h, uint32_t nodeid);
void test_echo (flux_t h, uint32_t nodeid);
void test_err (flux_t h, uint32_t nodeid);
void test_src (flux_t h, uint32_t nodeid);
void test_sink (flux_t h, uint32_t nodeid);
void test_nsrc (flux_t h, uint32_t nodeid);
void test_putmsg (flux_t h, uint32_t nodeid);
void test_pingzero (flux_t h, uint32_t nodeid);
void test_pingself (flux_t h, uint32_t nodeid);
void test_pingany (flux_t h, uint32_t nodeid);

typedef struct {
    const char *name;
    void (*fun)(flux_t h, uint32_t nodeid);
} test_t;

static test_t tests[] = {
    { "null",   &test_null },
    { "echo",   &test_echo },
    { "err",    &test_err },
    { "src",    &test_src },
    { "sink",   &test_sink },
    { "nsrc",   &test_nsrc },
    { "putmsg", &test_putmsg },
    { "pingzero", &test_pingzero},
    { "pingself", &test_pingself},
    { "pingany", &test_pingany},
};

test_t *test_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (tests) / sizeof (tests[0]); i++)
        if (!strcmp (tests[i].name, name))
            return &tests[i];
    return NULL;
}

#define OPTIONS "hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: treq [--rank N] {null | echo | err | src | sink | nsrc | putmsg | pingzero | pingself | pingany}\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    uint32_t nodeid = FLUX_NODEID_ANY;
    test_t *t;

    log_init ("treq");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    if (!(t = test_lookup (argv[optind])))
        usage ();

    if (!(h = flux_api_open ()))
        err_exit ("flux_api_open");

    t->fun (h, nodeid);

    flux_api_close (h);

    log_fini ();
    return 0;
}

void test_null (flux_t h, uint32_t nodeid)
{
    if (flux_json_rpc (h, nodeid, "req.null", NULL, NULL) < 0)
        err_exit ("req.null");
}

void test_echo (flux_t h, uint32_t nodeid)
{
    JSON in = Jnew ();
    JSON out = NULL;
    const char *s;

    Jadd_str (in, "mumble", "burble");
    if (flux_json_rpc (h, nodeid, "req.echo", in, &out) < 0)
        err_exit ("%s", __FUNCTION__);
    if (!out)
        msg_exit ("%s: no JSON returned", __FUNCTION__);
    if (!Jget_str (out, "mumble", &s) || strcmp (s, "burble") != 0)
        msg_exit ("%s: returned JSON wasn't an echo", __FUNCTION__);
    Jput (in);
    Jput (out);
}

void test_err (flux_t h, uint32_t nodeid)
{
    if (flux_json_rpc (h, nodeid, "req.err", NULL, NULL) == 0)
        msg_exit ("%s: succeeded when should've failed", __FUNCTION__);
    if (errno != 42)
        msg_exit ("%s: got errno %d instead of 42", __FUNCTION__, errno);
}

void test_src (flux_t h, uint32_t nodeid)
{
    JSON out = NULL;
    int i;
    if (flux_json_rpc (h, nodeid, "req.src", NULL, &out) < 0)
        err_exit ("%s", __FUNCTION__);
    if (!out)
        msg_exit ("%s: no JSON returned", __FUNCTION__);
    if (!Jget_int (out, "wormz", &i) || i != 42)
        msg_exit ("%s: didn't get expected JSON", __FUNCTION__);
    Jput (out);
}

void test_sink (flux_t h, uint32_t nodeid)
{
    JSON in = Jnew();
    Jadd_double (in, "pi", 3.14);
    if (flux_json_rpc (h, nodeid, "req.sink", in, NULL) < 0)
        err_exit ("%s", __FUNCTION__);
    Jput (in);
}

void test_nsrc (flux_t h, uint32_t nodeid)
{
    const int count = 10000;
    JSON in = Jnew ();
    JSON out = NULL;
    int i, seq;

    Jadd_int (in, "count", count);
    if (flux_json_request (h, nodeid, 0, "req.nsrc", in) < 0)
        err_exit ("%s", __FUNCTION__);

    for (i = 0; i < count; i++) {
        zmsg_t *zmsg = flux_response_recvmsg (h, 0, false);
        if (!zmsg)
            err_exit ("%s", __FUNCTION__);
        if (flux_json_response_decode (zmsg, &out) < 0)
            msg_exit ("%s: decode %d", __FUNCTION__, i);
        if (!Jget_int (out, "seq", &seq))
            msg_exit ("%s: decode %d - no seq", __FUNCTION__, i);
        if (seq != i)
            msg_exit ("%s: decode %d - seq mismatch %d", __FUNCTION__, i, seq);
        Jput (out);
    }
}

/* This test is to make sure that deferred responses are handled in order.
 * Arrange for module to source 10K sequenced responses.  Messages 5000-5499
 * are "put back" on the handle using flux_response_putmsg().  We ensure that
 * the 10K messages are nonetheless received in order.
 */
void test_putmsg (flux_t h, uint32_t nodeid)
{
    const int count = 10000;
    const int defer_start = 5000;
    const int defer_count = 500;
    JSON in = Jnew ();
    JSON out = NULL;
    int seq, myseq = 0;
    zlist_t *defer = zlist_new ();
    bool popped = false;
    zmsg_t *z;

    if (!defer)
        oom ();

    Jadd_int (in, "count", count);
    if (flux_json_request (h, nodeid, 0, "req.nsrc", in) < 0)
        err_exit ("%s", __FUNCTION__);
    do {
        zmsg_t *zmsg = flux_response_recvmsg (h, 0, false);
        if (!zmsg)
            err_exit ("%s", __FUNCTION__);
        if (flux_json_response_decode (zmsg, &out) < 0)
            msg_exit ("%s: decode", __FUNCTION__);
        if (!Jget_int (out, "seq", &seq))
            msg_exit ("%s: decode - no seq", __FUNCTION__);
        Jput (out);
        if (seq >= defer_start && seq < defer_start + defer_count && !popped) {
            if (zlist_append (defer, zmsg) < 0)
                oom ();
            if (seq == defer_start + defer_count - 1) {
                while ((z = zlist_pop (defer))) {
                    if (flux_response_putmsg (h, &z) < 0)
                        err_exit ("%s: flux_response_putmsg", __FUNCTION__);
                }
                popped = true;
            }
            continue;
        }
        if (seq != myseq)
            msg_exit ("%s: expected %d got %d", __FUNCTION__, myseq, seq);
        myseq++;
        zmsg_destroy (&zmsg);
    } while (myseq < count);
    zlist_destroy (&defer);
}

static int count_hops (const char *s)
{
    const char *p = s;
    int count = 0;
    if (strlen (p) > 0)
        count++;
    while ((p = strchr (p, '!'))) {
        p++;
        count++;
    }
    return count;
}

static void xping (flux_t h, uint32_t nodeid, uint32_t xnodeid, const char *svc)
{
    JSON in = Jnew ();
    JSON out = NULL;
    const char *route;

    Jadd_int (in, "rank", xnodeid);
    Jadd_str (in, "service", svc);
    if (flux_json_rpc (h, nodeid, "req.xping", in, &out) < 0)
        err_exit ("req.xping");
    if (!Jget_str (out, "route", &route))
        errn_exit (EPROTO, "req.xping");
    printf ("hops=%d\n", count_hops (route));
    Jput (out);
    Jput (in);
}

void test_pingzero (flux_t h, uint32_t nodeid)
{
    xping (h, nodeid, 0, "req.ping");
}

void test_pingany (flux_t h, uint32_t nodeid)
{
    xping (h, nodeid, FLUX_NODEID_ANY, "req.ping");
}

void test_pingself (flux_t h, uint32_t nodeid)
{
    xping (h, nodeid, nodeid, "req.ping");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */