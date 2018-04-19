/* $Id: ice_strans.c 4728 2014-02-04 10:13:56Z bennylp $ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <pjnath/ice_strans.h>
#include <pjnath/errno.h>
#include <pj/addr_resolv.h>
#include <pj/array.h>
#include <pj/assert.h>
#include <pj/ip_helper.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/rand.h>
#include <pj/string.h>
#include <pj/compat/socket.h>
#include <pj/ctype.h> //add for p2p
#include <pjnath/p2p_global.h> //add for p2p
#define ENABLE_TRACE 0

#if defined(ENABLE_TRACE) && (ENABLE_TRACE != 0)
#  define TRACE_PKT(expr)	    PJ_LOG(5,expr)
#else
#  define TRACE_PKT(expr)
#endif


/* Transport IDs */
enum tp_type
{
    TP_NONE,
    TP_STUN,
    TP_TURN
};

/* Candidate's local preference values. This is mostly used to
 * specify preference among candidates with the same type. Since
 * we don't have the facility to specify that, we'll just set it
 * all to the same value.
 */
#if PJNATH_ICE_PRIO_STD
#   define SRFLX_PREF  65535
#   define HOST_PREF   65535
#   define RELAY_PREF  65535
#else
#   define SRFLX_PREF  0
#   define HOST_PREF   0
#   define RELAY_PREF  0
#endif


/* The candidate type preference when STUN candidate is used */
static pj_uint8_t srflx_pref_table[PJ_ICE_CAND_TYPE_MAX] =
{
#if PJNATH_ICE_PRIO_STD
    100,    /**< PJ_ICE_HOST_PREF	    */
    110,    /**< PJ_ICE_SRFLX_PREF	    */
    126,    /**< PJ_ICE_PRFLX_PREF	    */
    0	    /**< PJ_ICE_RELAYED_PREF    */
#else
    /* Keep it to 2 bits */
    1,	/**< PJ_ICE_HOST_PREF	    */
    2,	/**< PJ_ICE_SRFLX_PREF	    */
    3,	/**< PJ_ICE_PRFLX_PREF	    */
    0	/**< PJ_ICE_RELAYED_PREF    */
#endif
};


/* ICE callbacks */
static void	   on_ice_complete(pj_ice_sess *ice, pj_status_t status);
static pj_status_t ice_tx_pkt(pj_ice_sess *ice,
			      unsigned comp_id,
			      unsigned transport_id,
			      const void *pkt, pj_size_t size,
			      const pj_sockaddr_t *dst_addr,
			      unsigned dst_addr_len);
static void	   ice_rx_data(pj_ice_sess *ice,
			       unsigned comp_id,
			       unsigned transport_id,
			       void *pkt, pj_size_t size,
			       const pj_sockaddr_t *src_addr,
			       unsigned src_addr_len);


/* STUN socket callbacks */
/* Notification when incoming packet has been received. */
static pj_bool_t stun_on_rx_data(pj_stun_sock *stun_sock,
				 void *pkt,
				 unsigned pkt_len,
				 const pj_sockaddr_t *src_addr,
				 unsigned addr_len);
/* Notifification when asynchronous send operation has completed. */
static pj_bool_t stun_on_data_sent(pj_stun_sock *stun_sock,
				   pj_ioqueue_op_key_t *send_key,
				   pj_ssize_t sent);
/* Notification when the status of the STUN transport has changed. */
static pj_bool_t stun_on_status(pj_stun_sock *stun_sock,
				pj_stun_sock_op op,
				pj_status_t status);


/* TURN callbacks */
static void turn_on_rx_data(pj_turn_sock *turn_sock,
			    void *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len);
static void turn_on_state(pj_turn_sock *turn_sock, pj_turn_state_t old_state,
			  pj_turn_state_t new_state);



/* Forward decls */
static void ice_st_on_destroy(void *obj);
static void destroy_ice_st(pj_ice_strans *ice_st);
#define ice_st_perror(ice_st,msg,rc) pjnath_perror(ice_st->obj_name,msg,rc)
static void sess_init_update(pj_ice_strans *ice_st);

//add for p2p
static pj_status_t pj_ice_get_local_info(pj_ice_strans *ice_st, char buffer[], unsigned maxlen);
static void ice_start_p2p_nego(pj_ice_strans *ice_st, char* remote_info, unsigned remote_info_len);
#define TIMER_P2P_RECONNECT 1
/**
 * This structure describes an ICE stream transport component. A component
 * in ICE stream transport typically corresponds to a single socket created
 * for this component, and bound to a specific transport address. This
 * component may have multiple alias addresses, for example one alias
 * address for each interfaces in multi-homed host, another for server
 * reflexive alias, and another for relayed alias. For each transport
 * address alias, an ICE stream transport candidate (#pj_ice_sess_cand) will
 * be created, and these candidates will eventually registered to the ICE
 * session.
 */
typedef struct pj_ice_strans_comp
{
    pj_ice_strans	*ice_st;	/**< ICE stream transport.	*/
    unsigned		 comp_id;	/**< Component ID.		*/

    pj_stun_sock	*stun_sock;	/**< STUN transport.		*/
    pj_turn_sock	*turn_sock;	/**< TURN relay transport.	*/
    pj_bool_t		 turn_log_off;	/**< TURN loggin off?		*/
    unsigned		 turn_err_cnt;	/**< TURN disconnected count.	*/

    unsigned		 cand_cnt;	/**< # of candidates/aliaes.	*/
    pj_ice_sess_cand	 cand_list[PJ_ICE_ST_MAX_CAND];	/**< Cand array	*/

    unsigned		 default_cand;	/**< Default candidate.		*/

} pj_ice_strans_comp;


/**
 * This structure represents the ICE stream transport.
 */
struct pj_ice_strans
{
    char		    *obj_name;	/**< Log ID.			*/
    pj_pool_t		    *pool;	/**< Pool used by this object.	*/
    void		    *user_data;	/**< Application data.		*/
    pj_ice_strans_cfg	     cfg;	/**< Configuration.		*/
    pj_ice_strans_cb	     cb;	/**< Application callback.	*/
    pj_grp_lock_t	    *grp_lock;  /**< Group lock.		*/

    pj_ice_strans_state	     state;	/**< Session state.		*/
    pj_ice_sess		    *ice;	/**< ICE session.		*/
    pj_time_val		     start_time;/**< Time when ICE was started	*/

    unsigned		     comp_cnt;	/**< Number of components.	*/
    pj_ice_strans_comp	   **comp;	/**< Components array.		*/

    pj_timer_entry	     ka_timer;	/**< STUN keep-alive timer.	*/

    pj_bool_t		     destroy_req;/**< Destroy has been called?	*/
    pj_bool_t		     cb_called;	/**< Init error callback called?*/

	pj_timer_entry	     reconnect_timer;	/**< reconnect timer.	*///add for p2p
};

//add for p2p
static void ice_strans_p2p_exchange_info(pj_turn_sock *turn_sock,
										 pj_str_t* remote_info,
										 pj_status_t status)
{
	pj_ice_strans_comp *comp;
	comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
	if (comp == NULL) {
		/* We have disassociated ourselves from the TURN socket */
		return;
	}

	(*comp->ice_st->cb.on_p2p_exchange_info)(comp->ice_st, status);

	if(status == PJ_SUCCESS)
		ice_start_p2p_nego(comp->ice_st, remote_info->ptr, remote_info->slen);
}

static void ice_strans_on_recved_p2p_connect(pj_turn_sock *turn_sock, 
											 pj_str_t* remote_user, 
											 pj_int32_t conn_id,
											 pj_int32_t conn_flag)
{
	pj_ice_strans_comp *comp;
	comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
	if (comp == NULL) {
		/* We have disassociated ourselves from the TURN socket */
		return;
	}

	(*comp->ice_st->cb.on_recved_p2p_connect)(comp->ice_st->user_data, remote_user, conn_id, conn_flag);
}

static pj_status_t ice_strans_on_p2p_disconnect(pj_turn_sock *turn_sock)
{
	pj_ice_strans_comp *comp;
	comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
	if (comp == NULL) {
		/* We have disassociated ourselves from the TURN socket */
		return PJ_SUCCESS;
	}
	if(comp->ice_st->ice)
	{
		comp->ice_st->cb.on_ice_complete(comp->ice_st, PJ_ICE_STRANS_OP_KEEP_ALIVE, PJ_EEOF);
	}
	return PJ_SUCCESS;
}

static pj_status_t ice_strans_on_get_local_info(pj_turn_sock *turn_sock, 
												char* remote_info,
												unsigned remote_info_len,
												char* local_info, 
												unsigned local_info_maxlen)
{
	pj_ice_strans_comp *comp;
	comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
	if (comp == NULL) {
		/* We have disassociated ourselves from the TURN socket */
		PJ_LOG(4,("ice_strans_on_get_local_info",	"ice_strans_on_get_local_info comp == NULL"));
		return PJ_SUCCESS;
	}
	if(comp->ice_st->ice)
	{
		pj_status_t status = pj_ice_get_local_info(comp->ice_st, local_info, local_info_maxlen);
		if(status > 0)
			ice_start_p2p_nego(comp->ice_st, remote_info, remote_info_len);
		PJ_LOG(4,("ice_strans_on_get_local_info", "ice_strans_on_get_local_info pj_ice_get_local_info %d", status));
		return status;
	}
	else
	{
		PJ_LOG(4,("ice_strans_on_get_local_info",	"ice_strans_on_get_local_info return PJ_SUCCESS"));
		return PJ_SUCCESS;
	}
}


/* Validate configuration */
static pj_status_t pj_ice_strans_cfg_check_valid(const pj_ice_strans_cfg *cfg)
{
    pj_status_t status;

    status = pj_stun_config_check_valid(&cfg->stun_cfg);
    if (!status)
	return status;

    return PJ_SUCCESS;
}


/*
 * Initialize ICE transport configuration with default values.
 */
PJ_DEF(void) pj_ice_strans_cfg_default(pj_ice_strans_cfg *cfg)
{
    pj_bzero(cfg, sizeof(*cfg));

    pj_stun_config_init(&cfg->stun_cfg, NULL, 0, NULL, NULL);
    pj_stun_sock_cfg_default(&cfg->stun.cfg);
    pj_turn_alloc_param_default(&cfg->turn.alloc_param);
    pj_turn_sock_cfg_default(&cfg->turn.cfg);

    pj_ice_sess_options_default(&cfg->opt);

    cfg->af = pj_AF_INET();
    cfg->stun.port = PJ_STUN_PORT;
    cfg->turn.conn_type = PJ_TURN_TP_UDP;

    cfg->stun.max_host_cands = 64;
    cfg->stun.ignore_stun_error = PJ_FALSE;
}


/*
 * Copy configuration.
 */
PJ_DEF(void) pj_ice_strans_cfg_copy( pj_pool_t *pool,
				     pj_ice_strans_cfg *dst,
				     const pj_ice_strans_cfg *src)
{
    pj_memcpy(dst, src, sizeof(*src));

    if (src->stun.server.slen)
		pj_strdup_with_null(pool, &dst->stun.server, &src->stun.server);
    if (src->turn.server.slen)
		pj_strdup_with_null(pool, &dst->turn.server, &src->turn.server);
	if (src->turn.alloc_param.remote_user.slen)//add for p2p
		pj_strdup_with_null(pool, &dst->turn.alloc_param.remote_user, &src->turn.alloc_param.remote_user);
    pj_stun_auth_cred_dup(pool, &dst->turn.auth_cred,
			  &src->turn.auth_cred);
}


/*
 * Add or update TURN candidate.
 */
static pj_status_t add_update_turn(pj_ice_strans *ice_st,
				   pj_ice_strans_comp *comp)
{
    pj_turn_sock_cb turn_sock_cb;
    pj_ice_sess_cand *cand = NULL;
    unsigned i;
    pj_status_t status;

    /* Find relayed candidate in the component */
    for (i=0; i<comp->cand_cnt; ++i) {
	if (comp->cand_list[i].type == PJ_ICE_CAND_TYPE_RELAYED) {
	    cand = &comp->cand_list[i];
	    break;
	}
    }

    /* If candidate is found, invalidate it first */
    if (cand) {
	cand->status = PJ_EPENDING;

	/* Also if this component's default candidate is set to relay,
	 * move it temporarily to something else.
	 */
	if ((int)comp->default_cand == cand - comp->cand_list) {
	    /* Init to something */
	    comp->default_cand = 0;
	    /* Use srflx candidate as the default, if any */
	    for (i=0; i<comp->cand_cnt; ++i) {
		if (comp->cand_list[i].type == PJ_ICE_CAND_TYPE_SRFLX) {
		    comp->default_cand = i;
		    break;
		}
	    }
	}
    }

    /* Init TURN socket */
    pj_bzero(&turn_sock_cb, sizeof(turn_sock_cb));
    turn_sock_cb.on_rx_data = &turn_on_rx_data;
    turn_sock_cb.on_state = &turn_on_state;
	turn_sock_cb.on_p2p_connect = ice_st->cb.on_p2p_connect; //add for p2p
	turn_sock_cb.on_recved_p2p_connect = &ice_strans_on_recved_p2p_connect;
	turn_sock_cb.on_p2p_exchange_info = &ice_strans_p2p_exchange_info;
	turn_sock_cb.on_get_local_info = &ice_strans_on_get_local_info;
	turn_sock_cb.on_p2p_disconnect = &ice_strans_on_p2p_disconnect;

    /* Override with component specific QoS settings, if any */
    if (ice_st->cfg.comp[comp->comp_id-1].qos_type) {
	ice_st->cfg.turn.cfg.qos_type =
	    ice_st->cfg.comp[comp->comp_id-1].qos_type;
    }
    if (ice_st->cfg.comp[comp->comp_id-1].qos_params.flags) {
	pj_memcpy(&ice_st->cfg.turn.cfg.qos_params,
		  &ice_st->cfg.comp[comp->comp_id-1].qos_params,
		  sizeof(ice_st->cfg.turn.cfg.qos_params));
    }

    /* Override with component specific socket buffer size settings, if any */
    if (ice_st->cfg.comp[comp->comp_id-1].so_rcvbuf_size > 0) {
	ice_st->cfg.turn.cfg.so_rcvbuf_size =
	    ice_st->cfg.comp[comp->comp_id-1].so_rcvbuf_size;
    }
    if (ice_st->cfg.comp[comp->comp_id-1].so_sndbuf_size > 0) {
	ice_st->cfg.turn.cfg.so_sndbuf_size =
	    ice_st->cfg.comp[comp->comp_id-1].so_sndbuf_size;
    }

    /* Create the TURN transport */
    status = pj_turn_sock_create(&ice_st->cfg.stun_cfg, ice_st->cfg.af,
				 ice_st->cfg.turn.conn_type,
				 &turn_sock_cb, &ice_st->cfg.turn.cfg,
				 comp, &comp->turn_sock);
    if (status != PJ_SUCCESS) {
	return status;
    }

    /* Add pending job */
    ///sess_add_ref(ice_st);

    /* Start allocation */
    status=pj_turn_sock_alloc(comp->turn_sock,
			      &ice_st->cfg.turn.server,
			      ice_st->cfg.turn.port,
			      ice_st->cfg.resolver,
			      &ice_st->cfg.turn.auth_cred,
			      &ice_st->cfg.turn.alloc_param);
    if (status != PJ_SUCCESS) {
	///sess_dec_ref(ice_st);
	return status;
    }

    /* Add relayed candidate with pending status if there's no existing one */
    if (cand == NULL) {
	cand = &comp->cand_list[comp->cand_cnt++];
	cand->type = PJ_ICE_CAND_TYPE_RELAYED;
	cand->status = PJ_EPENDING;
	cand->local_pref = RELAY_PREF;
	cand->transport_id = TP_TURN;
	cand->comp_id = (pj_uint8_t) comp->comp_id;
    }

    PJ_LOG(4,(ice_st->obj_name,
		  "Comp %d: TURN relay candidate waiting for allocation",
		  comp->comp_id));

    return PJ_SUCCESS;
}


/*
 * Create the component.
 */
static pj_status_t create_comp(pj_ice_strans *ice_st, unsigned comp_id)
{
    pj_ice_strans_comp *comp = NULL;
    pj_status_t status;

    /* Verify arguments */
    PJ_ASSERT_RETURN(ice_st && comp_id, PJ_EINVAL);

    /* Check that component ID present */
    PJ_ASSERT_RETURN(comp_id <= ice_st->comp_cnt, PJNATH_EICEINCOMPID);

    /* Create component */
    comp = PJ_POOL_ZALLOC_T(ice_st->pool, pj_ice_strans_comp);
    comp->ice_st = ice_st;
    comp->comp_id = comp_id;

    ice_st->comp[comp_id-1] = comp;

    /* Initialize default candidate */
    comp->default_cand = 0;

    /* Create STUN transport if configured */
    if (ice_st->cfg.stun.server.slen || ice_st->cfg.stun.max_host_cands) {
	pj_stun_sock_cb stun_sock_cb;
	pj_ice_sess_cand *cand;

	pj_bzero(&stun_sock_cb, sizeof(stun_sock_cb));
	stun_sock_cb.on_rx_data = &stun_on_rx_data;
	stun_sock_cb.on_status = &stun_on_status;
	stun_sock_cb.on_data_sent = &stun_on_data_sent;

	/* Override component specific QoS settings, if any */
	if (ice_st->cfg.comp[comp_id-1].qos_type) {
	    ice_st->cfg.stun.cfg.qos_type =
		ice_st->cfg.comp[comp_id-1].qos_type;
	}
	if (ice_st->cfg.comp[comp_id-1].qos_params.flags) {
	    pj_memcpy(&ice_st->cfg.stun.cfg.qos_params,
		      &ice_st->cfg.comp[comp_id-1].qos_params,
		      sizeof(ice_st->cfg.stun.cfg.qos_params));
	}

	/* Override component specific socket buffer size settings, if any */
	if (ice_st->cfg.comp[comp_id-1].so_rcvbuf_size > 0) {
	    ice_st->cfg.stun.cfg.so_rcvbuf_size =
		ice_st->cfg.comp[comp_id-1].so_rcvbuf_size;
	}
	if (ice_st->cfg.comp[comp_id-1].so_sndbuf_size > 0) {
	    ice_st->cfg.stun.cfg.so_sndbuf_size =
		ice_st->cfg.comp[comp_id-1].so_sndbuf_size;
	}

	/* Create the STUN transport */
	status = pj_stun_sock_create(&ice_st->cfg.stun_cfg, NULL,
				     ice_st->cfg.af, &stun_sock_cb,
				     &ice_st->cfg.stun.cfg,
				     comp, &comp->stun_sock);
	if (status != PJ_SUCCESS)
	    return status;

	/* Start STUN Binding resolution and add srflx candidate
	 * only if server is set
	 */
	if (ice_st->cfg.stun.server.slen) {
	    pj_stun_sock_info stun_sock_info;

	    /* Add pending job */
	    ///sess_add_ref(ice_st);

	    PJ_LOG(4,(ice_st->obj_name,
		      "Comp %d: srflx candidate starts Binding discovery",
		      comp_id));

	    pj_log_push_indent();

	    /* Start Binding resolution */
	    status = pj_stun_sock_start(comp->stun_sock,
					&ice_st->cfg.stun.server,
					ice_st->cfg.stun.port,
					ice_st->cfg.resolver);
	    if (status != PJ_SUCCESS) {
		///sess_dec_ref(ice_st);
		pj_log_pop_indent();
		return status;
	    }

	    /* Enumerate addresses */
	    status = pj_stun_sock_get_info(comp->stun_sock, &stun_sock_info);
	    if (status != PJ_SUCCESS) {
		///sess_dec_ref(ice_st);
		pj_log_pop_indent();
		return status;
	    }

	    /* Add srflx candidate with pending status. */
	    cand = &comp->cand_list[comp->cand_cnt++];
	    cand->type = PJ_ICE_CAND_TYPE_SRFLX;
	    cand->status = PJ_EPENDING;
	    cand->local_pref = SRFLX_PREF;
	    cand->transport_id = TP_STUN;
	    cand->comp_id = (pj_uint8_t) comp_id;
	    pj_sockaddr_cp(&cand->base_addr, &stun_sock_info.aliases[0]);
	    pj_sockaddr_cp(&cand->rel_addr, &cand->base_addr);
	    pj_ice_calc_foundation(ice_st->pool, &cand->foundation,
				   cand->type, &cand->base_addr);

	    /* Set default candidate to srflx */
	    comp->default_cand = (unsigned)(cand - comp->cand_list);

	    pj_log_pop_indent();
	}

	/* Add local addresses to host candidates, unless max_host_cands
	 * is set to zero.
	 */
	if (ice_st->cfg.stun.max_host_cands) {
	    pj_stun_sock_info stun_sock_info;
	    unsigned i;

	    /* Enumerate addresses */
	    status = pj_stun_sock_get_info(comp->stun_sock, &stun_sock_info);
	    if (status != PJ_SUCCESS)
		return status;

	    for (i=0; i<stun_sock_info.alias_cnt &&
		      i<ice_st->cfg.stun.max_host_cands; ++i)
	    {
		char addrinfo[PJ_INET6_ADDRSTRLEN+10];
		const pj_sockaddr *addr = &stun_sock_info.aliases[i];

		/* Leave one candidate for relay */
		if (comp->cand_cnt >= PJ_ICE_ST_MAX_CAND-1) {
		    PJ_LOG(4,(ice_st->obj_name, "Too many host candidates"));
		    break;
		}

		/* Ignore loopback addresses unless cfg->stun.loop_addr
		 * is set
		 */
		if ((pj_ntohl(addr->ipv4.sin_addr.s_addr)>>24)==127) {
		    if (ice_st->cfg.stun.loop_addr==PJ_FALSE)
			continue;
		}

		cand = &comp->cand_list[comp->cand_cnt++];

		cand->type = PJ_ICE_CAND_TYPE_HOST;
		cand->status = PJ_SUCCESS;
		cand->local_pref = HOST_PREF;
		cand->transport_id = TP_STUN;
		cand->comp_id = (pj_uint8_t) comp_id;
		pj_sockaddr_cp(&cand->addr, addr);
		pj_sockaddr_cp(&cand->base_addr, addr);
		pj_bzero(&cand->rel_addr, sizeof(cand->rel_addr));
		pj_ice_calc_foundation(ice_st->pool, &cand->foundation,
				       cand->type, &cand->base_addr);

		PJ_LOG(4,(ice_st->obj_name,
			  "Comp %d: host candidate %s added",
			  comp_id, pj_sockaddr_print(&cand->addr, addrinfo,
						     sizeof(addrinfo), 3)));
	    }
	}
    }

    /* Create TURN relay if configured. */
    if (ice_st->cfg.turn.server.slen) {
	add_update_turn(ice_st, comp);
    }

    /* It's possible that we end up without any candidates */
    if (comp->cand_cnt == 0) {
	PJ_LOG(4,(ice_st->obj_name,
		  "Error: no candidate is created due to settings"));
	return PJ_EINVAL;
    }

    return PJ_SUCCESS;
}

//add for p2p
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	pj_ice_strans *ice_st = (pj_ice_strans*)e->user_data;
	PJ_UNUSED_ARG(th);

	if(e->id == TIMER_P2P_RECONNECT)
	{
		if (ice_st->destroy_req) 
			return;

		pj_grp_lock_acquire(ice_st->grp_lock);
		if (ice_st->destroy_req) {
			pj_grp_lock_release(ice_st->grp_lock);
			return;
		}

		add_update_turn(ice_st, ice_st->comp[0]);

		pj_grp_lock_release(ice_st->grp_lock);
	}
}

/*
 * Create ICE stream transport
 */
PJ_DEF(pj_status_t) pj_ice_strans_create( const char *name,
					  const pj_ice_strans_cfg *cfg,
					  unsigned comp_cnt,
					  void *user_data,
					  const pj_ice_strans_cb *cb,
					  pj_ice_strans **p_ice_st)
{
    pj_pool_t *pool;
    pj_ice_strans *ice_st;
    unsigned i;
    pj_status_t status;

    status = pj_ice_strans_cfg_check_valid(cfg);
    if (status != PJ_SUCCESS)
	return status;

    PJ_ASSERT_RETURN(comp_cnt && cb && p_ice_st &&
		     comp_cnt <= PJ_ICE_MAX_COMP , PJ_EINVAL);

    if (name == NULL)
	name = "ice%p";

    pool = pj_pool_create(cfg->stun_cfg.pf, name, PJNATH_POOL_LEN_ICE_STRANS,
			  PJNATH_POOL_INC_ICE_STRANS, NULL);
    ice_st = PJ_POOL_ZALLOC_T(pool, pj_ice_strans);
    ice_st->pool = pool;
    ice_st->obj_name = pool->obj_name;
    ice_st->user_data = user_data;

    PJ_LOG(4,(ice_st->obj_name,
	      "Creating ICE stream transport with %d component(s)",
	      comp_cnt));
    pj_log_push_indent();

    status = pj_grp_lock_create(pool, NULL, &ice_st->grp_lock);
    if (status != PJ_SUCCESS) {
	pj_pool_release(pool);
	pj_log_pop_indent();
	return status;
    }

    pj_grp_lock_add_ref(ice_st->grp_lock);
    pj_grp_lock_add_handler(ice_st->grp_lock, pool, ice_st,
			    &ice_st_on_destroy);

    pj_ice_strans_cfg_copy(pool, &ice_st->cfg, cfg);
    ice_st->cfg.stun.cfg.grp_lock = ice_st->grp_lock;
	if(ice_st->cfg.turn.alloc_param.is_assist == PJ_FALSE) //add for p2p,reconnect destroy turn socket
		ice_st->cfg.turn.cfg.grp_lock = ice_st->grp_lock;
    pj_memcpy(&ice_st->cb, cb, sizeof(*cb));

    ice_st->comp_cnt = comp_cnt;
    ice_st->comp = (pj_ice_strans_comp**)
		   pj_pool_calloc(pool, comp_cnt, sizeof(pj_ice_strans_comp*));

	/* Timer add for p2p*/
	pj_timer_entry_init(&ice_st->reconnect_timer, 0, ice_st, &on_timer_event);

    /* Move state to candidate gathering */
    ice_st->state = PJ_ICE_STRANS_STATE_INIT;

    /* Acquire initialization mutex to prevent callback to be
     * called before we finish initialization.
     */
    pj_grp_lock_acquire(ice_st->grp_lock);

    for (i=0; i<comp_cnt; ++i) {
	status = create_comp(ice_st, i+1);
	if (status != PJ_SUCCESS) {
	    pj_grp_lock_release(ice_st->grp_lock);
	    destroy_ice_st(ice_st);
	    pj_log_pop_indent();
	    return status;
	}
    }

    /* Done with initialization */
    pj_grp_lock_release(ice_st->grp_lock);

    PJ_LOG(4,(ice_st->obj_name, "ICE stream transport %p created", ice_st));

    *p_ice_st = ice_st;

    /* Check if all candidates are ready (this may call callback) */
    sess_init_update(ice_st);

    pj_log_pop_indent();

    return PJ_SUCCESS;
}

/* REALLY destroy ICE */
static void ice_st_on_destroy(void *obj)
{
    pj_ice_strans *ice_st = (pj_ice_strans*)obj;

    PJ_LOG(4,(ice_st->obj_name, "ICE stream transport %p destroyed", obj));

    /* Done */
    //pj_pool_release(ice_st->pool);
	delay_destroy_pool(ice_st->pool); //modify for p2p
}

/* Destroy ICE */
static void destroy_ice_st(pj_ice_strans *ice_st)
{
    unsigned i;

    PJ_LOG(5,(ice_st->obj_name, "ICE stream transport %p destroy request..",
	      ice_st));
    pj_log_push_indent();

    pj_grp_lock_acquire(ice_st->grp_lock);

    if (ice_st->destroy_req) {
	pj_grp_lock_release(ice_st->grp_lock);
	return;
    }

    ice_st->destroy_req = PJ_TRUE;

	//add for p2p
	pj_timer_heap_cancel_if_active(ice_st->cfg.stun_cfg.timer_heap, &ice_st->reconnect_timer, 0);

    /* Destroy ICE if we have ICE */
    if (ice_st->ice) {
	pj_ice_sess_destroy(ice_st->ice);
	ice_st->ice = NULL;
    }

    /* Destroy all components */
    for (i=0; i<ice_st->comp_cnt; ++i) {
	if (ice_st->comp[i]) {
	    if (ice_st->comp[i]->stun_sock) {
		pj_stun_sock_destroy(ice_st->comp[i]->stun_sock);
		ice_st->comp[i]->stun_sock = NULL;
	    }
	    if (ice_st->comp[i]->turn_sock) {
		pj_turn_sock_destroy(ice_st->comp[i]->turn_sock);
		pj_turn_sock_set_user_data(ice_st->comp[i]->turn_sock, NULL);
		ice_st->comp[i]->turn_sock = NULL;
	    }
	}
    }

    pj_grp_lock_dec_ref(ice_st->grp_lock);
    pj_grp_lock_release(ice_st->grp_lock);

    pj_log_pop_indent();
}

/* Get ICE session state. */
PJ_DEF(pj_ice_strans_state) pj_ice_strans_get_state(pj_ice_strans *ice_st)
{
    return ice_st->state;
}

/* State string */
PJ_DEF(const char*) pj_ice_strans_state_name(pj_ice_strans_state state)
{
    const char *names[] = {
	"Null",
	"Candidate Gathering",
	"Candidate Gathering Complete",
	"Session Initialized",
	"Negotiation In Progress",
	"Negotiation Success",
	"Negotiation Failed"
    };

    PJ_ASSERT_RETURN(state <= PJ_ICE_STRANS_STATE_FAILED, "???");
    return names[state];
}

/* Notification about failure */
static void sess_fail(pj_ice_strans *ice_st, pj_ice_strans_op op,
		      const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(4,(ice_st->obj_name, "%s: %s", title, errmsg));
    pj_log_push_indent();

    if (op==PJ_ICE_STRANS_OP_INIT && ice_st->cb_called) {
	pj_log_pop_indent();
	return;
    }

    ice_st->cb_called = PJ_TRUE;

    if (ice_st->cb.on_ice_complete)
	(*ice_st->cb.on_ice_complete)(ice_st, op, status);

    pj_log_pop_indent();
}

/* Update initialization status */
static void sess_init_update(pj_ice_strans *ice_st)
{
    unsigned i;

    /* Ignore if init callback has been called */
    if (ice_st->cb_called)
	return;

    /* Notify application when all candidates have been gathered */
    for (i=0; i<ice_st->comp_cnt; ++i) {
	unsigned j;
	pj_ice_strans_comp *comp = ice_st->comp[i];

	for (j=0; j<comp->cand_cnt; ++j) {
	    pj_ice_sess_cand *cand = &comp->cand_list[j];

	    if (cand->status == PJ_EPENDING)
		return;
	}
    }

    /* All candidates have been gathered */
    ice_st->cb_called = PJ_TRUE;
    ice_st->state = PJ_ICE_STRANS_STATE_READY;
    if (ice_st->cb.on_ice_complete)
	(*ice_st->cb.on_ice_complete)(ice_st, PJ_ICE_STRANS_OP_INIT,
				      PJ_SUCCESS);
}

/*
 * Destroy ICE stream transport.
 */
PJ_DEF(pj_status_t) pj_ice_strans_destroy(pj_ice_strans *ice_st)
{
    destroy_ice_st(ice_st);
    return PJ_SUCCESS;
}


/*
 * Get user data
 */
PJ_DEF(void*) pj_ice_strans_get_user_data(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st, NULL);
    return ice_st->user_data;
}


/*
 * Get the value of various options of the ICE stream transport.
 */
PJ_DEF(pj_status_t) pj_ice_strans_get_options( pj_ice_strans *ice_st,
					       pj_ice_sess_options *opt)
{
    PJ_ASSERT_RETURN(ice_st && opt, PJ_EINVAL);
    pj_memcpy(opt, &ice_st->cfg.opt, sizeof(*opt));
    return PJ_SUCCESS;
}

/*
 * Specify various options for this ICE stream transport.
 */
PJ_DEF(pj_status_t) pj_ice_strans_set_options(pj_ice_strans *ice_st,
					      const pj_ice_sess_options *opt)
{
    PJ_ASSERT_RETURN(ice_st && opt, PJ_EINVAL);
    pj_memcpy(&ice_st->cfg.opt, opt, sizeof(*opt));
    if (ice_st->ice)
	pj_ice_sess_set_options(ice_st->ice, &ice_st->cfg.opt);
    return PJ_SUCCESS;
}

/**
 * Get the group lock for this ICE stream transport.
 */
PJ_DEF(pj_grp_lock_t *) pj_ice_strans_get_grp_lock(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st, NULL);
    return ice_st->grp_lock;
}

/*
 * Create ICE!
 */
PJ_DEF(pj_status_t) pj_ice_strans_init_ice(pj_ice_strans *ice_st,
					   pj_ice_sess_role role,
					   const pj_str_t *local_ufrag,
					   const pj_str_t *local_passwd)
{
    pj_status_t status;
    unsigned i;
    pj_ice_sess_cb ice_cb;
    //const pj_uint8_t srflx_prio[4] = { 100, 126, 110, 0 };

    /* Check arguments */
    PJ_ASSERT_RETURN(ice_st, PJ_EINVAL);
    /* Must not have ICE */
    PJ_ASSERT_RETURN(ice_st->ice == NULL, PJ_EINVALIDOP);
    /* Components must have been created */
    PJ_ASSERT_RETURN(ice_st->comp[0] != NULL, PJ_EINVALIDOP);

    /* Init callback */
    pj_bzero(&ice_cb, sizeof(ice_cb));
    ice_cb.on_ice_complete = &on_ice_complete;
    ice_cb.on_rx_data = &ice_rx_data;
    ice_cb.on_tx_pkt = &ice_tx_pkt;

    /* Create! */
    status = pj_ice_sess_create(&ice_st->cfg.stun_cfg, ice_st->obj_name, role,
			        ice_st->comp_cnt, &ice_cb,
			        local_ufrag, local_passwd,
			        ice_st->grp_lock,
					&ice_st->cfg.stun.server,
			        &ice_st->ice);
    if (status != PJ_SUCCESS)
	return status;

    /* Associate user data */
    ice_st->ice->user_data = (void*)ice_st;

    /* Set options */
    pj_ice_sess_set_options(ice_st->ice, &ice_st->cfg.opt);

    /* If default candidate for components are SRFLX one, upload a custom
     * type priority to ICE session so that SRFLX candidates will get
     * checked first.
     */
    if (ice_st->comp[0]->default_cand >= 0 &&
	ice_st->comp[0]->cand_list[ice_st->comp[0]->default_cand].type
	    == PJ_ICE_CAND_TYPE_SRFLX)
    {
	pj_ice_sess_set_prefs(ice_st->ice, srflx_pref_table);
    }

    /* Add components/candidates */
    for (i=0; i<ice_st->comp_cnt; ++i) {
	unsigned j;
	pj_ice_strans_comp *comp = ice_st->comp[i];

	/* Re-enable logging for Send/Data indications */
	if (comp->turn_sock) {
	    PJ_LOG(5,(ice_st->obj_name,
		      "Disabling STUN Indication logging for "
		      "component %d", i+1));
	    pj_turn_sock_set_log(comp->turn_sock, 0xFFFF);
	    comp->turn_log_off = PJ_FALSE;
	}

	for (j=0; j<comp->cand_cnt; ++j) {
	    pj_ice_sess_cand *cand = &comp->cand_list[j];
	    unsigned ice_cand_id;

	    /* Skip if candidate is not ready */
	    if (cand->status != PJ_SUCCESS) {
		PJ_LOG(5,(ice_st->obj_name,
			  "Candidate %d of comp %d is not added (pending)",
			  j, i));
		continue;
	    }

	    /* Must have address */
	    pj_assert(pj_sockaddr_has_addr(&cand->addr));

	    /* Add the candidate */
	    status = pj_ice_sess_add_cand(ice_st->ice, comp->comp_id,
					  cand->transport_id, cand->type,
					  cand->local_pref,
					  &cand->foundation, &cand->addr,
					  &cand->base_addr,  &cand->rel_addr,
					  pj_sockaddr_get_len(&cand->addr),
					  (unsigned*)&ice_cand_id);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}
    }

    /* ICE session is ready for negotiation */
    ice_st->state = PJ_ICE_STRANS_STATE_SESS_READY;

    return PJ_SUCCESS;

on_error:
    pj_ice_strans_stop_ice(ice_st);
    return status;
}

/*
 * Check if the ICE stream transport has the ICE session created.
 */
PJ_DEF(pj_bool_t) pj_ice_strans_has_sess(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st, PJ_FALSE);
    return ice_st->ice != NULL;
}

/*
 * Check if ICE negotiation is still running.
 */
PJ_DEF(pj_bool_t) pj_ice_strans_sess_is_running(pj_ice_strans *ice_st)
{
    return ice_st && ice_st->ice && ice_st->ice->rcand_cnt &&
	   !pj_ice_strans_sess_is_complete(ice_st);
}


/*
 * Check if ICE negotiation has completed.
 */
PJ_DEF(pj_bool_t) pj_ice_strans_sess_is_complete(pj_ice_strans *ice_st)
{
    return ice_st && ice_st->ice && ice_st->ice->is_complete;
}


/*
 * Get the current/running component count.
 */
PJ_DEF(unsigned) pj_ice_strans_get_running_comp_cnt(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st, PJ_EINVAL);

    if (ice_st->ice && ice_st->ice->rcand_cnt) {
	return ice_st->ice->comp_cnt;
    } else {
	return ice_st->comp_cnt;
    }
}


/*
 * Get the ICE username fragment and password of the ICE session.
 */
PJ_DEF(pj_status_t) pj_ice_strans_get_ufrag_pwd( pj_ice_strans *ice_st,
						 pj_str_t *loc_ufrag,
						 pj_str_t *loc_pwd,
						 pj_str_t *rem_ufrag,
						 pj_str_t *rem_pwd)
{
    PJ_ASSERT_RETURN(ice_st && ice_st->ice, PJ_EINVALIDOP);

    if (loc_ufrag) *loc_ufrag = ice_st->ice->rx_ufrag;
    if (loc_pwd) *loc_pwd = ice_st->ice->rx_pass;

    if (rem_ufrag || rem_pwd) {
	PJ_ASSERT_RETURN(ice_st->ice->rcand_cnt != 0, PJ_EINVALIDOP);
	if (rem_ufrag) *rem_ufrag = ice_st->ice->tx_ufrag;
	if (rem_pwd) *rem_pwd = ice_st->ice->tx_pass;
    }

    return PJ_SUCCESS;
}

/*
 * Get number of candidates
 */
PJ_DEF(unsigned) pj_ice_strans_get_cands_count(pj_ice_strans *ice_st,
					       unsigned comp_id)
{
    unsigned i, cnt;

    PJ_ASSERT_RETURN(ice_st && ice_st->ice && comp_id &&
		     comp_id <= ice_st->comp_cnt, 0);

    cnt = 0;
    for (i=0; i<ice_st->ice->lcand_cnt; ++i) {
	if (ice_st->ice->lcand[i].comp_id != comp_id)
	    continue;
	++cnt;
    }

    return cnt;
}

/*
 * Enum candidates
 */
PJ_DEF(pj_status_t) pj_ice_strans_enum_cands(pj_ice_strans *ice_st,
					     unsigned comp_id,
					     unsigned *count,
					     pj_ice_sess_cand cand[])
{
    unsigned i, cnt;

    PJ_ASSERT_RETURN(ice_st && ice_st->ice && comp_id &&
		     comp_id <= ice_st->comp_cnt && count && cand, PJ_EINVAL);

    cnt = 0;
    for (i=0; i<ice_st->ice->lcand_cnt && cnt<*count; ++i) {
	if (ice_st->ice->lcand[i].comp_id != comp_id)
	    continue;
	pj_memcpy(&cand[cnt], &ice_st->ice->lcand[i],
		  sizeof(pj_ice_sess_cand));
	++cnt;
    }

    *count = cnt;
    return PJ_SUCCESS;
}

/*
 * Get default candidate.
 */
PJ_DEF(pj_status_t) pj_ice_strans_get_def_cand( pj_ice_strans *ice_st,
						unsigned comp_id,
						pj_ice_sess_cand *cand)
{
    const pj_ice_sess_check *valid_pair;

    PJ_ASSERT_RETURN(ice_st && comp_id && comp_id <= ice_st->comp_cnt &&
		      cand, PJ_EINVAL);

    valid_pair = pj_ice_strans_get_valid_pair(ice_st, comp_id);
    if (valid_pair) {
	pj_memcpy(cand, valid_pair->lcand, sizeof(pj_ice_sess_cand));
    } else {
	pj_ice_strans_comp *comp = ice_st->comp[comp_id - 1];
	pj_assert(comp->default_cand>=0 && comp->default_cand<comp->cand_cnt);
	pj_memcpy(cand, &comp->cand_list[comp->default_cand],
		  sizeof(pj_ice_sess_cand));
    }
    return PJ_SUCCESS;
}

/*
 * Get the current ICE role.
 */
PJ_DEF(pj_ice_sess_role) pj_ice_strans_get_role(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st && ice_st->ice, PJ_ICE_SESS_ROLE_UNKNOWN);
    return ice_st->ice->role;
}

/*
 * Change session role.
 */
PJ_DEF(pj_status_t) pj_ice_strans_change_role( pj_ice_strans *ice_st,
					       pj_ice_sess_role new_role)
{
    PJ_ASSERT_RETURN(ice_st && ice_st->ice, PJ_EINVALIDOP);
    return pj_ice_sess_change_role(ice_st->ice, new_role);
}

/*
 * Start ICE processing !
 */
PJ_DEF(pj_status_t) pj_ice_strans_start_ice( pj_ice_strans *ice_st,
					     const pj_str_t *rem_ufrag,
					     const pj_str_t *rem_passwd,
					     unsigned rem_cand_cnt,
					     const pj_ice_sess_cand rem_cand[])
{
    pj_status_t status;

    PJ_ASSERT_RETURN(ice_st && rem_ufrag && rem_passwd &&
		     rem_cand_cnt && rem_cand, PJ_EINVAL);

    /* Mark start time */
    pj_gettimeofday(&ice_st->start_time);

    /* Build check list */
    status = pj_ice_sess_create_check_list(ice_st->ice, rem_ufrag, rem_passwd,
					   rem_cand_cnt, rem_cand);
    if (status != PJ_SUCCESS)
	return status;

    /* If we have TURN candidate, now is the time to create the permissions */
    if (ice_st->comp[0]->turn_sock) {
	unsigned i;

	for (i=0; i<ice_st->comp_cnt; ++i) {
	    pj_ice_strans_comp *comp = ice_st->comp[i];
	    pj_sockaddr addrs[PJ_ICE_ST_MAX_CAND];
	    unsigned j, count=0;

	    /* Gather remote addresses for this component */
	    for (j=0; j<rem_cand_cnt && count<PJ_ARRAY_SIZE(addrs); ++j) {
		if (rem_cand[j].comp_id==i+1) {
		    pj_memcpy(&addrs[count++], &rem_cand[j].addr,
			      pj_sockaddr_get_len(&rem_cand[j].addr));
		}
	    }

	    if (count) {
		status = pj_turn_sock_set_perm(comp->turn_sock, count,
					       addrs, 1); //set options=1, modify by p2p,when p2p relay,
		if (status != PJ_SUCCESS) {
		    pj_ice_strans_stop_ice(ice_st);
		    return status;
		}
	    }
	}
    }

    /* Start ICE negotiation! */
    status = pj_ice_sess_start_check(ice_st->ice);
    if (status != PJ_SUCCESS) {
	pj_ice_strans_stop_ice(ice_st);
	return status;
    }

    ice_st->state = PJ_ICE_STRANS_STATE_NEGO;
    return status;
}

/*
 * Get valid pair.
 */
PJ_DEF(const pj_ice_sess_check*)
pj_ice_strans_get_valid_pair(const pj_ice_strans *ice_st,
			     unsigned comp_id)
{
    PJ_ASSERT_RETURN(ice_st && comp_id && comp_id <= ice_st->comp_cnt,
		     NULL);

    if (ice_st->ice == NULL)
	return NULL;

    return ice_st->ice->comp[comp_id-1].valid_check;
}

/*
 * Stop ICE!
 */
PJ_DEF(pj_status_t) pj_ice_strans_stop_ice(pj_ice_strans *ice_st)
{
    PJ_ASSERT_RETURN(ice_st, PJ_EINVAL);

    if (ice_st->ice) {
	pj_ice_sess_destroy(ice_st->ice);
	ice_st->ice = NULL;
    }

    ice_st->state = PJ_ICE_STRANS_STATE_INIT;
    return PJ_SUCCESS;
}

/*
 * Application wants to send outgoing packet.
 */
PJ_DEF(pj_status_t) pj_ice_strans_sendto( pj_ice_strans *ice_st,
					  unsigned comp_id,
					  const void *data,
					  pj_size_t data_len,
					  const pj_sockaddr_t *dst_addr,
					  int dst_addr_len)
{
    pj_ice_strans_comp *comp;
    unsigned def_cand;
    pj_status_t status;

    PJ_ASSERT_RETURN(ice_st && comp_id && comp_id <= ice_st->comp_cnt &&
		     dst_addr && dst_addr_len, PJ_EINVAL);

    comp = ice_st->comp[comp_id-1];

    /* Check that default candidate for the component exists */
    def_cand = comp->default_cand;
    if (def_cand >= comp->cand_cnt)
	return PJ_EINVALIDOP;

    /* If ICE is available, send data with ICE, otherwise send with the
     * default candidate selected during initialization.
     *
     * https://trac.pjsip.org/repos/ticket/1416:
     * Once ICE has failed, also send data with the default candidate.
     */
    if (ice_st->ice && ice_st->state == PJ_ICE_STRANS_STATE_RUNNING) {
	pj_turn_sock *turn_sock = comp->turn_sock; //modify for p2p,deadlock when turn_sock is Deallocating 
	if (turn_sock) {
	    pj_turn_sock_lock(turn_sock);
	}
	status = pj_ice_sess_send_data(ice_st->ice, comp_id, data, data_len);
	if (turn_sock) {
	    pj_turn_sock_unlock(turn_sock);
	}
	return status;

    } else if (comp->cand_list[def_cand].status == PJ_SUCCESS) {

	if (comp->cand_list[def_cand].type == PJ_ICE_CAND_TYPE_RELAYED) {

	    enum {
		msg_disable_ind = 0xFFFF &
				  ~(PJ_STUN_SESS_LOG_TX_IND|
				    PJ_STUN_SESS_LOG_RX_IND)
	    };

	    /* https://trac.pjsip.org/repos/ticket/1316 */
	    if (comp->turn_sock == NULL) {
		/* TURN socket error */
		return PJ_EINVALIDOP;
	    }

	    if (!comp->turn_log_off) {
		/* Disable logging for Send/Data indications */
		PJ_LOG(5,(ice_st->obj_name,
			  "Disabling STUN Indication logging for "
			  "component %d", comp->comp_id));
		pj_turn_sock_set_log(comp->turn_sock, msg_disable_ind);
		comp->turn_log_off = PJ_TRUE;
	    }

	    status = pj_turn_sock_sendto(comp->turn_sock,
					 (const pj_uint8_t*)data,
					 (unsigned)data_len,
					 dst_addr, dst_addr_len);
	    return (status==PJ_SUCCESS||status==PJ_EPENDING) ?
		    PJ_SUCCESS : status;
	} else {
	    status = pj_stun_sock_sendto(comp->stun_sock, NULL, data,
					 (unsigned)data_len, 0, dst_addr,
					 dst_addr_len);
	    return (status==PJ_SUCCESS||status==PJ_EPENDING) ?
		    PJ_SUCCESS : status;
	}

    } else
	return PJ_EINVALIDOP;
}

/*
 * Callback called by ICE session when ICE processing is complete, either
 * successfully or with failure.
 */
static void on_ice_complete(pj_ice_sess *ice, pj_status_t status)
{
    pj_ice_strans *ice_st = (pj_ice_strans*)ice->user_data;
    pj_time_val t;
    unsigned msec;

    pj_grp_lock_add_ref(ice_st->grp_lock);

    pj_gettimeofday(&t);
    PJ_TIME_VAL_SUB(t, ice_st->start_time);
    msec = PJ_TIME_VAL_MSEC(t);

    if (ice_st->cb.on_ice_complete) {
	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(4,(ice_st->obj_name,
		      "ICE negotiation failed after %ds:%03d: %s",
		      msec/1000, msec%1000, errmsg));
	} else {
	    unsigned i;
	    enum {
		msg_disable_ind = 0xFFFF &
				  ~(PJ_STUN_SESS_LOG_TX_IND|
				    PJ_STUN_SESS_LOG_RX_IND)
	    };

	    PJ_LOG(4,(ice_st->obj_name,
		      "ICE negotiation success after %ds:%03d",
		      msec/1000, msec%1000));

	    for (i=0; i<ice_st->comp_cnt; ++i) {
		const pj_ice_sess_check *check;

		check = pj_ice_strans_get_valid_pair(ice_st, i+1);
		if (check) {
		    char lip[PJ_INET6_ADDRSTRLEN+10];
		    char rip[PJ_INET6_ADDRSTRLEN+10];

		    pj_sockaddr_print(&check->lcand->addr, lip,
				      sizeof(lip), 3);
		    pj_sockaddr_print(&check->rcand->addr, rip,
				      sizeof(rip), 3);

		    if (check->lcand->transport_id == TP_TURN) {
			/* Activate channel binding for the remote address
			 * for more efficient data transfer using TURN.
			 */
			status = pj_turn_sock_bind_channel(
					ice_st->comp[i]->turn_sock,
					&check->rcand->addr,
					sizeof(check->rcand->addr));

			/* Disable logging for Send/Data indications */
			PJ_LOG(5,(ice_st->obj_name,
				  "Disabling STUN Indication logging for "
				  "component %d", i+1));
			pj_turn_sock_set_log(ice_st->comp[i]->turn_sock,
					     msg_disable_ind);
			ice_st->comp[i]->turn_log_off = PJ_TRUE;
		    }

		    PJ_LOG(4,(ice_st->obj_name, " Comp %d: "
			      "sending from %s candidate %s to "
			      "%s candidate %s",
			      i+1,
			      pj_ice_get_cand_type_name(check->lcand->type),
			      lip,
			      pj_ice_get_cand_type_name(check->rcand->type),
			      rip));

		} else {
		    PJ_LOG(4,(ice_st->obj_name,
			      "Comp %d: disabled", i+1));
		}
	    }
	}

	ice_st->state = (status==PJ_SUCCESS) ? PJ_ICE_STRANS_STATE_RUNNING :
					       PJ_ICE_STRANS_STATE_FAILED;

	pj_log_push_indent();
	(*ice_st->cb.on_ice_complete)(ice_st, PJ_ICE_STRANS_OP_NEGOTIATION,
				      status);
	pj_log_pop_indent();

    }

    pj_grp_lock_dec_ref(ice_st->grp_lock);
}

/*
 * Callback called by ICE session when it wants to send outgoing packet.
 */
static pj_status_t ice_tx_pkt(pj_ice_sess *ice,
			      unsigned comp_id,
			      unsigned transport_id,
			      const void *pkt, pj_size_t size,
			      const pj_sockaddr_t *dst_addr,
			      unsigned dst_addr_len)
{
    pj_ice_strans *ice_st = (pj_ice_strans*)ice->user_data;
    pj_ice_strans_comp *comp;
    pj_status_t status;
#if defined(ENABLE_TRACE) && (ENABLE_TRACE != 0)
    char daddr[PJ_INET6_ADDRSTRLEN];
#endif

    PJ_ASSERT_RETURN(comp_id && comp_id <= ice_st->comp_cnt, PJ_EINVAL);

    comp = ice_st->comp[comp_id-1];

    TRACE_PKT((comp->ice_st->obj_name,
	       "Component %d TX packet to %s:%d with transport %d",
	       comp_id,
	       pj_sockaddr_print(dst_addr, daddr, sizeof(addr), 0),
	       pj_sockaddr_get_port(dst_addr),
	       transport_id));

    if (transport_id == TP_TURN) {
	if (comp->turn_sock) {
	    status = pj_turn_sock_sendto(comp->turn_sock,
					 (const pj_uint8_t*)pkt,
					 (unsigned)size,
					 dst_addr, dst_addr_len);
	} else {
	    status = PJ_EINVALIDOP;
	}
    } else if (transport_id == TP_STUN) {
	status = pj_stun_sock_sendto(comp->stun_sock, NULL,
				     pkt, (unsigned)size, 0,
				     dst_addr, dst_addr_len);
    } else {
	pj_assert(!"Invalid transport ID");
	status = PJ_EINVALIDOP;
    }

    return (status==PJ_SUCCESS||status==PJ_EPENDING) ? PJ_SUCCESS : status;
}

/*
 * Callback called by ICE session when it receives application data.
 */
static void ice_rx_data(pj_ice_sess *ice,
		        unsigned comp_id,
			unsigned transport_id,
		        void *pkt, pj_size_t size,
		        const pj_sockaddr_t *src_addr,
		        unsigned src_addr_len)
{
    pj_ice_strans *ice_st = (pj_ice_strans*)ice->user_data;

    PJ_UNUSED_ARG(transport_id);

    if (ice_st->cb.on_rx_data) {
	(*ice_st->cb.on_rx_data)(ice_st, comp_id, pkt, size,
				 src_addr, src_addr_len);
    }
}

/* Notification when incoming packet has been received from
 * the STUN socket.
 */
static pj_bool_t stun_on_rx_data(pj_stun_sock *stun_sock,
				 void *pkt,
				 unsigned pkt_len,
				 const pj_sockaddr_t *src_addr,
				 unsigned addr_len)
{
    pj_ice_strans_comp *comp;
    pj_ice_strans *ice_st;
    pj_status_t status;

    comp = (pj_ice_strans_comp*) pj_stun_sock_get_user_data(stun_sock);
    if (comp == NULL) {
	/* We have disassociated ourselves from the STUN socket */
	return PJ_FALSE;
    }

    ice_st = comp->ice_st;

    pj_grp_lock_add_ref(ice_st->grp_lock);

    if (ice_st->ice == NULL) {
	/* The ICE session is gone, but we're still receiving packets.
	 * This could also happen if remote doesn't do ICE. So just
	 * report this to application.
	 */
	if (ice_st->cb.on_rx_data) {
	    (*ice_st->cb.on_rx_data)(ice_st, comp->comp_id, pkt, pkt_len,
				     src_addr, addr_len);
	}

    } else {

	/* Hand over the packet to ICE session */
	status = pj_ice_sess_on_rx_pkt(comp->ice_st->ice, comp->comp_id,
				       TP_STUN, pkt, pkt_len,
				       src_addr, addr_len);

	if (status != PJ_SUCCESS) {
	    ice_st_perror(comp->ice_st, "Error processing packet",
			  status);
	}
    }

    return pj_grp_lock_dec_ref(ice_st->grp_lock) ? PJ_FALSE : PJ_TRUE;
}

/* Notifification when asynchronous send operation to the STUN socket
 * has completed.
 */
static pj_bool_t stun_on_data_sent(pj_stun_sock *stun_sock,
				   pj_ioqueue_op_key_t *send_key,
				   pj_ssize_t sent)
{
    PJ_UNUSED_ARG(stun_sock);
    PJ_UNUSED_ARG(send_key);
    PJ_UNUSED_ARG(sent);
    return PJ_TRUE;
}

/* Notification when the status of the STUN transport has changed. */
static pj_bool_t stun_on_status(pj_stun_sock *stun_sock,
				pj_stun_sock_op op,
				pj_status_t status)
{
    pj_ice_strans_comp *comp;
    pj_ice_strans *ice_st;
    pj_ice_sess_cand *cand = NULL;
    unsigned i;

    pj_assert(status != PJ_EPENDING);

    comp = (pj_ice_strans_comp*) pj_stun_sock_get_user_data(stun_sock);
    ice_st = comp->ice_st;

    pj_grp_lock_add_ref(ice_st->grp_lock);

    /* Wait until initialization completes */
    pj_grp_lock_acquire(ice_st->grp_lock);

    /* Find the srflx cancidate */
    for (i=0; i<comp->cand_cnt; ++i) {
	if (comp->cand_list[i].type == PJ_ICE_CAND_TYPE_SRFLX) {
	    cand = &comp->cand_list[i];
	    break;
	}
    }

    pj_grp_lock_release(ice_st->grp_lock);

    /* It is possible that we don't have srflx candidate even though this
     * callback is called. This could happen when we cancel adding srflx
     * candidate due to initialization error.
     */
    if (cand == NULL) {
	return pj_grp_lock_dec_ref(ice_st->grp_lock) ? PJ_FALSE : PJ_TRUE;
    }

    switch (op) {
    case PJ_STUN_SOCK_DNS_OP:
	if (status != PJ_SUCCESS) {
	    /* May not have cand, e.g. when error during init */
	    if (cand)
		cand->status = status;
	    if (!ice_st->cfg.stun.ignore_stun_error) {
		sess_fail(ice_st, PJ_ICE_STRANS_OP_INIT,
		          "DNS resolution failed", status);
	    } else {
		PJ_LOG(4,(ice_st->obj_name,
			  "STUN error is ignored for comp %d",
			  comp->comp_id));
	    }
	}
	break;
    case PJ_STUN_SOCK_BINDING_OP:
    case PJ_STUN_SOCK_MAPPED_ADDR_CHANGE:
	if (status == PJ_SUCCESS) {
	    pj_stun_sock_info info;

	    status = pj_stun_sock_get_info(stun_sock, &info);
	    if (status == PJ_SUCCESS) {
		char ipaddr[PJ_INET6_ADDRSTRLEN+10];
		const char *op_name = (op==PJ_STUN_SOCK_BINDING_OP) ?
				    "Binding discovery complete" :
				    "srflx address changed";
		pj_bool_t dup = PJ_FALSE;

		/* Eliminate the srflx candidate if the address is
		 * equal to other (host) candidates.
		 */
		for (i=0; i<comp->cand_cnt; ++i) {
		    if (comp->cand_list[i].type == PJ_ICE_CAND_TYPE_HOST &&
			pj_sockaddr_cmp(&comp->cand_list[i].addr,
					&info.mapped_addr) == 0)
		    {
			dup = PJ_TRUE;
			break;
		    }
		}

		if (dup) {
		    /* Duplicate found, remove the srflx candidate */
		    unsigned idx = (unsigned)(cand - comp->cand_list);

		    /* Update default candidate index */
		    if (comp->default_cand > idx) {
			--comp->default_cand;
		    } else if (comp->default_cand == idx) {
			comp->default_cand = 0;
		    }

		    /* Remove srflx candidate */
		    pj_array_erase(comp->cand_list, sizeof(comp->cand_list[0]),
				   comp->cand_cnt, idx);
		    --comp->cand_cnt;
		} else {
		    /* Otherwise update the address */
		    pj_sockaddr_cp(&cand->addr, &info.mapped_addr);
		    cand->status = PJ_SUCCESS;
		}

		PJ_LOG(4,(comp->ice_st->obj_name,
			  "Comp %d: %s, "
			  "srflx address is %s",
			  comp->comp_id, op_name,
			  pj_sockaddr_print(&info.mapped_addr, ipaddr,
					     sizeof(ipaddr), 3)));

		sess_init_update(ice_st);
	    }
	}

	if (status != PJ_SUCCESS) {
	    /* May not have cand, e.g. when error during init */
	    if (cand)
		cand->status = status;
	    if (!ice_st->cfg.stun.ignore_stun_error || comp->cand_cnt==1) {
		sess_fail(ice_st, PJ_ICE_STRANS_OP_INIT,
			  "STUN binding request failed", status);
	    } else {
		PJ_LOG(4,(ice_st->obj_name,
			  "STUN error is ignored for comp %d",
			  comp->comp_id));

		if (cand) {
		    unsigned idx = (unsigned)(cand - comp->cand_list);

		    /* Update default candidate index */
		    if (comp->default_cand == idx) {
			comp->default_cand = !idx;
		    }
		}

		sess_init_update(ice_st);
	    }
	}
	break;
    case PJ_STUN_SOCK_KEEP_ALIVE_OP:
	if (status != PJ_SUCCESS) {
	    pj_assert(cand != NULL);
	    cand->status = status;
	    if (!ice_st->cfg.stun.ignore_stun_error) {
		sess_fail(ice_st, PJ_ICE_STRANS_OP_INIT,
			  "STUN keep-alive failed", status);
	    } else {
		PJ_LOG(4,(ice_st->obj_name, "STUN error is ignored"));
	    }
	}
	break;
    }

    return pj_grp_lock_dec_ref(ice_st->grp_lock)? PJ_FALSE : PJ_TRUE;
}

/* Callback when TURN socket has received a packet */
static void turn_on_rx_data(pj_turn_sock *turn_sock,
			    void *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len)
{
    pj_ice_strans_comp *comp;
    pj_status_t status;

    comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
    if (comp == NULL) {
	/* We have disassociated ourselves from the TURN socket */
	return;
    }

    pj_grp_lock_add_ref(comp->ice_st->grp_lock);

    if (comp->ice_st->ice == NULL) {
	/* The ICE session is gone, but we're still receiving packets.
	 * This could also happen if remote doesn't do ICE and application
	 * specifies TURN as the default address in SDP.
	 * So in this case just give the packet to application.
	 */
	if (comp->ice_st->cb.on_rx_data) {
	    (*comp->ice_st->cb.on_rx_data)(comp->ice_st, comp->comp_id, pkt,
					   pkt_len, peer_addr, addr_len);
	}

    } else {

	/* Hand over the packet to ICE */
	status = pj_ice_sess_on_rx_pkt(comp->ice_st->ice, comp->comp_id,
				       TP_TURN, pkt, pkt_len,
				       peer_addr, addr_len);

	if (status != PJ_SUCCESS) {
	    ice_st_perror(comp->ice_st,
			  "Error processing packet from TURN relay",
			  status);
	}
    }

    pj_grp_lock_dec_ref(comp->ice_st->grp_lock);
}


/* Callback when TURN client state has changed */
static void turn_on_state(pj_turn_sock *turn_sock, pj_turn_state_t old_state,
			  pj_turn_state_t new_state)
{
    pj_ice_strans_comp *comp;

	PJ_LOG(4,("turn_on_state", "turn_on_state begin"));

    comp = (pj_ice_strans_comp*) pj_turn_sock_get_user_data(turn_sock);
    if (comp == NULL 
		|| comp->turn_sock == NULL 
		|| comp->ice_st == NULL
		|| comp->ice_st->destroy_req) {//modify by p2p,maybe p2p_transport_destroy called
	/* Not interested in further state notification once the relay is
	 * disconnecting.
	 */
	pj_turn_sock_set_user_data(turn_sock, NULL);
	if(comp)
		comp->turn_sock = NULL;
	return;
    }

    PJ_LOG(5,(comp->ice_st->obj_name, "TURN client state changed %s --> %s",
	      pj_turn_state_name(old_state), pj_turn_state_name(new_state)));
    pj_log_push_indent();

    pj_grp_lock_add_ref(comp->ice_st->grp_lock);
	
	PJ_LOG(4,("turn_on_state", "turn_on_state pj_grp_lock_add_ref"));

    if (new_state == PJ_TURN_STATE_READY) {
	pj_turn_session_info rel_info;
	char ipaddr[PJ_INET6_ADDRSTRLEN+8];
	pj_ice_sess_cand *cand = NULL;
	unsigned i;

	comp->turn_err_cnt = 0;

	/* Get allocation info */
	pj_turn_sock_get_info(turn_sock, &rel_info);

	/* Wait until initialization completes */
	pj_grp_lock_acquire(comp->ice_st->grp_lock);

	/* Find relayed candidate in the component */
	for (i=0; i<comp->cand_cnt; ++i) {
	    if (comp->cand_list[i].type == PJ_ICE_CAND_TYPE_RELAYED) {
		cand = &comp->cand_list[i];
		break;
	    }
	}
	pj_assert(cand != NULL);

	pj_grp_lock_release(comp->ice_st->grp_lock);

	/* Update candidate */
	pj_sockaddr_cp(&cand->addr, &rel_info.relay_addr);
	pj_sockaddr_cp(&cand->base_addr, &rel_info.relay_addr);
	pj_sockaddr_cp(&cand->rel_addr, &rel_info.mapped_addr);
	pj_ice_calc_foundation(comp->ice_st->pool, &cand->foundation,
			       PJ_ICE_CAND_TYPE_RELAYED,
			       &rel_info.relay_addr);
	cand->status = PJ_SUCCESS;

	/* Set default candidate to relay */
	comp->default_cand = (unsigned)(cand - comp->cand_list);

	PJ_LOG(4,(comp->ice_st->obj_name,
		  "Comp %d: TURN allocation complete, relay address is %s",
		  comp->comp_id,
		  pj_sockaddr_print(&rel_info.relay_addr, ipaddr,
				     sizeof(ipaddr), 3)));

	sess_init_update(comp->ice_st);

    } else if (new_state >= PJ_TURN_STATE_DEALLOCATING) {
	pj_turn_session_info info;

	++comp->turn_err_cnt;

	pj_turn_sock_get_info(turn_sock, &info);

	/* Unregister ourself from the TURN relay */
	pj_turn_sock_set_user_data(turn_sock, NULL);
	comp->turn_sock = NULL;

	/* Set session to fail on error. last_status PJ_SUCCESS means normal
	 * deallocation, which should not trigger sess_fail as it may have
	 * been initiated by ICE destroy
	 */
	if (info.last_status != PJ_SUCCESS || (old_state == PJ_TURN_STATE_RESOLVED && new_state == PJ_TURN_STATE_DESTROYING) ) { //modify for p2p
		if (comp->ice_st->state < PJ_ICE_STRANS_STATE_READY) {
			if((old_state == PJ_TURN_STATE_RESOLVED && new_state == PJ_TURN_STATE_DESTROYING))
				info.last_status = PJ_ERESOLVE;
			if((PJ_STUN_SC_ALLOCATION_MISMATCH+PJNATH_ERRNO_START) == info.last_status)
			{
				pj_time_val delay = {0, 200};//reconnect by timer
				comp->ice_st->cb_called = PJ_FALSE;
				pj_timer_heap_schedule_w_grp_lock(comp->ice_st->cfg.stun_cfg.timer_heap,
					&comp->ice_st->reconnect_timer, &delay,
					TIMER_P2P_RECONNECT,
					comp->ice_st->grp_lock);
			}
			else
			{
				sess_fail(comp->ice_st, PJ_ICE_STRANS_OP_INIT,
					"TURN allocation failed", info.last_status);

				if (comp->ice_st->cfg.turn.alloc_param.is_assist)
				{
					pj_time_val delay = {5, 0};//reconnect by timer
					comp->ice_st->cb_called = PJ_FALSE;
					pj_timer_heap_schedule_w_grp_lock(comp->ice_st->cfg.stun_cfg.timer_heap,
						&comp->ice_st->reconnect_timer, &delay,
						TIMER_P2P_RECONNECT,
						comp->ice_st->grp_lock);
				}
			}
		} else if (comp->ice_st->cfg.turn.alloc_param.is_assist == PJ_FALSE) { //modify for p2p
			sess_fail(comp->ice_st, PJ_ICE_STRANS_OP_KEEP_ALIVE,
				"TURN refresh failed", info.last_status);
		} else {
			PJ_PERROR(4,(comp->ice_st->obj_name, info.last_status,
				"Comp %d: TURN allocation failed, retrying",
				comp->comp_id));

			if(info.last_status == OTHER_CLINET_KICK_OUT_ERROR){ //modify for p2p,kick out by other client
				sess_fail(comp->ice_st, PJ_ICE_STRANS_OP_KEEP_ALIVE,
					"TURN refresh failed", P2P_KICK_OUT_ERROR);
			}else 
			{
				pj_time_val delay = {5, 0};//reconnect by timer
				if (comp->turn_err_cnt <= 1)//first disconnect, reconnect immediately 
				{
					delay.sec = 0;
				}
				else if(comp->turn_err_cnt == 2)//second disconnect, call back to user,and continue try connect server
				{
					sess_fail(comp->ice_st, PJ_ICE_STRANS_OP_KEEP_ALIVE,
						"TURN refresh failed", info.last_status);
				}
				comp->ice_st->cb_called = PJ_FALSE;
				pj_timer_heap_schedule_w_grp_lock(comp->ice_st->cfg.stun_cfg.timer_heap,
					&comp->ice_st->reconnect_timer, &delay,
					TIMER_P2P_RECONNECT,
					comp->ice_st->grp_lock);
			}
		}
	}
	}

    pj_grp_lock_dec_ref(comp->ice_st->grp_lock);

    pj_log_pop_indent();
}

//add for p2p
PJ_DECL(void) pj_ice_strans_cfg_copy2(pj_pool_t *pool,
									  pj_ice_strans_cfg *dst,
									  const pj_ice_strans *ice_st)
{
	pj_ice_strans_cfg_copy(pool, dst, &ice_st->cfg);
}

PJ_DECL(pj_ice_strans_cfg*) pj_ice_strans_get_cfg(pj_ice_strans *ice_st)
{
	return &ice_st->cfg;
}

PJ_DECL(pj_status_t) pj_ice_strans_p2p_connnect(pj_ice_strans *ice_st,
												pj_str_t* remote_user,
												p2p_conn_arg *arg)
{
	if (ice_st && ice_st->state == PJ_ICE_STRANS_STATE_READY && ice_st->comp && ice_st->comp[0]->turn_sock) 
	{
		return pj_turn_sock_p2p_connnect(ice_st->comp[0]->turn_sock, 
			&ice_st->cfg.turn.auth_cred.data.static_cred.username, 
			remote_user,
			arg);
	}
	else
		return PJ_EINVALIDOP;
}
//add for p2p
#define PRINT(fmt, arg0, arg1, arg2, arg3, arg4, arg5)	    \
	printed = pj_ansi_snprintf(p, maxlen - (p-buffer),  \
	fmt, arg0, arg1, arg2, arg3, arg4, arg5); \
	if (printed <= 0) return -PJ_ETOOSMALL; \
	p += printed

/* Utility to create a=candidate SDP attribute */
static int print_cand(char buffer[], unsigned maxlen,
					  const pj_ice_sess_cand *cand)
{
	char ipaddr[PJ_INET6_ADDRSTRLEN];
	char *p = buffer;
	int printed;

	PRINT("a=candidate:%.*s %u UDP %u %s %u typ ",
		(int)cand->foundation.slen,
		cand->foundation.ptr,
		(unsigned)cand->comp_id,
		cand->prio,
		pj_sockaddr_print(&cand->addr, ipaddr, 
		sizeof(ipaddr), 0),
		(unsigned)pj_sockaddr_get_port(&cand->addr));

	PRINT("%s\n",
		pj_ice_get_cand_type_name(cand->type),
		0, 0, 0, 0, 0);

	if (p == buffer+maxlen)
		return -PJ_ETOOSMALL;

	*p = '\0';

	return p-buffer;
}

static pj_status_t pj_ice_get_local_info(pj_ice_strans *ice_st, char buffer[], unsigned maxlen)
{
	char *p = buffer;
	unsigned comp;
	int printed;
	pj_str_t local_ufrag, local_pwd;
	pj_status_t status;

	/* Write "dummy" SDP v=, o=, s=, and t= lines */
	PRINT("v=0\no=- 3414953978 3414953978 IN IP4 localhost\ns=ice\nt=0 0\n", 
		0, 0, 0, 0, 0, 0);

	/* Get ufrag and pwd from current session */
	pj_ice_strans_get_ufrag_pwd(ice_st, &local_ufrag, &local_pwd,
		NULL, NULL);

	/* Write the a=ice-ufrag and a=ice-pwd attributes */
	PRINT("a=ice-ufrag:%.*s\na=ice-pwd:%.*s\n",
		(int)local_ufrag.slen,
		local_ufrag.ptr,
		(int)local_pwd.slen,
		local_pwd.ptr, 
		0, 0);

	/* Write each component */
	for (comp=0; comp<ice_st->comp_cnt; ++comp) {
		unsigned j, cand_cnt;
		pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
		char ipaddr[PJ_INET6_ADDRSTRLEN];

		/* Get default candidate for the component */
		status = pj_ice_strans_get_def_cand(ice_st, comp+1, &cand[0]);
		if (status != PJ_SUCCESS)
			return -status;

		/* Write the default address */
		if (comp==0) {
			/* For component 1, default address is in m= and c= lines */
			PRINT("m=audio %d RTP/AVP 0\n"
				"c=IN IP4 %s\n",
				(int)pj_sockaddr_get_port(&cand[0].addr),
				pj_sockaddr_print(&cand[0].addr, ipaddr,
				sizeof(ipaddr), 0),
				0, 0, 0, 0);
		} else if (comp==1) {
			/* For component 2, default address is in a=rtcp line */
			PRINT("a=rtcp:%d IN IP4 %s\n",
				(int)pj_sockaddr_get_port(&cand[0].addr),
				pj_sockaddr_print(&cand[0].addr, ipaddr,
				sizeof(ipaddr), 0),
				0, 0, 0, 0);
		} else {
			/* For other components, we'll just invent this.. */
			PRINT("a=Xice-defcand:%d IN IP4 %s\n",
				(int)pj_sockaddr_get_port(&cand[0].addr),
				pj_sockaddr_print(&cand[0].addr, ipaddr,
				sizeof(ipaddr), 0),
				0, 0, 0, 0);
		}

		/* Enumerate all candidates for this component */
		cand_cnt = PJ_ARRAY_SIZE(cand);
		status = pj_ice_strans_enum_cands(ice_st, comp+1,
			&cand_cnt, cand);
		if (status != PJ_SUCCESS)
			return -status;

		/* And encode the candidates as SDP */
		for (j=0; j<cand_cnt; ++j) {
			if(!get_p2p_global()->enable_relay && cand[j].type == PJ_ICE_CAND_TYPE_RELAYED)
				continue;
			if(get_p2p_global()->only_relay && cand[j].type != PJ_ICE_CAND_TYPE_RELAYED)
				continue;
			printed = print_cand(p, maxlen - (p-buffer), &cand[j]);
			if (printed < 0)
				return -PJ_ETOOSMALL;
			p += printed;
		}
	}

	if (p == buffer+maxlen)
		return -PJ_ETOOSMALL;

	*p = '\0';
	return p - buffer;
}
/* Variables to store parsed remote ICE info,add for p2p */
typedef struct _remote_info
{
	char		 ufrag[80];
	char		 pwd[80];
	unsigned	 comp_cnt;
	pj_sockaddr	 def_addr[PJ_ICE_MAX_COMP];
	unsigned	 cand_cnt;
	pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
} ice_remote_info;

//static void ice_remote_info_plus(ice_remote_info* rem, pj_ice_strans *ice_st)
//{
//	if(rem->cand_cnt < PJ_ICE_ST_MAX_CAND)
//	{
//		unsigned i;
//		for(i=0; i<rem->cand_cnt; i++)
//		{
//			pj_ice_sess_cand *cand = &(rem->cand[i]);
//			if(cand->type == PJ_ICE_CAND_TYPE_SRFLX)
//			{
//				unsigned m;
//				pj_uint16_t port = 0;
//				port = pj_sockaddr_get_port(&cand->addr);
//				for(m=rem->cand_cnt; m<PJ_ICE_ST_MAX_CAND; m++)
//				{
//					pj_ice_sess_cand *cand_plus = &(rem->cand[m]);					
//					pj_memcpy(cand_plus, cand, sizeof(pj_ice_sess_cand));
//					pj_strdup(ice_st->pool, &cand->foundation, &cand_plus->foundation);
//					pj_sockaddr_set_port(&cand_plus->addr, ++port);
//					rem->cand_cnt++;
//				}
//				break;
//			}
//		}
//	}
//}
static pj_bool_t icedemo_input_remote(pj_ice_strans *ice_st, char* buffer, unsigned maxlen, ice_remote_info* rem)
{
	char linebuf[256];
	unsigned media_cnt = 0;
	unsigned comp0_port = 0;
	char     comp0_addr[80];
	pj_bool_t done = PJ_FALSE;
	char* position = buffer;
	char* end = buffer + maxlen;

	pj_bzero(rem, sizeof(*rem));

	comp0_addr[0] = '\0';

	while (!done && position < end) {
		int len;
		char *line;
		char* c = position;

		while (*c != '\n' && c < end)
			++c;

		pj_memcpy(linebuf, position, c-position);
		len = c-position;
		position = c + 1;

		linebuf[len] = '\0';
		while (len && (linebuf[len-1] == '\r' || linebuf[len-1] == '\n'))
			linebuf[--len] = '\0';

		line = linebuf;
		while (len && pj_isspace(*line))
			++line, --len;

		if (len==0)
			break;

		/* Ignore subsequent media descriptors */
		if (media_cnt > 1)
			continue;

		switch (line[0]) {
	case 'm':
		{
			int cnt;
			char media[32], portstr[32];

			++media_cnt;
			if (media_cnt > 1) {
				puts("Media line ignored");
				break;
			}

			cnt = sscanf(line+2, "%s %s RTP/", media, portstr);
			if (cnt != 2) {
				goto on_error;
			}

			comp0_port = atoi(portstr);

		}
		break;
	case 'c':
		{
			int cnt;
			char c[32], net[32], ip[80];

			cnt = sscanf(line+2, "%s %s %s", c, net, ip);
			if (cnt != 3) {
				goto on_error;
			}

			strcpy(comp0_addr, ip);
		}
		break;
	case 'a':
		{
			char *attr = strtok(line+2, ": \t\r\n");
			if (strcmp(attr, "ice-ufrag")==0) {
				strcpy(rem->ufrag, attr+strlen(attr)+1);
			} else if (strcmp(attr, "ice-pwd")==0) {
				strcpy(rem->pwd, attr+strlen(attr)+1);
			} else if (strcmp(attr, "rtcp")==0) {
				char *val = attr+strlen(attr)+1;
				int af, cnt;
				int port;
				char net[32], ip[64];
				pj_str_t tmp_addr;
				pj_status_t status;

				cnt = sscanf(val, "%d IN %s %s", &port, net, ip);
				if (cnt != 3) {
					goto on_error;
				}

				if (strchr(ip, ':'))
					af = pj_AF_INET6();
				else
					af = pj_AF_INET();

				pj_sockaddr_init(af, &rem->def_addr[1], NULL, 0);
				tmp_addr = pj_str(ip);
				status = pj_sockaddr_set_str_addr(af, &rem->def_addr[1],
					&tmp_addr);
				if (status != PJ_SUCCESS) {
					goto on_error;
				}
				pj_sockaddr_set_port(&rem->def_addr[1], (pj_uint16_t)port);

			} else if (strcmp(attr, "candidate")==0) {
				char *sdpcand = attr+strlen(attr)+1;
				int af, cnt;
				char foundation[32], transport[12], ipaddr[80], type[32];
				pj_str_t tmpaddr;
				int comp_id, prio, port;
				pj_ice_sess_cand *cand;
				pj_status_t status;

				cnt = sscanf(sdpcand, "%s %d %s %d %s %d typ %s",
					foundation,
					&comp_id,
					transport,
					&prio,
					ipaddr,
					&port,
					type);
				if (cnt != 7) {
					goto on_error;
				}

				cand = &rem->cand[rem->cand_cnt];
				pj_bzero(cand, sizeof(*cand));

				if (strcmp(type, "host")==0){
					if(get_p2p_global()->only_relay)
						continue;
					cand->type = PJ_ICE_CAND_TYPE_HOST;
				}
				else if (strcmp(type, "srflx")==0){
					if(get_p2p_global()->only_relay)
						continue;
					cand->type = PJ_ICE_CAND_TYPE_SRFLX;
				}
				else if (strcmp(type, "relay")==0){
					if(!get_p2p_global()->enable_relay)
						continue;
					cand->type = PJ_ICE_CAND_TYPE_RELAYED;
				}
				else {
					continue;
				}

				cand->comp_id = (pj_uint8_t)comp_id;
				pj_strdup2(ice_st->pool, &cand->foundation, foundation);
				cand->prio = prio;

				if (strchr(ipaddr, ':'))
					af = pj_AF_INET6();
				else
					af = pj_AF_INET();

				tmpaddr = pj_str(ipaddr);
				pj_sockaddr_init(af, &cand->addr, NULL, 0);
				status = pj_sockaddr_set_str_addr(af, &cand->addr, &tmpaddr);
				if (status != PJ_SUCCESS) {
					goto on_error;
				}

				pj_sockaddr_set_port(&cand->addr, (pj_uint16_t)port);

				++rem->cand_cnt;

				if (cand->comp_id > rem->comp_cnt)
					rem->comp_cnt = cand->comp_id;
			}
		}
		break;
		}
	}

	if (rem->cand_cnt==0 ||
		rem->ufrag[0]==0 ||
		rem->pwd[0]==0 ||
		rem->comp_cnt == 0)
	{
		goto on_error;
	}

	if (comp0_port==0 || comp0_addr[0]=='\0') {
		goto on_error;
	} else {
		int af;
		pj_str_t tmp_addr;
		pj_status_t status;

		if (strchr(comp0_addr, ':'))
			af = pj_AF_INET6();
		else
			af = pj_AF_INET();

		pj_sockaddr_init(af, &rem->def_addr[0], NULL, 0);
		tmp_addr = pj_str(comp0_addr);
		status = pj_sockaddr_set_str_addr(af, &rem->def_addr[0],
			&tmp_addr);
		if (status != PJ_SUCCESS) {
			goto on_error;
		}
		pj_sockaddr_set_port(&rem->def_addr[0], (pj_uint16_t)comp0_port);
		//ice_remote_info_plus(rem, ice_st);	
	}
	return PJ_TRUE;

on_error:
	pj_bzero(rem, sizeof(*rem));
	return PJ_FALSE;
}

PJ_DECL(pj_status_t) pj_ice_strans_p2p_exchange_info(pj_ice_strans *ice_st)
{
	char buffer[1024];
	pj_status_t status;
	if (ice_st && ice_st->state == PJ_ICE_STRANS_STATE_SESS_READY) 
	{		
		status = pj_ice_get_local_info(ice_st, buffer, sizeof(buffer));
		if(status < 0)
			return -status;
		return pj_turn_sock_p2p_exchange_info(ice_st->comp[0]->turn_sock, buffer);
	}
	else
		return PJ_EINVALIDOP;
}

static void ice_start_p2p_nego(pj_ice_strans *ice_st, char* remote_info, unsigned remote_info_len)
{
	ice_remote_info ri;
	pj_bool_t result;
	pj_str_t rufrag, rpwd;
	pj_status_t status;

	if (!ice_st || ice_st->state != PJ_ICE_STRANS_STATE_SESS_READY) 
		return;

	result = icedemo_input_remote(ice_st, remote_info, remote_info_len, &ri);
	if(result == PJ_FALSE)
		return;

	status = pj_ice_strans_start_ice(ice_st, 
		pj_cstr(&rufrag, ri.ufrag),
		pj_cstr(&rpwd, ri.pwd),
		ri.cand_cnt,
		ri.cand);
	if(ice_st->cb.on_ice_complete)
	{
		if(status != PJ_SUCCESS)
			(*ice_st->cb.on_ice_complete)(ice_st, PJ_ICE_STRANS_OP_NEGOTIATION, status);
		else
			(*ice_st->cb.on_ice_complete)(ice_st, PJ_ICE_STRANS_OP_NEGOTIATION_BEGIN, status);
	}
}

PJ_DECL(pj_status_t) pj_ice_strans_get_turn_sockaddr(pj_ice_strans *ice_st,
													 pj_sockaddr_t *dst_addr,
													 int *namelen)
{
	if (ice_st && ice_st->comp && ice_st->comp[0]->turn_sock) 
	{
		return pj_turn_sock_get_sockaddr(ice_st->comp[0]->turn_sock, dst_addr, namelen);
	}
	else
		return PJ_EINVALIDOP;
}

PJ_DECL(pj_status_t) pj_ice_close_relayed_socket(pj_ice_strans *ice_st)
{
	if (ice_st 
		&& ice_st->state == PJ_ICE_STRANS_STATE_RUNNING
		&& ice_st->comp 
		&& ice_st->comp[0]->turn_sock) 
	{		
		return pj_turn_sock_close_relayed_socket(ice_st->comp[0]->turn_sock);
	}
	else
		return PJ_EINVALIDOP;
}
//get local internat address
PJ_DECL(pj_status_t) pj_ice_get_local_internat_addr(pj_ice_strans *ice_st, pj_sockaddr* local_addr)
{
	if (ice_st 
		&& ice_st->state >= PJ_ICE_STRANS_STATE_INIT
		&& ice_st->comp
		&& ice_st->comp[0]->cand_cnt)
	{
		unsigned int i;
		for(i=0; i<ice_st->comp[0]->cand_cnt; i++)
		{
			pj_ice_sess_cand* cand = &ice_st->comp[0]->cand_list[i];
			if(cand->type == PJ_ICE_CAND_TYPE_SRFLX)
			{
				pj_sockaddr_cp(local_addr, &cand->addr);
				return PJ_SUCCESS;
			}
		}
		//default use first 
		pj_sockaddr_cp(local_addr, &ice_st->comp[0]->cand_list[0].addr);
		return PJ_SUCCESS;
	}

	return PJ_EINVALIDOP;
}

//get remote peer internat address
PJ_DECL(pj_status_t) pj_ice_get_remote_internat_addr(pj_ice_strans *ice_st, pj_sockaddr* remote_addr)
{
	if (ice_st 
		&& ice_st->state >= PJ_ICE_STRANS_STATE_NEGO)
	{
		unsigned int i;
		for(i=0; i<ice_st->ice->rcand_cnt; i++)
		{
			pj_ice_sess_cand* cand = &ice_st->ice->rcand[i];
			if(cand->type == PJ_ICE_CAND_TYPE_SRFLX)
			{
				pj_sockaddr_cp(remote_addr, &cand->addr);
				return PJ_SUCCESS;
			}
		}
		//default use first 
		pj_sockaddr_cp(remote_addr, &ice_st->comp[0]->cand_list[0].addr);
		return PJ_SUCCESS;
	}

	return PJ_EINVALIDOP;
}
PJ_DECL(pj_status_t) pj_ice_report_session_info(pj_ice_strans *ice_st,
												pj_str_t* remote_user,
												pj_time_val* begin_time,
												int result,
												pj_int32_t conn_id,
												char* client_guid,
												pj_int32_t type,
												pj_sockaddr* local_addr,
												pj_sockaddr* remote_addr)
{
	if (ice_st 
		&& ice_st->state >= PJ_ICE_STRANS_STATE_INIT
		&& ice_st->comp 
		&& ice_st->comp[0]->turn_sock) 
	{		
		return pj_turn_sock_report_session_info(ice_st->comp[0]->turn_sock,
			remote_user,
			begin_time,
			result,
			conn_id,
			client_guid,
			type,
			local_addr,
			remote_addr);
	}
	else
		return PJ_EINVALIDOP;
}

PJ_DECL(pj_status_t) pj_ice_report_session_destroyed(pj_ice_strans *ice_st, 
													 pj_int32_t conn_id,
													 char* client_guid)
{
	if (ice_st 
		&& ice_st->state >= PJ_ICE_STRANS_STATE_READY
		&& ice_st->comp 
		&& ice_st->comp[0]->turn_sock) 
	{		
		return pj_turn_sock_report_session_destroyed(ice_st->comp[0]->turn_sock,
			conn_id,
			client_guid);
	}
	else
		return PJ_EINVALIDOP;
}

PJ_DECL(pj_status_t) pj_ice_get_udt_socket(pj_ice_strans *ice_st, pj_sock_t* sock)
{
	if (ice_st 
		&& ice_st->state == PJ_ICE_STRANS_STATE_RUNNING
		&& ice_st->ice->comp
		&& ice_st->comp)
	{
		if(ice_st->ice->comp[0].valid_check->lcand->transport_id == TP_STUN)
		{
			return pj_stun_sock_get_sock(ice_st->comp[0]->stun_sock, sock);
		}
	}

	*sock = 0;
	return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pj_ice_get_guess_socket(pj_ice_strans *ice_st, pj_sock_t* sock)
{
	if (ice_st 
		&& ice_st->state == PJ_ICE_STRANS_STATE_RUNNING
		&& ice_st->ice->comp
		&& ice_st->comp)
	{
		return pj_stun_sock_get_sock(ice_st->comp[0]->stun_sock, sock);
	}

	*sock = 0;
	return PJ_SUCCESS;
}

PJ_DECL(int) pj_ice_strans_server_net_state(pj_ice_strans *ice_st)
{
	if (ice_st 
		&& ice_st->comp 
		&& ice_st->comp[0]->turn_sock) 
	{
		return  pj_turn_sock_server_net_state(ice_st->comp[0]->turn_sock);
	}
	else
		return PJ_STUN_MAX_TRANSMIT_COUNT;
}