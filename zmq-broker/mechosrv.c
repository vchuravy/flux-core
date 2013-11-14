
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

/* Copy input arguments to output arguments and respond to RPC.
 */
static void mechosrv_recv (flux_t h, zmsg_t **zmsg, zmsg_type_t type)
{
    json_object *request = NULL;
    json_object *inarg = NULL;
    flux_mrpc_t f = NULL;

    if (type != ZMSG_EVENT) {
        flux_log (h, LOG_ERR, "ignoring non-event message");
        goto done;
    }
    if (cmb_msg_decode (*zmsg, NULL, &request) < 0) {
        flux_log (h, LOG_ERR, "cmb_msg_decode: %s", strerror (errno));
        goto done;
    }
    if (!request) {
        flux_log (h, LOG_ERR, "missing JSON part");
        goto done;
    }
    if (!(f = flux_mrpc_create_fromevent (h, request))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "flux_mrpc_create_fromevent: %s",
                                    strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (f, &inarg) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_get_inarg: %s", strerror (errno));
        goto done;
    }
    flux_mrpc_put_outarg (f, inarg);
    if (flux_mrpc_respond (f) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    if (request)
        json_object_put (request);
    if (inarg)
        json_object_put (inarg);
    if (f)
        flux_mrpc_destroy (f);
    zmsg_destroy (zmsg);
}

static void mechosrv_init (flux_t h)
{
    if (flux_event_subscribe (h, "mrpc.mecho") < 0)
        err_exit ("%s: flux_event_subscribe", __FUNCTION__);
}

static void mechosrv_fini (flux_t h)
{
    if (flux_event_unsubscribe (h, "mrpc.mecho") < 0)
        err_exit ("%s: flux_event_unsubscribe", __FUNCTION__);
}

struct plugin_struct mechosrv = {
    .name = "mecho",
    .recvFn = mechosrv_recv,
    .initFn = mechosrv_init,
    .finiFn = mechosrv_fini,
};

/*
 * vi: ts=4 sw=4 expandtab
 */