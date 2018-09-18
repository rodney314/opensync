# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

##############################################################################
#
# Networking Configuration and Statistics Library
#
##############################################################################
UNIT_NAME := inet
#
# Template type:
UNIT_TYPE := LIB

UNIT_SRC := src/inet_base.c
UNIT_SRC += src/inet_unit.c
UNIT_SRC += src/inet_target.c
# XXX Possibly remove this it's not being used anywhere
UNIT_SRC += src/linux/inet_iflist.c

UNIT_EXPORT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS := $(UNIT_EXPORT_CFLAGS)
UNIT_CFLAGS += -DWAR_GRE_MAC

UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/ds
UNIT_DEPS += src/lib/const
UNIT_DEPS += src/lib/evx
UNIT_DEPS += src/lib/daemon
UNIT_DEPS += src/lib/execsh
UNIT_DEPS += src/lib/read_until
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/kconfig

UNIT_DEPS_CFLAGS += src/lib/log
UNIT_DEPS_CFLAGS += src/lib/target

ifdef CONFIG_USE_KCONFIG
#
# Kconfig based configuration
#
$(eval $(if $(CONFIG_INET_ETH_LINUX),       UNIT_SRC += src/linux/inet_eth.c))
$(eval $(if $(CONFIG_INET_VIF_LINUX),       UNIT_SRC += src/linux/inet_vif.c))
$(eval $(if $(CONFIG_INET_GRE_LINUX),       UNIT_SRC += src/linux/inet_gre.c))

$(eval $(if $(CONFIG_INET_FW_NULL),         UNIT_SRC += src/null/inet_fw_null.c))
$(eval $(if $(CONFIG_INET_FW_IPTABLES),     UNIT_SRC += src/linux/inet_fw_iptables.c))

$(eval $(if $(CONFIG_INET_DHCPS_NULL),      UNIT_SRC += src/null/inet_dhcps_null.c))
$(eval $(if $(CONFIG_INET_DHCPS_DNSMASQ),   UNIT_SRC += src/linux/inet_dhcps_dnsmasq.c))

$(eval $(if $(CONFIG_INET_DHCPC_NULL),      UNIT_SRC += src/null/inet_dhcpc_null.c))
$(eval $(if $(CONFIG_INET_DHCPC_UDHCPC),    UNIT_SRC += src/linux/inet_dhcpc_udhcpc.c))

$(eval $(if $(CONFIG_INET_UPNP_NULL),       UNIT_SRC += src/null/inet_upnp_null.c))
$(eval $(if $(CONFIG_INET_UPNP_MINIUPNPD),  UNIT_SRC += src/linux/inet_upnp_miniupnpd.c))

$(eval $(if $(CONFIG_INET_DNS_NULL),        UNIT_SRC += src/null/inet_dns_null.c))
$(eval $(if $(CONFIG_INET_DNS_RESOLVCONF),  UNIT_SRC += src/linux/inet_dns_resolv.c))

$(eval $(if $(CONFIG_INET_DHSNIFF_NULL),    UNIT_SRC += src/null/inet_dhsnif_null.c))
$(eval $(if $(CONFIG_INET_DHSNIFF_PCAP),    UNIT_SRC += src/linux/inet_dhsnif_pcap.c))
else
#
# Legacy "configuration"
#
UNIT_SRC += src/linux/inet_eth.c
UNIT_SRC += src/linux/inet_vif.c
UNIT_SRC += src/linux/inet_gre.c

UNIT_SRC += src/linux/inet_fw_iptables.c
UNIT_SRC += src/linux/inet_dhcps_dnsmasq.c
UNIT_SRC += src/linux/inet_dhcpc_udhcpc.c
UNIT_SRC += src/linux/inet_upnp_miniupnpd.c
UNIT_SRC += src/linux/inet_dns_resolv.c

# Let it soak on kconfig-enabled platforms before we enable this for "default" platforms
#UNIT_SRC += src/linux/inet_dhsnif_pcap.c
UNIT_SRC += src/null/inet_dhsnif_null.c
endif

