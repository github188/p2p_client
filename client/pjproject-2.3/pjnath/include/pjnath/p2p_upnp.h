#ifndef __PJNATH_P2P_UPNP_H__
#define __PJNATH_P2P_UPNP_H__

#include <pjlib.h>

#define UPNP_ITEM_RESERVE_COUNT 3

PJ_BEGIN_DECL

pj_status_t create_p2p_upnp();

pj_bool_t malloc_p2p_upnp_port(pj_uint16_t* port);

void free_p2p_upnp_port(pj_uint16_t port);

void destroy_p2p_upnp();

PJ_END_DECL

#endif