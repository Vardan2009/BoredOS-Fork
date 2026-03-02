#ifndef E1000_NETIF_H
#define E1000_NETIF_H

#include "lwip/netif.h"

err_t e1000_netif_init(struct netif *netif);
void e1000_netif_poll(struct netif *netif);

#endif
