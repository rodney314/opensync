/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "evx.h"
#include "daemon.h"
#include "ds_dlist.h"

#include "const.h"
#include "util.h"
#include "log.h"
#include "inet_upnp.h"

#if !defined(CONFIG_USE_KCONFIG)

#define CONFIG_INET_MINIUPNPD_PATH      "/usr/sbin/miniupnpd"
#define CONFIG_INET_MINIUPNPD_ETC       "/tmp/miniupnpd"

#endif

#define UPNP_MINIUPNPD_CONF_PATH      CONFIG_INET_MINIUPNPD_ETC "/miniupnpd.conf"
#define UPNP_MINIUPNPD_LEASES_PATH    CONFIG_INET_MINIUPNPD_ETC "/upnpd.leases"

#define UPNP_DEBOUNCE_TIMER      1.0

static char upnp_miniupnpd_conf_header[] =
    "#\n"
    "# Auto-generated by Plume\n"
    "#\n"
    "\n"
    "enable_natpmp=yes\n"
    "enable_upnp=yes\n"
    "secure_mode=yes\n"
    "system_uptime=yes\n"
    "allow 1024-65535 0.0.0.0/0 1024-65535\n"
    "deny 0-65535 0.0.0.0/0 0-65535\n"
    "\n"
    "# ---\n"
    "\n";

struct __inet_upnp
{
    char                    upnp_ifname[C_IFNAME_LEN];
    bool                    upnp_enabled;
    bool                    upnp_nat_enabled;
    enum inet_upnp_mode     upnp_mode_active;
    enum inet_upnp_mode     upnp_mode_inactive;
    ds_dlist_t              upnp_dnode;
};

static void __upnp_restart(void);
static void __upnp_restart_debounce(struct ev_loop *loop, ev_debounce *w, int revent);
static void upnp_restart(void);

static bool upnp_global_init = false;
static ds_dlist_t upnp_list = DS_DLIST_INIT(inet_upnp_t, upnp_dnode);
static daemon_t upnp_process;
static ev_debounce upnp_debounce;

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */
bool inet_upnp_init(inet_upnp_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    if (strscpy(self->upnp_ifname, ifname, sizeof(self->upnp_ifname)) < 0)
    {
        LOG(ERR, "miniupnp: Interface name %s is too long.", ifname);
        return false;
    }

    if (!upnp_global_init)
    {
        /*
         * Initialize the miniupnpd global instance and process
         */
        ev_debounce_init(&upnp_debounce, __upnp_restart_debounce, UPNP_DEBOUNCE_TIMER);

        if (!daemon_init(&upnp_process, CONFIG_INET_MINIUPNPD_PATH, DAEMON_LOG_ALL))
        {
            LOG(ERR, "miniupnp: Error initializing UPnP process object.");
            return false;
        }

        daemon_arg_add(&upnp_process, "-d");                             /* Run in foreground */
        daemon_arg_add(&upnp_process, "-f", UPNP_MINIUPNPD_CONF_PATH);   /* Config file path */

        /* Path to the PID file */
        if (!daemon_pidfile_set(&upnp_process, "/var/run/miniupnpd.pid", false))
        {
            LOG(ERR, "miniupnp: Error initializing UPnP process PID file.");
            return false;
        }

        upnp_global_init = true;
    }

    return true;
}

bool inet_upnp_fini(inet_upnp_t *self)
{
    return inet_upnp_stop(self);
}


inet_upnp_t *inet_upnp_new(const char *ifname)
{
    inet_upnp_t *self = malloc(sizeof(inet_upnp_t));

    if (!inet_upnp_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

bool inet_upnp_del(inet_upnp_t *self)
{
    bool retval =  inet_upnp_fini(self);
    if (!retval)
    {
        LOG(WARN, "miniupnp: Error stopping UPNP on interface: %s", self->upnp_ifname);
    }

    free(self);

    return retval;
}

/**
 * Start the FW service on interface
 */
bool inet_upnp_start(inet_upnp_t *self)
{
    if (self->upnp_enabled) return true;

#if 0
    /* If UPnP is active, insert it into the UPnP list */
    self->upnp_mode_active = UPNP_MODE_NONE;
    if (self->upnp_mode_inactive != UPNP_MODE_NONE)
    {
        if (self->upnp_mode_inactive == UPNP_MODE_EXTERNAL && !self->upnp_nat_enabled)
        {
            LOG(WARN, "miniupnp: NAT must be enabled on the external UPnP interface.");
        }
        else if (self->upnp_mode_inactive == UPNP_MODE_INTERNAL && self->upnp_nat_enabled)
        {
            LOG(WARN, "miniupnp: NAT must be disabled on the internal UPnP interface.");
        }
        else
        {
            /* Add the self to the list of UPnP interfaces and perform a delayed restart */
            self->upnp_mode_active = self->upnp_mode_inactive;
            ds_dlist_insert_tail(&upnp_list, self);
            fw_upnp_restart();
        }
    }
#endif
    if (self->upnp_mode_inactive != UPNP_MODE_NONE)
    {
        /* Add self to the list of UPnP interfaces and perform a delayed restart */
        self->upnp_mode_active = self->upnp_mode_inactive;
        ds_dlist_insert_tail(&upnp_list, self);
        upnp_restart();
    }

    self->upnp_enabled = true;

    return true;
}

/**
 * Stop the miniupnpd service on interface
 */
bool inet_upnp_stop(inet_upnp_t *self)
{
    bool retval = true;

    if (!self->upnp_enabled) return true;

    if (self->upnp_mode_active != UPNP_MODE_NONE)
    {
        self->upnp_mode_active = UPNP_MODE_NONE;
        ds_dlist_remove(&upnp_list, self);
        /* Remove self from the list of UPnP interfaces and perform a delayed restart */
        upnp_restart();
    }

    self->upnp_mode_active = UPNP_MODE_NONE;

    self->upnp_enabled = false;

    return retval;
}

bool inet_upnp_set(inet_upnp_t *self, enum inet_upnp_mode mode)
{
    self->upnp_mode_inactive = mode;

    return true;
}

bool inet_upnp_get(inet_upnp_t *self, enum inet_upnp_mode *mode)
{
    *mode = self->upnp_mode_active;

    return true;
}

/**
 * Global restart of the UPNP service
 */
void __upnp_restart(void)
{
    inet_upnp_t *upnp;
    ds_dlist_iter_t iter;

    FILE *fconf = NULL;
    bool retval = false;

    const char *ext_if = NULL;
    const char *int_if = NULL;

    LOG(INFO, "miniupnp: daemon restart...\n");

    if (!daemon_stop(&upnp_process))
    {
        LOG(WARN, "miniupnp: Error stopping the UPnP process.");
    }

    /* No UPnP configuration, just exit */
    if (ds_dlist_is_empty(&upnp_list))
    {
        retval = true;
        goto exit;
    }

    /*
     * Scan the list of registered UPnP configurations and extract the external and internal interfaces
     * Ideally, there should be only two interfaces on the list -- one internal and one external.
     */
    for (upnp = ds_dlist_ifirst(&iter, &upnp_list);
            upnp != NULL;
            upnp = ds_dlist_inext(&iter))
    {
        if (upnp->upnp_mode_active == UPNP_MODE_EXTERNAL)
        {
            if (ext_if != NULL)
            {
                LOG(WARN, "miniupnp: Multiple external interfaces selected, %s will be ignored.", upnp->upnp_ifname);
                continue;
            }

            ext_if = upnp->upnp_ifname;
        }

        if (upnp->upnp_mode_active == UPNP_MODE_INTERNAL)
        {
            if (int_if != NULL)
            {
                LOG(WARN, "miniupnp: Multiple internal interfaces selected, %s will be ignored.", upnp->upnp_ifname);
                continue;
            }

            int_if = upnp->upnp_ifname;
        }
    }

    if (ext_if == NULL || int_if == NULL)
    {
        LOG(WARN, "miniupnp: At least one internal and one external interfaces are required for UPnPD.");
        retval = true;
        goto exit;
    }

    if (mkdir(CONFIG_INET_MINIUPNPD_ETC, 0700) != 0 && errno != EEXIST)
    {
        LOG(ERR, "miniupnp: Error creating MiniUPnPD config dir: %s", CONFIG_INET_MINIUPNPD_ETC);
        goto exit;
    }

    fconf = fopen(UPNP_MINIUPNPD_CONF_PATH, "w");
    if (fconf == NULL)
    {
        LOG(ERR, "miniupnp: Error creating MiniUPnPD config file: %s", UPNP_MINIUPNPD_CONF_PATH);
        goto exit;
    }

    fprintf(fconf, "%s\n", upnp_miniupnpd_conf_header);
    fprintf(fconf, "ext_ifname=%s\n", ext_if);
    fprintf(fconf, "listening_ip=%s\n", int_if);
    fprintf(fconf, "lease_file=%s\n", UPNP_MINIUPNPD_LEASES_PATH);
    fflush(fconf);

    if (!daemon_start(&upnp_process))
    {
        LOG(ERR, "miniupnp: Error starting MiniUPnPD.");
        goto exit;
    }

    retval = true;

exit:
    if (fconf != NULL) fclose(fconf);

    if (!retval)
    {
        /* Error dispatch */
    }
}

void __upnp_restart_debounce(struct ev_loop *loop, ev_debounce *ev, int revent)
{
    __upnp_restart();
}

static void upnp_restart(void)
{
    /* Do a delayed restart */
    ev_debounce_start(EV_DEFAULT, &upnp_debounce);
}

