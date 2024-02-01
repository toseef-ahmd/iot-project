APPLICATION = sensors
BOARD ?= iotlab-m3
RIOTBASE ?= $(CURDIR)/../RIOT
# Increase from default for confirmable block2 follow-on requests
GCOAP_RESEND_BUFS_MAX ?= 2
CFLAGS += -DCONFIG_GCOAP_RESEND_BUFS_MAX=$(GCOAP_RESEND_BUFS_MAX)

# Include packages that pull up and auto-init the link layer.
# NOTE: 6LoWPAN will be included if IEEE802.15.4 devices are present
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
# Specify the mandatory networking modules
USEMODULE += gnrc_ipv6_default
USEMODULE += gcoap
# Additional networking modules that can be dropped if not needed
USEMODULE += gnrc_icmpv6_echo

# Required by gcoap example
USEMODULE += od
USEMODULE += fmt

# Import other module
USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps
USEMODULE += ztimer_msec
USEMODULE += lps331ap
USEMODULE += lsm303dlhc
USEMODULE += l3g4200d
USEMODULE += isl29020
DEVELHELP ?= 1

include $(RIOTBASE)/Makefile.include