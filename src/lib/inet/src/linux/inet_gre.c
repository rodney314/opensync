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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>

#include <string.h>

#include "log.h"
#include "util.h"

#include "inet_unit.h"

#include "inet.h"
#include "inet_base.h"
#include "inet_gre.h"

#include "execsh.h"

/*
 * ===========================================================================
 *  Initialization
 * ===========================================================================
 */
inet_t *inet_gre_new(const char *ifname)
{
    inet_gre_t *self = NULL;

    self = malloc(sizeof(*self));
    if (self == NULL)
    {
        goto error;
    }

    if (!inet_gre_init(self, ifname))
    {
        LOG(ERR, "inet_vif: %s: Failed to initialize interface instance.", ifname);
        goto error;
    }

    return (inet_t *)self;

 error:
    if (self != NULL) free(self);
    return NULL;
}

bool inet_gre_init(inet_gre_t *self, const char *ifname)
{
    if (!inet_eth_init(&self->eth, ifname))
    {
        LOG(ERR, "inet_gre: %s: Failed to instantiate class, inet_eth_init() failed.", ifname);
        return false;
    }

    self->inet.in_ip4tunnel_set_fn = inet_gre_ip4tunnel_set;
    self->base.in_service_commit_fn = inet_gre_service_commit;

    return true;
}

/*
 * ===========================================================================
 *  IPv4 Tunnel functions
 * ===========================================================================
 */
bool inet_gre_ip4tunnel_set(inet_t *super, const char *parent, inet_ip4addr_t laddr, inet_ip4addr_t raddr)
{
    inet_gre_t *self = (inet_gre_t *)super;

    if (parent == NULL) parent = "";

    if (strcmp(parent, self->in_ifparent) == 0 &&
            inet_ip4addr_cmp(&self->in_local_addr, &laddr) == 0 &&
            inet_ip4addr_cmp(&self->in_remote_addr, &raddr) == 0)
    {
        return true;
    }

    if (strscpy(self->in_ifparent, parent, sizeof(self->in_ifparent)) < 0)
    {
        LOG(ERR, "inet_gre: %s: Parent interface name too long: %s.",
                self->inet.in_ifname,
                parent);
        return false;
    }

    self->in_local_addr = laddr;
    self->in_remote_addr = raddr;

    /* Interface must be recreated, therefore restart the top service */
    return inet_unit_restart(self->base.in_units, INET_BASE_INTERFACE, false);
}

/*
 * ===========================================================================
 *  Commit and start/stop services
 * ===========================================================================
 */

/*
 * $1 - interface name
 * $2 - parent interface name
 * $3 - local address
 * $4 - remote address
 */
static char gre_create_gretap[] =
_S(
    [ -e "/sys/class/net/$1" ] && ip link del "$1";
    ip link add "$1" type gretap local "$3" remote "$4" dev "$2" tos 1;
#ifdef WAR_GRE_MAC
    /* Set the same MAC address for GRE as WiFI STA interface */
    [ -z "$(echo $1 | grep g-)" ] || ifconfig "$1" hw ether "$(cat /sys/class/net/$2/address)";
#endif
);

/*
 * $1 - interface name, always return success
 */
static char gre_delete_gretap[] =
_S(
    [ -e "/sys/class/net/$1" ] && ip link del "$1"
);


/**
 * Create/destroy the GRETAP interface
 */
bool inet_gre_interface_start(inet_gre_t *self, bool enable)
{
    int status;
    char slocal_addr[C_IP4ADDR_LEN];
    char sremote_addr[C_IP4ADDR_LEN];

    if (enable)
    {
        if (self->in_ifparent[0] == '\0') 
        {
            LOG(INFO, "inet_gre: %s: No parent interface was specified.", self->inet.in_ifname);
            return false;
        }

        if (INET_IP4ADDR_IS_ANY(self->in_local_addr))
        {
            LOG(INFO, "inet_gre: %s: No local address was specified.", self->inet.in_ifname);
            return false;
        }

        if (INET_IP4ADDR_IS_ANY(self->in_remote_addr))
        {
            LOG(INFO, "inet_gre: %s: No remote address was specified.", self->inet.in_ifname);
            return false;
        }

        snprintf(slocal_addr, sizeof(slocal_addr), PRI(inet_ip4addr_t), FMT(inet_ip4addr_t, self->in_local_addr));
        snprintf(sremote_addr, sizeof(sremote_addr), PRI(inet_ip4addr_t), FMT(inet_ip4addr_t, self->in_remote_addr));

        status = execsh_log(LOG_SEVERITY_INFO, gre_create_gretap,
                self->inet.in_ifname,
                self->in_ifparent,
                slocal_addr,
                sremote_addr);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_gre: %s: Error creating GRETAP interface.", self->inet.in_ifname);
            return false;
        }

        LOG(INFO, "inet_gre: %s: GRETAP interface was successfully created.", self->inet.in_ifname);
    }
    else
    {
        status = execsh_log(LOG_SEVERITY_INFO, gre_delete_gretap, self->inet.in_ifname);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(ERR, "inet_gre: %s: Error deleting GRETAP interface.", self->inet.in_ifname);
        }

        LOG(INFO, "inet_gre: %s: GRETAP interface was successfully deleted.", self->inet.in_ifname);
    }

    return true;
}

bool inet_gre_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable)
{
    inet_gre_t *self = (inet_gre_t *)super;

    LOG(DEBUG, "inet_gre: %s: Service %s -> %s.",
            self->inet.in_ifname,
            inet_base_service_str(srv),
            enable ? "start" : "stop");

    switch (srv)
    {
        case INET_BASE_INTERFACE:
            return inet_gre_interface_start(self, enable);

        default:
            LOG(DEBUG, "inet_eth: %s: Delegating service %s %s to inet_eth.",
                    self->inet.in_ifname,
                    inet_base_service_str(srv),
                    enable ? "start" : "stop");

            /* Delegate everything else to inet_base() */
            return inet_eth_service_commit(super, srv, enable);
    }

    return true;
}

