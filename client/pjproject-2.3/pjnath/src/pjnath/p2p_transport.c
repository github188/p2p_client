#include <p2p_transport.h>
#include <pjnath/p2p_global.h>
#include <pjnath/errno.h>
#include <pjnath/p2p_conn.h>
#include <pjnath/p2p_tcp.h>

#define THIS_FILE "p2p_transport.c"
#define P2P_VERSION "1.5.100"

#define KA_INTERVAL 60
#define CONN_HASH_TABLE_SIZE 31

#ifdef WIN32
#define UDP_RECV_BUF_SIZE (65536)
#endif

#define INTERNAL_FLAG_TCP (1)

static void on_ice_data_keep_alive(pj_ice_strans *ice_st, pj_status_t status);
static void p2p_listener_get_sock_addr(pj_sockaddr_t* addr, void* user_data);
static pj_status_t p2p_listener_udt_on_accept(void* user_data, void* udt_sock, pj_sockaddr_t* addr);
static pj_status_t p2p_listener_udt_send(void* user_data, const pj_sockaddr_t* addr, const char* buffer, size_t buffer_len);


//return 1 is p2p no relay, 0 is p2p relay
static int p2p_get_conn_type(pj_ice_strans_p2p_conn* conn)
{
	if(conn->local_addr_type < PJ_ICE_CAND_TYPE_RELAYED 
		&& conn->remote_addr_type < PJ_ICE_CAND_TYPE_RELAYED)
		return 1;
	else
		return 0;
}

//report session information to p2p server
void p2p_report_session_info(pj_ice_strans_p2p_conn* conn, int result, unsigned char port_guess_ok)
{
	if(conn->is_initiative && conn->remote_user.slen != 0)
	{
		PJ_LOG(4,(conn->transport->obj_name, "p2p_report_session_info result %d,port_guess_ok %d", result, port_guess_ok));

		pj_ice_report_session_info(conn->transport->assist_icest, 
			&conn->remote_user,
			&conn->connect_begin_time,
			result,
			conn->conn_id,
			get_p2p_global()->client_guid + strlen(P2P_CLIENT_PREFIX),
			port_guess_ok ? 1 : p2p_get_conn_type(conn),
			&conn->local_internet_addr,
			&conn->remote_internet_addr);
	}
}

//report session information to p2p server
static void p2p_transport_report_session_info(p2p_transport *transport, pj_int32_t conn_id, int result)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	if(transport == 0 || !transport->connected)
		return;

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &conn_id, sizeof(pj_int32_t), &hval);
	if(conn) //*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
	{
		p2p_report_session_info(conn, result, 0);
		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
	}
}

static void p2p_transport_add_udt_conn(pj_ice_strans_p2p_conn* conn)
{
	p2p_transport *transport = conn->transport;
	pj_grp_lock_acquire(transport->grp_lock);
	pj_hash_set(transport->pool, transport->udt_conn_hash_table, &conn->remote_addr, pj_sockaddr_get_len(&conn->remote_addr), 0, conn);
	pj_grp_lock_release(transport->grp_lock);
}

static void remove_conn_from_transport(p2p_transport *transport, pj_int32_t connection_id)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	p2p_udt_listener *udt_listener = NULL;
	if(transport == 0)
		return;
	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn) //use pj_hash_set NULL, remove from hash table
	{
		pj_hash_set(NULL, transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), hval, NULL);
		if(conn->is_initiative == PJ_FALSE)
		{
			pj_hash_set(NULL, transport->udt_conn_hash_table, &conn->remote_addr, pj_sockaddr_get_len(&conn->remote_addr), 0, NULL);
			if(pj_hash_count(transport->udt_conn_hash_table) == 0 
				&& pj_hash_count(transport->conn_hash_table) == 0 )
			{
				udt_listener = transport->udt_listener;
				transport->udt_listener = NULL;
			}
		}
	}
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
		destroy_p2p_conn(conn);
	if(udt_listener)
	{
		destroy_p2p_udt_listener(udt_listener);
		PJ_LOG(4,(conn->transport->obj_name, "remove_conn_from_transport destroy_p2p_udt_listener"));
	}
}

static void p2p_conn_udt_on_connect(void* user_data, pj_bool_t success)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	pj_ice_strans_cfg * ice_cfg = 0;

	if(!p2p_conn_is_valid(conn))
		return;

	PJ_LOG(3,(conn->transport->obj_name, 
		"p2p_conn_udt_on_connect transport %p, conn_id %d, success %d", 
		conn->transport,
		conn->conn_id,
		success));
	
	ice_cfg = pj_ice_strans_get_cfg(conn->icest);

	if(success)
	{
#ifdef USE_P2P_PORT_GUESS
		if(!conn->port_guess)
#endif
			p2p_report_session_info(conn, P2P_SESSION_OK, 0);

		conn->udt_status = UDT_STATUS_CONNECTED;
	}
	else
	{
		p2p_report_session_info(conn, P2P_CREATE_UDT_ERROR, 0);
	}
	if( conn->transport->cb && conn->transport->cb->on_connect_complete)/*call back connect remote user result to application*/
	{
		(*conn->transport->cb->on_connect_complete)(conn->transport,
			conn->conn_id, 
			success ? PJ_SUCCESS : PJ_UDT_CONNECT_FAILED,
			conn->transport->user_data, 
			ice_cfg->turn.alloc_param.user_data );
	}	
}

static void on_io_thread_udt_close(void* data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)data;
	if(!p2p_conn_is_valid(conn))
	{
		//add in p2p_conn_udt_on_close
		pj_grp_lock_release(conn->grp_lock);
		return;
	}
	on_ice_data_keep_alive(conn->icest, PJ_EGONE);
	//add in p2p_conn_udt_on_close
	pj_grp_lock_release(conn->grp_lock);
}

void p2p_conn_udt_on_close(void* user_data)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)user_data;
	p2p_socket_pair_item item;
	
	if(!p2p_conn_is_valid(conn))
		return;

	if(conn->udt_status == UDT_STATUS_DISCONNECT)
		return;

	//release in on_io_thread_udt_close
	pj_grp_lock_add_ref(conn->grp_lock);

	conn->udt_status = UDT_STATUS_DISCONNECT;

	item.cb = on_io_thread_udt_close;
	item.data = user_data;
	schedule_socket_pair(get_p2p_global()->sock_pair, &item);
}

//p2p transfer data ice negotiate begin
static void on_ice_data_nego_complete_begin(pj_ice_strans *ice_st,
									  pj_status_t status)
{
	if(status == PJ_SUCCESS)
	{
		pj_ice_strans_p2p_conn* conn;
		conn = pj_ice_strans_get_user_data(ice_st);
		pj_ice_get_remote_internat_addr(ice_st, &conn->remote_internet_addr);
	}
}

//p2p transfer data ice negotiate complete, if status is PJ_SUCCESS, the p2p channel is ok now.
static void on_ice_data_nego_complete(pj_ice_strans *ice_st,
									  pj_status_t status)
{
	pj_ice_strans_p2p_conn* conn;
	conn = pj_ice_strans_get_user_data(ice_st);
	PJ_LOG(4, (conn->transport->obj_name, "on_ice_data_nego_complete %p, status=%d, conn_id=%d, is_initiative=%d", 
		conn->transport, 
		status,
		conn->conn_id,
		conn->is_initiative));
	
	if(status == PJ_SUCCESS)
	{
		const pj_ice_sess_check* valid_check = pj_ice_strans_get_valid_pair(conn->icest, 1);
		if(valid_check)
		{
			char addr_info[PJ_INET6_ADDRSTRLEN+10];

			if(valid_check->rcand)
			{
				pj_sockaddr_cp(&conn->remote_addr, &valid_check->rcand->addr);
				pj_sockaddr_print(&conn->remote_addr, addr_info, sizeof(addr_info), 3);
				PJ_LOG(4,(conn->transport->obj_name, "on_ice_data_nego_complete remote addr %s %d", addr_info, valid_check->rcand->type));
				if(valid_check->rcand->type < PJ_ICE_CAND_TYPE_RELAYED) //if a client and turn server in LAN, b client in WAN, maybe PJ_ICE_CAND_TYPE_PRFLX
					pj_ice_close_relayed_socket(ice_st);
				conn->remote_addr_type = valid_check->rcand->type;
			}

			if(valid_check->lcand)
			{
				pj_sockaddr_cp(&conn->local_addr, &valid_check->lcand->addr);
				pj_sockaddr_print(&conn->local_addr, addr_info, sizeof(addr_info), 3);
				conn->local_addr_type = valid_check->lcand->type;
				PJ_LOG(4,(conn->transport->obj_name, "on_ice_data_nego_complete local addr %s", addr_info));
			}
#ifdef USE_P2P_PORT_GUESS
			if(get_p2p_global()->enable_port_guess != 0)
			{
				//if relayed,try port guess
				if(valid_check->rcand->type == PJ_ICE_CAND_TYPE_RELAYED 
					|| valid_check->lcand->type == PJ_ICE_CAND_TYPE_RELAYED)
				{
					pj_sock_t sock = 0;
					pj_ice_get_guess_socket(ice_st, &sock);
					if(sock != 0)
						conn->port_guess = p2p_create_port_guess(sock, &conn->remote_internet_addr,	conn);
				}
			}
#endif
		}
		
		if(conn->is_initiative)
		{
			p2p_udt_cb udt_cb;
			pj_sock_t sock;
			pj_ice_get_udt_socket(conn->icest, &sock);

			pj_bzero(&udt_cb, sizeof(p2p_udt_cb));
			udt_cb.udt_send = &p2p_ice_send_data;
			udt_cb.udt_on_recved = &on_p2p_conn_recved_data;
			udt_cb.get_sock_addr = &p2p_conn_get_sock_addr;
			udt_cb.get_peer_addr = &p2p_conn_get_peer_addr;
			udt_cb.udt_on_connect = &p2p_conn_udt_on_connect;
			udt_cb.udt_pause_send = &p2p_conn_pause_send;
			udt_cb.udt_on_close = &p2p_conn_udt_on_close;
			udt_cb.udt_on_noresend_recved = &on_p2p_conn_recved_noresend_data;

			status = create_p2p_udt_connector(&udt_cb, conn, conn->send_buf_size, conn->recv_buf_size, sock, &conn->udt.p2p_udt_connector);
			if(status != PJ_SUCCESS)
			{
				pj_ice_strans_cfg * ice_cfg = pj_ice_strans_get_cfg(ice_st);
				p2p_report_session_info(conn, P2P_CREATE_UDT_ERROR, 0);
				if( conn->transport->cb && conn->transport->cb->on_connect_complete)/*call back connect remote user result to application*/
				{
					(*conn->transport->cb->on_connect_complete)(conn->transport,
						conn->conn_id, 
						status,
						conn->transport->user_data, 
						ice_cfg->turn.alloc_param.user_data );
				}				
			}
		}
		else
		{
			p2p_transport_add_udt_conn(conn);
#ifdef USE_P2P_TCP
			p2p_listener_udt_on_accept(conn->transport, NULL, &conn->remote_addr);
#endif
		}
	}
	else
	{
		p2p_report_session_info(conn, P2P_FAILED_NEGO, 0);
	}
	
	if(status != PJ_SUCCESS)
	{
		remove_conn_from_transport(conn->transport, conn->conn_id);
	}
}

//p2p transfer data ice connected to server
static void on_ice_data_init_complete(pj_ice_strans *ice_st,
									  pj_status_t status)
{
	pj_ice_strans_p2p_conn* conn;
	pj_ice_strans_cfg *ice_cfg;
	conn = pj_ice_strans_get_user_data(ice_st);
	
	PJ_LOG(4,(conn->transport->obj_name, "on_ice_data_init_complete %p, id %d, status=%d", conn->transport, conn->conn_id, status));
	ice_cfg = pj_ice_strans_get_cfg(ice_st);

	if(status == PJ_SUCCESS)
	{
		pj_ice_get_local_internat_addr(ice_st, &conn->local_internet_addr);
		if (!pj_ice_strans_has_sess(ice_st)) 
		{
			PJ_LOG(4,(conn->transport->obj_name, "PJ_ICE_STRANS_OP_INIT call pj_ice_strans_init_ice"));
			status = pj_ice_strans_init_ice(ice_st, PJ_ICE_SESS_ROLE_CONTROLLED, NULL, NULL);
			PJ_LOG(4,(conn->transport->obj_name, "PJ_ICE_STRANS_OP_INIT pj_ice_strans_init_ice return %d", status));
			if(conn->is_initiative)
			{
				if(status == PJ_SUCCESS)
				{
					/*request exchange address information*/
					status = pj_ice_strans_p2p_exchange_info(ice_st);
					PJ_LOG(4,(conn->transport->obj_name, "PJ_ICE_STRANS_OP_INIT pj_ice_strans_p2p_exchange_info return %d", status));
				}
				if(status != PJ_SUCCESS)
				{
					PJ_LOG(4,(conn->transport->obj_name, "cb_on_ice_data_complete PJ_ICE_STRANS_OP_INIT failed, call back %d", status));
					p2p_report_session_info(conn, P2P_CREATE_ICE_ERROR, 0);
					if( conn->transport->cb && conn->transport->cb->on_connect_complete)
					{
						(*conn->transport->cb->on_connect_complete)(conn->transport,
							conn->conn_id, 
							status,
							conn->transport->user_data, 
							ice_cfg->turn.alloc_param.user_data );
					}
					
					remove_conn_from_transport(conn->transport, conn->conn_id);
				}
			}
		}
	}
	else
	{
		if(conn->is_initiative)
		{
			p2p_report_session_info(conn, P2P_DATA_CONNECT_SERVER, 0);
			if( conn->transport->cb && conn->transport->cb->on_connect_complete)
			{
				(*conn->transport->cb->on_connect_complete)(conn->transport,
					conn->conn_id, 
					status,
					conn->transport->user_data, 
					ice_cfg->turn.alloc_param.user_data );
			}			
		}
		remove_conn_from_transport(conn->transport, conn->conn_id);
	}
}

/** This operation is used to report failure in keep-alive operation.
*  Currently it is only used to report TURN Refresh failure.
*/
/*server is down or remote user close p2p connection*/
static void on_ice_data_keep_alive(pj_ice_strans *ice_st,
									pj_status_t status)
{
	pj_ice_strans_p2p_conn* conn;
	pj_ice_strans_cfg *ice_cfg;
	pj_bool_t udt_sock_invalid = PJ_FALSE;

	conn = pj_ice_strans_get_user_data(ice_st);
	ice_cfg = pj_ice_strans_get_cfg(ice_st);

	if(conn->is_initiative)
	{
		if(conn->udt.p2p_udt_connector)
			udt_sock_invalid = PJ_TRUE;
	}
	else
	{
		if(conn->udt.p2p_udt_accepter)
			udt_sock_invalid = PJ_TRUE;
	}

	PJ_LOG(4,(conn->transport->obj_name, "on_ice_data_keep_alive %p, id=%d,status=%d,disconnect_req=%d",
		conn->transport, conn->conn_id, status, conn->disconnect_req, conn->udt_status));
	if(status != PJ_SUCCESS)
	{
		//wake up block send
		p2p_conn_wakeup_block_send(conn);

		//if user had called p2p_transport_disconnect, do not call on_connection_disconnect
		if( !conn->disconnect_req 
			&& udt_sock_invalid
			&& (conn->udt_status == UDT_STATUS_CONNECTED || conn->udt_status == UDT_STATUS_DISCONNECT)
			&& conn->transport->cb 
			&& conn->transport->cb->on_connection_disconnect)
		{
				(*conn->transport->cb->on_connection_disconnect)(conn->transport,
					conn->conn_id, 
					conn->transport->user_data, 
					ice_cfg->turn.alloc_param.user_data );
		}
		remove_conn_from_transport(conn->transport, conn->conn_id);
	}
}

static void cb_on_ice_data_complete(pj_ice_strans *ice_st, 
									  pj_ice_strans_op op,
									  pj_status_t status)
{
	PJ_LOG(4,("pj_ice_strans", "cb_on_ice_data_complete %p, op=%d, status=%d", ice_st, op, status));
	switch(op)
	{
	case PJ_ICE_STRANS_OP_INIT:
		on_ice_data_init_complete(ice_st, status);
		break;
	case PJ_ICE_STRANS_OP_NEGOTIATION:
		on_ice_data_nego_complete(ice_st, status);
		break;
	case PJ_ICE_STRANS_OP_KEEP_ALIVE:
		on_ice_data_keep_alive(ice_st, status);
		break;
	case PJ_ICE_STRANS_OP_NEGOTIATION_BEGIN:
		on_ice_data_nego_complete_begin(ice_st, status);
		break;
	}
}

static void cb_on_p2p_exchange_info(pj_ice_strans *ice_st, pj_status_t status)
{
	PJ_LOG(4,("pj_ice_strans", "cb_on_p2p_exchange_info start %d %p", ice_st));
	if(status != PJ_SUCCESS)
	{
		pj_ice_strans_p2p_conn* conn;
		pj_ice_strans_cfg *ice_cfg;
		conn = pj_ice_strans_get_user_data(ice_st);

		PJ_LOG(4,(conn->transport->obj_name, "cb_on_p2p_exchange_info %p, id=%d, status=%d", conn->transport, conn->conn_id, status));
		ice_cfg = pj_ice_strans_get_cfg(ice_st);
		p2p_report_session_info(conn, P2P_FAILED_EXCHANGE_INFO, 0);
		if( conn->transport->cb && conn->transport->cb->on_connect_complete)
		{
			(*conn->transport->cb->on_connect_complete)(conn->transport,
				conn->conn_id, 
				status,
				conn->transport->user_data, 
				ice_cfg->turn.alloc_param.user_data );
		}		
		remove_conn_from_transport(conn->transport, conn->conn_id);
	}
}

#ifdef USE_P2P_PORT_GUESS
static pj_bool_t port_guess_on_ice_rx_data(pj_ice_strans_p2p_conn* conn,  
									  void *pkt, 
									  pj_size_t size,
									  const pj_sockaddr_t *src_addr,
									  unsigned src_addr_len)
{
	unsigned char type =  *((unsigned char*)pkt);

	if(type == P2P_GUESS_REQUEST) //receive port guess request
	{
		char addr_info[PJ_INET6_ADDRSTRLEN+10];
		pj_sockaddr_print(src_addr, addr_info, sizeof(addr_info), 3);

		PJ_LOG(3,("cb_on_ice_rx_data", "cb_on_ice_rx_data P2P_GUESS_REQUEST %s", addr_info));

		if(conn->port_guess)
			p2p_port_guess_on_request(conn->port_guess, pkt, size, src_addr, src_addr_len);
		return PJ_TRUE;
	}
	else if(type == P2P_GUESS_RESPONSE) //receive port guess request
	{
		char addr_info[PJ_INET6_ADDRSTRLEN+10];
		pj_sockaddr_print(src_addr, addr_info, sizeof(addr_info), 3);
		PJ_LOG(3,("cb_on_ice_rx_data", "cb_on_ice_rx_data P2P_GUESS_RESPONSE %s", addr_info));

		if(conn->port_guess)
			p2p_port_guess_on_response(conn->port_guess, pkt, size, src_addr, src_addr_len);
		return PJ_TRUE;
	}

	return PJ_FALSE;
}
#endif

static void cb_on_ice_rx_data(pj_ice_strans *ice_st,
					  unsigned comp_id, 
					  void *pkt, pj_size_t size,
					  const pj_sockaddr_t *src_addr,
					  unsigned src_addr_len)
{
	pj_ice_strans_p2p_conn* conn = pj_ice_strans_get_user_data(ice_st);
	char addr_info[PJ_INET6_ADDRSTRLEN+10];

	PJ_UNUSED_ARG(comp_id);

	/*pj_sockaddr_print(src_addr, addr_info, sizeof(addr_info), 3);
	PJ_LOG(5, ("p2p_conn", "cb_on_ice_rx_data %p, %p %d, %d, %s", conn, conn->conn_id, size, conn->is_initiative, addr_info));*/

#ifdef USE_P2P_PORT_GUESS
	if(port_guess_on_ice_rx_data(conn, pkt, size, src_addr, src_addr_len))
		return;
#endif

	if(conn->is_initiative)
	{
		if(conn->udt.p2p_udt_connector)
		{
			p2p_udt_connector_on_recved(conn->udt.p2p_udt_connector, pkt, size, &conn->remote_addr, src_addr_len);
		}
	}
	else
	{
		/*when first receive remote udt connect data package, add the connection to udt connection hash table
		 *get conn->remote_addr in on_ice_data_nego_complete function
		 *when ice is controlled or controlling, the src_add maybe not equal to conn->remote_addr
		 *force use conn->remote_addr replase src_addr when call p2p_udt_listener_on_recved*/
		if(!conn->recved_first_data)
		{
			char new_addr_info[PJ_INET6_ADDRSTRLEN+10]={0};

			pj_sockaddr_print(&conn->remote_addr, addr_info, sizeof(addr_info), 3);
			PJ_LOG(4,(conn->transport->obj_name, "cb_on_ice_rx_data old remote addr %s", addr_info));
			
			pj_sockaddr_print(src_addr, new_addr_info, sizeof(new_addr_info), 3);
			PJ_LOG(4,(conn->transport->obj_name, "cb_on_ice_rx_data new remote addr %s", new_addr_info));

			conn->recved_first_data = PJ_TRUE;
		}
		if(conn->transport && conn->transport->udt_listener)
		{
#ifdef USE_P2P_TCP
			if(conn->udt.p2p_udt_accepter)
				p2p_udt_accepter_on_recved(conn->udt.p2p_udt_accepter, pkt, size, &conn->remote_addr, src_addr_len);
#else
			p2p_udt_listener_on_recved(conn->transport->udt_listener, pkt, size, &conn->remote_addr, src_addr_len);
#endif
		}
	}
}

static pj_status_t create_initiative_data_icest(p2p_transport *p2p, void* user_data, pj_str_t* remote_user, pj_int32_t conn_id)
{
	pj_status_t status;
	pj_ice_strans_cfg ice_cfg;
	pj_ice_strans_cb icecb;
	pj_ice_strans *ice;
	pj_pool_t *tmp_pool;
	pj_uint32_t hval =0;
	pj_ice_strans_p2p_conn* conn;
	pj_bool_t destroy_ice = PJ_TRUE;

	PJ_LOG(4,(p2p->obj_name, "create_initiative_data_icest %p, start %p %.*s %d", p2p, user_data,remote_user->slen, remote_user->ptr, conn_id));

	pj_grp_lock_acquire(p2p->grp_lock);
	conn  = pj_hash_get(p2p->conn_hash_table, &conn_id, sizeof(pj_int32_t), &hval);
	if(conn)//*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_release(p2p->grp_lock);
	if(!conn)//the connection maybe removed by user
		return PJ_EGONE;

	//create temp memory pool for alloc pj_ice_strans_cfg
	tmp_pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_tmp%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	/*copy ice config from assist ice*/
	pj_ice_strans_cfg_copy2(tmp_pool, &ice_cfg, p2p->assist_icest);

	ice_cfg.stun.server = ice_cfg.turn.server;
	ice_cfg.stun.port = ice_cfg.turn.port;
	ice_cfg.turn.alloc_param.is_initiative = PJ_TRUE;
	ice_cfg.turn.alloc_param.is_assist = PJ_FALSE;
	ice_cfg.turn.alloc_param.conn_id = conn_id;
	ice_cfg.turn.alloc_param.user_data = user_data;
	ice_cfg.stun.max_host_cands = 64;
	pj_strdup_with_null(tmp_pool, &ice_cfg.turn.alloc_param.remote_user, remote_user);

#ifdef WIN32
	ice_cfg.stun.cfg.so_rcvbuf_size = ice_cfg.turn.cfg.so_rcvbuf_size = UDP_RECV_BUF_SIZE;
#endif

	/* init the callback */
	pj_bzero(&icecb, sizeof(icecb));
	icecb.on_rx_data = &cb_on_ice_rx_data;
	icecb.on_ice_complete = &cb_on_ice_data_complete;
	icecb.on_p2p_exchange_info = &cb_on_p2p_exchange_info;

	/* create the instance, call back function is "cb_on_ice_data_complete"*/
	status = pj_ice_strans_create("p2pdat%p", &ice_cfg, 1, conn, &icecb, &ice);
	pj_pool_release(tmp_pool);
	if (status != PJ_SUCCESS)
	{
		PJ_LOG(4,(p2p->obj_name, "create_initiative_data_icest %p, failed to end, status %d", p2p, status));
		return status;
	}

	pj_grp_lock_acquire(conn->grp_lock);
	if(conn->destroy_req == PJ_FALSE)
	{
		destroy_ice = PJ_FALSE;
		conn->icest = ice;
	}
	pj_grp_lock_dec_ref(conn->grp_lock);
	pj_grp_lock_release(conn->grp_lock);

	if(destroy_ice)
		pj_ice_strans_destroy(ice);

	PJ_LOG(4,(p2p->obj_name, "create_initiative_data_icest %p, end", p2p));
	return PJ_SUCCESS;
}



pj_bool_t passivity_icest_exist(p2p_transport *p2p, pj_str_t* remote_user, pj_int32_t conn_id)
{
	pj_hash_iterator_t itbuf, *it;
	pj_bool_t exist = PJ_FALSE;

	pj_grp_lock_acquire(p2p->grp_lock);
	if(pj_hash_count(p2p->udt_conn_hash_table) >= (unsigned int)get_p2p_global()->max_client_count)
	{
		PJ_LOG(4,(p2p->obj_name, "****************too many client****************"));
		pj_grp_lock_release(p2p->grp_lock);
		return PJ_TRUE;
	}

	it = pj_hash_first(p2p->conn_hash_table, &itbuf);
	while (it) 
	{
		pj_ice_strans_p2p_conn *conn = (pj_ice_strans_p2p_conn*) pj_hash_this(p2p->conn_hash_table, it);
		if(pj_strcmp(&conn->remote_user, remote_user) == 0 
			&& conn->remote_conn_id == conn_id)
		{
			exist = PJ_TRUE;
			break;
		}

		it = pj_hash_next(p2p->conn_hash_table, it);
	}
	pj_grp_lock_release(p2p->grp_lock);

	return exist;
}

static pj_status_t create_passivity_data_icest(p2p_transport *p2p, void* user_data, pj_str_t* remote_user, pj_int32_t conn_id, pj_int32_t conn_flag, pj_int32_t internal_flag)
{
	pj_status_t status;
	pj_ice_strans_cfg ice_cfg;
	pj_ice_strans_cb icecb;
	pj_ice_strans *ice;
	pj_pool_t *tmp_pool;
	pj_int32_t cid;
	pj_uint32_t hval =0;
	pj_ice_strans_p2p_conn* conn;
	pj_bool_t seted = PJ_FALSE;


	if(passivity_icest_exist(p2p, remote_user, conn_id))
		return PJ_SUCCESS;

	PJ_LOG(4,(p2p->obj_name, "create_passivity_data_icest %p, start %p %.*s %d", p2p, user_data, remote_user->slen, remote_user->ptr, conn_id));

	//create temp memory pool for alloc pj_ice_strans_cfg
	tmp_pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p_tmp%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	/*copy ice config from assist ice*/
	pj_ice_strans_cfg_copy2(tmp_pool, &ice_cfg, p2p->assist_icest);
	
	ice_cfg.stun.server = ice_cfg.turn.server;
	ice_cfg.stun.port = ice_cfg.turn.port;
	ice_cfg.stun.max_host_cands = 64;

	ice_cfg.turn.alloc_param.is_initiative = PJ_FALSE;
	ice_cfg.turn.alloc_param.is_assist = PJ_FALSE;
	ice_cfg.turn.alloc_param.conn_id = conn_id;
	ice_cfg.turn.alloc_param.user_data = user_data;
	pj_strdup_with_null(tmp_pool, &ice_cfg.turn.alloc_param.remote_user, remote_user);
	if(ice_cfg.turn.conn_type == PJ_TURN_TP_UDP && (internal_flag & INTERNAL_FLAG_TCP))
		ice_cfg.turn.conn_type = PJ_TURN_TP_TCP;

	/* init the callback */
	pj_bzero(&icecb, sizeof(icecb));
	icecb.on_rx_data = &cb_on_ice_rx_data;
	icecb.on_ice_complete = &cb_on_ice_data_complete;
	icecb.on_p2p_exchange_info = &cb_on_p2p_exchange_info;

#ifdef WIN32
	ice_cfg.stun.cfg.so_rcvbuf_size = ice_cfg.turn.cfg.so_rcvbuf_size = UDP_RECV_BUF_SIZE;
#endif

	/* create the instance, call back function is "cb_on_ice_data_complete"*/
	conn = create_p2p_conn(&p2p->proxy_addr, PJ_FALSE);
	status = pj_ice_strans_create("p2pdat%p", &ice_cfg, 1, conn, &icecb, &ice);
	
	if (status != PJ_SUCCESS)
	{
		pj_grp_lock_dec_ref(conn->grp_lock);//free conn
		PJ_LOG(4,(p2p->obj_name, "create_passivity_data_icest %p, failed to end, status %d", p2p, status));
		pj_pool_release(tmp_pool);
		return status;
	}

	//add transfer data ice to hash table, hash key is connection id
	cid = pj_atomic_inc_and_get(get_p2p_global()->atomic_id);
	pj_grp_lock_acquire(p2p->grp_lock);
	if (pj_hash_get(p2p->conn_hash_table, &cid, sizeof(pj_int32_t),	&hval) == NULL) 
	{		
		conn->transport = p2p;
		conn->conn_id = cid;
		conn->conn_flag = conn_flag;
		conn->hash_value = hval;
		conn->icest = ice;
		conn->is_initiative = PJ_FALSE;
		conn->remote_conn_id = conn_id;
		pj_strdup_with_null(conn->pool, &conn->remote_user, remote_user);
		pj_strdup_with_null(conn->pool, &conn->user, &ice_cfg.turn.auth_cred.data.static_cred.username);
		pj_hash_set(p2p->pool, p2p->conn_hash_table, &cid, sizeof(pj_int32_t), hval, conn);
		seted = PJ_TRUE;
	}
	pj_pool_release(tmp_pool);
	pj_grp_lock_release(p2p->grp_lock);

	if(seted == PJ_FALSE)
	{
		pj_ice_strans_destroy(ice);
		pj_grp_lock_dec_ref(conn->grp_lock); //free conn
		PJ_LOG(4,(p2p->obj_name, "create_passivity_data_icest %p failed", p2p));
		return PJ_EUNKNOWN;
	}

	PJ_LOG(4,(p2p->obj_name, "create_passivity_data_icest %p, end", p2p));
	return PJ_SUCCESS;
}


//request connect remote user call back
static void on_p2p_connect(struct p2p_conn_arg* arg, pj_status_t status)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;

	PJ_LOG(4,("on_p2p_connect", "on_p2p_connect start %p %p %s %d %d", arg, arg->transport, arg->remote_user, arg->conn_id, status));
	if(!arg || !arg->transport || !arg->transport->grp_lock || !arg->transport->connected)
		return;
	
	//the p2p connection maybe destroyed by call p2p_transport_disconnect
	pj_grp_lock_acquire(arg->transport->grp_lock);
	conn  = pj_hash_get(arg->transport->conn_hash_table, &arg->conn_id, sizeof(pj_int32_t), &hval);
	pj_grp_lock_release(arg->transport->grp_lock);
	if(!conn)
	{
		PJ_LOG(3,("on_p2p_connect", "on_p2p_connect pj_hash_get return NULL"));

		p2p_free(arg->remote_user);
		p2p_free(arg);
		return;
	}

	if(status == PJ_SUCCESS)
	{
		if(arg && arg->transport)
		{
			pj_str_t user = pj_str(arg->remote_user);
			//connect successful, create transfer data ice
			status = create_initiative_data_icest(arg->transport, arg->user_data, &user,arg->conn_id);
			if(status != PJ_SUCCESS)
			{
				p2p_transport_report_session_info(arg->transport, arg->conn_id, P2P_CREATE_ICE_ERROR);
				if( arg->transport->cb && arg->transport->cb->on_connect_complete)
				{
					(*arg->transport->cb->on_connect_complete)(arg->transport,
						arg->conn_id, 
						status,
						arg->transport->user_data, 
						arg->user_data);
				}
				
			}
		}
	}
	else
	{
		if(arg->transport)
			p2p_transport_report_session_info(arg->transport, arg->conn_id, P2P_CONNECT_USER);
		if(arg && arg->transport && arg->transport->cb && arg->transport->cb->on_connect_complete)
		{
			(*arg->transport->cb->on_connect_complete)(arg->transport,
					arg->conn_id, 
					status,
					arg->transport->user_data, 
					arg->user_data);
		
		}		
	}
	if(status != PJ_SUCCESS)
		remove_conn_from_transport(arg->transport, arg->conn_id);
	p2p_free(arg->remote_user);
	p2p_free(arg);
	PJ_LOG(4,("on_p2p_connect", "on_p2p_connect end %p %d", arg, status));
}

//received a remote connect request
static void on_recved_p2p_connect(void* user_data, pj_str_t* remote_user, pj_int32_t conn_id, pj_int32_t conn_flag, pj_int32_t internal_flag)
{
	p2p_transport *p2p = (p2p_transport*)user_data;
	PJ_LOG(4,(p2p->obj_name, "on_recved_p2p_connect %p, start %.*s %d", p2p, remote_user->slen, remote_user->ptr, conn_id));
	
	create_passivity_data_icest(p2p, 0, remote_user, conn_id, conn_flag, internal_flag);

	if(p2p->udt_listener == NULL)
	{
		p2p_udt_cb cb;
		pj_status_t status;
		pj_sockaddr_in addr;
		int add_len;

		pj_sockaddr_in_init(&addr, NULL, 0);
		add_len = pj_sockaddr_get_len(&addr);
		status = pj_ice_strans_get_turn_sockaddr(p2p->assist_icest, &addr, &add_len); 
		if (status != PJ_SUCCESS)
		{
			PJ_LOG(2,(p2p->obj_name, "on_recved_p2p_connect pj_ice_strans_get_turn_sockaddr return %d", status));
			return;
		}

		pj_bzero(&cb, sizeof(cb));
		cb.get_sock_addr = &p2p_listener_get_sock_addr;
		cb.udt_on_accept = &p2p_listener_udt_on_accept;
		cb.udt_send = &p2p_listener_udt_send;
		status = create_p2p_udt_listener(&cb, p2p, &addr, &p2p->udt_listener);
		if (status != PJ_SUCCESS)
		{
			PJ_LOG(2,(p2p->obj_name, "on_recved_p2p_connect create_p2p_udt_listener return %d", status));
			return ;
		}
	}
}

//callback on p2p_transport's assist ice connected to turn server
static void cb_on_ice_assist_complete(pj_ice_strans *ice_st, 
									  pj_ice_strans_op op,
									  pj_status_t status)
{
	p2p_transport *p2p = pj_ice_strans_get_user_data(ice_st);
	pj_bool_t callback = PJ_FALSE;
	if(p2p == 0)
		return;
	PJ_LOG(4,(p2p->obj_name, "cb_on_ice_assist_complete %p, op=%d, status=%d,assist_icest %p", p2p, op, status,p2p->assist_icest));
	if(op == PJ_ICE_STRANS_OP_INIT)
	{
		if(status == PJ_SUCCESS)
		{
			if(p2p->connected == PJ_FALSE)
			{
				p2p->connected = PJ_TRUE;
				callback = PJ_TRUE;
			}
		}
		else
		{
			if(p2p->connected == PJ_TRUE)
			{
				p2p->connected = PJ_FALSE;
				callback = PJ_TRUE;
			}
		}
		//call p2p_transport_create connect server, error is 70018, p2p->assist_icest == NULL
		//no call on_create_complete, prevent user call p2p_transport_destroy
		if(p2p->first_assist_complete == PJ_TRUE && p2p->assist_icest)
		{
			p2p->first_assist_complete = PJ_FALSE;
			callback = PJ_TRUE;
		}
		if(callback && p2p->cb && p2p->cb->on_create_complete)
		{
			PJ_LOG(4,(p2p->obj_name, "cb_on_ice_assist_complete call on_create_complete"));
			(*p2p->cb->on_create_complete)(p2p, status, p2p->user_data);
		}
	}
	else if(op == PJ_ICE_STRANS_OP_KEEP_ALIVE)
	{
		p2p->connected = PJ_FALSE;
		if(p2p->cb && p2p->cb->on_disconnect_server)
		{
			(*p2p->cb->on_disconnect_server)(p2p, status, p2p->user_data);
		}
	}

}

static pj_status_t create_assist_icest(p2p_transport *p2p, p2p_transport_cfg* cfg)
{
	pj_status_t status;
	pj_ice_strans_cfg ice_cfg;
	pj_ice_strans_cb icecb;
	char* server;
	char seps[] = "\n";
	char *token = 0;
	enum { MAX_BIND_RETRY = 100 };
	/* Init our ICE settings with null values */
	pj_ice_strans_cfg_default(&ice_cfg);

	PJ_LOG(4,(p2p->obj_name, "create_assist_icest %p start", p2p));

	ice_cfg.stun_cfg.pf = &get_p2p_global()->caching_pool.factory;
	ice_cfg.af = pj_AF_INET();

	//check server family 
	server = strdup(cfg->server);
	token = strtok(server, seps);
	/*assist ice only use turn,do not use stun*/
	pj_strdup2_with_null(p2p->pool, &ice_cfg.turn.server, token);

	token = strtok(NULL, seps);
	if(token)
		ice_cfg.af = atoi(token);
	free(server);	

	ice_cfg.turn.port = cfg->port;
	ice_cfg.stun_cfg.ioqueue = get_p2p_global()->ioqueue;
	ice_cfg.stun_cfg.timer_heap = get_p2p_global()->timer_heap;
	ice_cfg.stun.max_host_cands = 0 ;
	pj_sockaddr_init(ice_cfg.af, &ice_cfg.stun.cfg.bound_addr, NULL, get_p2p_global()->bind_port);
	ice_cfg.stun.cfg.port_range = MAX_BIND_RETRY;


	/* TURN credential */
	ice_cfg.turn.auth_cred.type = PJ_STUN_AUTH_CRED_STATIC;
	pj_strdup2_with_null(p2p->pool, &ice_cfg.turn.auth_cred.data.static_cred.username, cfg->user);
	ice_cfg.turn.auth_cred.data.static_cred.data_type = PJ_STUN_PASSWD_PLAIN;
	pj_strdup2_with_null(p2p->pool, &ice_cfg.turn.auth_cred.data.static_cred.data, cfg->password);

	/* Connection type to TURN server */
	if (cfg->use_tcp_connect_srv)
	    ice_cfg.turn.conn_type = PJ_TURN_TP_TCP;
	else
	    ice_cfg.turn.conn_type = PJ_TURN_TP_UDP;

	ice_cfg.turn.alloc_param.ka_interval = KA_INTERVAL;
	ice_cfg.turn.alloc_param.is_assist = PJ_TRUE;
	pj_sockaddr_init(ice_cfg.af, &ice_cfg.turn.cfg.bound_addr, NULL, get_p2p_global()->bind_port+1);
	ice_cfg.turn.cfg.port_range = MAX_BIND_RETRY;

	/* init the callback */
	pj_bzero(&icecb, sizeof(icecb));
	icecb.on_ice_complete = &cb_on_ice_assist_complete;
	icecb.on_p2p_connect = &on_p2p_connect;
	icecb.on_recved_p2p_connect = &on_recved_p2p_connect;

	/* create the instance */
	status = pj_ice_strans_create("p2pass%p", &ice_cfg, 1, p2p, &icecb, &p2p->assist_icest);
	if (status != PJ_SUCCESS)
	{
		PJ_LOG(4,(p2p->obj_name, "create_assist_icest %p, failed to end, status=%d", p2p, status));
		return status;
	}
	PJ_LOG(4,(p2p->obj_name, "create_assist_icest %p, af is %d, end", p2p, ice_cfg.af));
	return PJ_SUCCESS;
}

static void async_p2p_transport_destroy(void *arg)
{
	pj_hash_iterator_t itbuf, *it;
	pj_ice_strans_p2p_conn** destroy_conn = 0;
	unsigned conn_count = 0;
	unsigned i;
	p2p_disconnection_id* item, *cur_id;
	p2p_transport *transport = (p2p_transport *)arg;

	PJ_LOG(4,(transport->obj_name, "p2p transport %p destroy start", transport));

	pj_grp_lock_acquire(transport->grp_lock);

	if (transport->destroy_req) { //already destroy, so return
		pj_grp_lock_release(transport->grp_lock);
		return;
	}
	transport->destroy_req = PJ_TRUE;

	pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &transport->timer, P2P_TIMER_NONE);

	//prevent deadlock, get items in hash table, then clean hash table
	conn_count = pj_hash_count(transport->conn_hash_table);
	if(conn_count)
	{
		pj_ice_strans_p2p_conn** conn;
		destroy_conn = conn = (pj_ice_strans_p2p_conn**)p2p_malloc(sizeof(pj_ice_strans_p2p_conn*)*conn_count);
		it = pj_hash_first(transport->conn_hash_table, &itbuf);
		while (it) 
		{
			*conn = (pj_ice_strans_p2p_conn*) pj_hash_this(transport->conn_hash_table, it);
			//remove from hash table
			pj_hash_set(NULL, transport->conn_hash_table, &(*conn)->conn_id, sizeof(pj_int32_t), (*conn)->hash_value, NULL);
			it = pj_hash_first(transport->conn_hash_table, &itbuf);
			conn++;
		}
	}

	item = transport->disconnect_conns.next;
	while (item != &transport->disconnect_conns) 
	{
		cur_id = item;
		item = item->next;
		p2p_free(cur_id);
	}
	pj_list_init(&transport->disconnect_conns);

	pj_grp_lock_release(transport->grp_lock);

	//destroy all ice
	for(i=0; i<conn_count; i++)
	{
		destroy_p2p_conn(destroy_conn[i]);
	}
	if(destroy_conn)
		p2p_free(destroy_conn);

	if(transport->assist_icest)
	{
		PJ_LOG(4,(transport->obj_name, "p2p transport %p pj_ice_strans_destroy", transport->assist_icest));
		pj_ice_strans_destroy(transport->assist_icest);
		transport->assist_icest = NULL;
	}

	if(transport->udt_listener)
	{
		PJ_LOG(4,(transport->obj_name, "p2p transport %p destroy_p2p_udt_listener", transport->udt_listener));
		destroy_p2p_udt_listener(transport->udt_listener);
		transport->udt_listener = NULL;
	}

	if(transport->destroy_event)
	{
		PJ_LOG(4,(transport->obj_name, "p2p transport %p pj_event_set", transport->destroy_event));
		pj_event_set(transport->destroy_event);
	}

	//free reference count, trigger free memory function "p2p_transport_on_destroy", add in p2p_transport_create
	pj_grp_lock_dec_ref(transport->grp_lock);

	//add in p2p_transport_on_destroy
	if(transport->destroy_in_net_thread)
		pj_grp_lock_dec_ref(transport->grp_lock);
	PJ_LOG(4,("p2p_destroy", "p2p transport %p destroy end", transport));
	return ;
}


static void p2p_transport_destroy_impl(p2p_transport *transport)
{
	pj_time_val delay = {0, 0};

	transport->destroy_in_net_thread = get_p2p_global()->thread == pj_thread_this();
	pj_grp_lock_add_ref(transport->grp_lock);
	//async call async_p2p_transport_destroy
	p2p_global_set_timer(delay, transport, async_p2p_transport_destroy);

	if(transport->destroy_in_net_thread)
	{
		PJ_LOG(4,(transport->obj_name, "p2p_transport_destroy_impl 1 %p end", transport));
	}
	else
	{
		PJ_LOG(4,(transport->obj_name, "p2p_transport_destroy_impl 2 %p end", transport));
		pj_event_wait(transport->destroy_event);		
		pj_grp_lock_dec_ref(transport->grp_lock);
		PJ_LOG(4,("p2p_destroy", "p2p_transport_destroy_impl 3 %p end", transport));
	}
}

/*
 * Timer event.
 */
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
	p2p_transport *transport = (p2p_transport*)e->user_data;
	p2p_disconnection_id** dis_id = NULL;
	pj_int32_t dis_id_count = 0;
	pj_int32_t i=0;
	p2p_disconnection_id* item;
	PJ_UNUSED_ARG(th);
	
	if(e->id == P2P_DISCONNECT_CONNECTION)
	{
		pj_grp_lock_acquire(transport->grp_lock);
		if(transport->destroy_req)
		{
			pj_grp_lock_release(transport->grp_lock);
			return;
		}

		dis_id_count = pj_list_size(&transport->disconnect_conns);
		if(dis_id_count)
			dis_id = (p2p_disconnection_id**)p2p_malloc(sizeof(p2p_disconnection_id*)*dis_id_count);

		item = transport->disconnect_conns.next;
		while (item != &transport->disconnect_conns) 
		{
			dis_id[i++] = item;
			item = item->next;
		}
		pj_list_init(&transport->disconnect_conns);
		
		pj_grp_lock_release(transport->grp_lock);

		if(dis_id_count)
		{
			for(i=0; i< dis_id_count; i++)
			{
				remove_conn_from_transport(transport, dis_id[i]->conn_id);
				p2p_free(dis_id[i]);
			}
			p2p_free(dis_id);
		}

		//delay destroy transport
		if(transport->delay_destroy 
			&& pj_hash_count(transport->conn_hash_table) == 0)
		{
			p2p_transport_destroy_impl(transport);
		}
	}
}

static pj_status_t p2p_listener_udt_send(void* user_data, const pj_sockaddr_t* addr, const char* buffer, size_t buffer_len)
{
	p2p_transport *p2p = (p2p_transport*)user_data;
	pj_ice_strans_p2p_conn* conn = 0;
	pj_uint32_t hval=0;
	pj_status_t status = PJ_EGONE;

	if(p2p->destroy_req == PJ_TRUE)
		return status;

	pj_grp_lock_acquire(p2p->grp_lock);
	if(p2p->destroy_req == PJ_TRUE)
	{
		pj_grp_lock_release(p2p->grp_lock);
		return status;
	}
	conn  = pj_hash_get(p2p->udt_conn_hash_table, addr, pj_sockaddr_get_len(addr), &hval);
	if(conn)//*************************for multithreading, so add reference
	{
		if(!conn->destroy_req)
			pj_grp_lock_add_ref(conn->grp_lock);
		else
			conn = NULL;
	}
	pj_grp_lock_release(p2p->grp_lock);
	if(conn)
	{	
		status = p2p_ice_send_data(conn, addr, buffer, buffer_len);
		pj_grp_lock_dec_ref(conn->grp_lock);
	}
	return status;
}

pj_status_t p2p_listener_udt_on_accept(void* user_data, void* udt_sock, pj_sockaddr_t* addr)
{
	p2p_transport *p2p = (p2p_transport*)user_data;
	pj_ice_strans_p2p_conn* conn = 0;
	pj_uint32_t hval=0;

	if(p2p->destroy_req == PJ_TRUE)
		return PJ_EGONE;
	pj_grp_lock_acquire(p2p->grp_lock);
	if(p2p->destroy_req == PJ_TRUE)
	{
		pj_grp_lock_release(p2p->grp_lock);
		return PJ_EGONE;
	}
	conn  = pj_hash_get(p2p->udt_conn_hash_table, addr, pj_sockaddr_get_len(addr), &hval);
	if(conn)//*************************for multithreading, so add reference
	{
		pj_grp_lock_add_ref(conn->grp_lock);
		pj_grp_lock_add_ref(p2p->grp_lock);
	}
	pj_grp_lock_release(p2p->grp_lock);

	if(conn)
	{	
		p2p_udt_cb udt_cb;
		pj_sock_t sock;

		conn->udt_status = UDT_STATUS_CONNECTED;
		
		pj_ice_get_udt_socket(conn->icest, &sock);

		pj_bzero(&udt_cb, sizeof(p2p_udt_cb));
		udt_cb.udt_on_recved = &on_p2p_conn_recved_data;
		udt_cb.get_sock_addr = &p2p_conn_get_sock_addr;
		udt_cb.get_peer_addr = &p2p_conn_get_peer_addr;
		udt_cb.udt_pause_send = &p2p_conn_pause_send;
		udt_cb.udt_on_close = &p2p_conn_udt_on_close;
		udt_cb.udt_send = &p2p_ice_send_data;
		udt_cb.udt_on_noresend_recved = &on_p2p_conn_recved_noresend_data;

		//accept ok,cancel destroy_timer
		pj_timer_heap_cancel_if_active(get_p2p_global()->timer_heap, &conn->destroy_timer, 0);

		create_p2p_udt_accepter(&udt_cb, conn, conn->send_buf_size, conn->recv_buf_size, udt_sock, sock, &conn->udt.p2p_udt_accepter);
		//when accept udt socket, callback to user
		if(p2p->cb && p2p->cb->on_accept_remote_connection)
		{
			(*p2p->cb->on_accept_remote_connection)(p2p, conn->conn_id, conn->conn_flag, p2p->user_data);
		}
		pj_grp_lock_dec_ref(conn->grp_lock);
		pj_grp_lock_dec_ref(p2p->grp_lock);
	}
	return PJ_SUCCESS;
}

static void p2p_listener_get_sock_addr(pj_sockaddr_t* addr, void* user_data)
{
	p2p_transport *p2p = (p2p_transport*)user_data;
	int add_len;
	if(p2p && p2p->assist_icest)
	{
		pj_sockaddr_in_init(addr, NULL, 0);
		add_len = pj_sockaddr_get_len(addr);
		pj_ice_strans_get_turn_sockaddr(p2p->assist_icest, addr, &add_len);
	}
}

//callback to free memory of p2p_transport
static void p2p_transport_on_destroy(void *obj)
{
	p2p_transport *p2p = (p2p_transport*)obj;
	PJ_LOG(4,(p2p->obj_name, "p2p transport %p destroyed", obj));

	if(p2p->destroy_event)
	{
		pj_event_destroy(p2p->destroy_event);
		p2p->destroy_event = NULL;
	}

	delay_destroy_pool(p2p->pool);
}

void check_pj_thread()
{
	if(!pj_thread_is_registered())
	{
		pj_thread_desc* thread_desc = p2p_malloc(sizeof(pj_thread_desc));
		pj_thread_t *thread;
		pj_thread_register("thr%p", *thread_desc, &thread);
	}
}

P2P_DECL(void) p2p_thread_unregister()
{
	pj_thread_t* thread = pj_thread_this();
	if(thread)
		p2p_free(thread);
}

P2P_DECL(int) p2p_transport_create(p2p_transport_cfg* cfg,
								   p2p_transport **transport)
{
	pj_status_t status;
	pj_pool_t *pool;
	p2p_transport *p2p;
	pj_str_t local_addr = pj_str(LOCAL_HOST_IP);
	char guid[256];
	if(cfg == 0 || transport == 0 || cfg->server == 0 
		|| ((cfg->user == 0 || cfg->password == 0) && cfg->terminal_type == P2P_DEVICE_TERMINAL) )
		return PJ_EINVAL;

	check_pj_thread();

	PJ_LOG(4,("p2p", "p2p_transport_create begin"));

	pool = pj_pool_create(&get_p2p_global()->caching_pool.factory, 
		"p2p%p", 
		PJNATH_POOL_LEN_ICE_STRANS,
		PJNATH_POOL_INC_ICE_STRANS, 
		NULL);

	p2p = PJ_POOL_ZALLOC_T(pool, p2p_transport);
	pj_bzero(p2p, sizeof(p2p_transport));
	p2p->pool = pool;
	p2p->obj_name = pool->obj_name;
	p2p->user_data = cfg->user_data;
	p2p->first_assist_complete = PJ_TRUE;
	status = pj_grp_lock_create(pool, NULL, &p2p->grp_lock);
	if (status != PJ_SUCCESS)
	{
		pj_pool_release(pool);
		return status;
	}

	if(cfg->proxy_addr)
	{
		pj_strdup2_with_null(pool, &p2p->proxy_addr, cfg->proxy_addr);
	}
	else
	{
		pj_strdup_with_null(pool, &p2p->proxy_addr, &local_addr);
	}

	if(cfg->cb)
	{
		p2p->cb = PJ_POOL_ZALLOC_T(pool, p2p_transport_cb);
		pj_memcpy(p2p->cb, cfg->cb, sizeof(p2p_transport_cb));
	}
	else
	{
		p2p->cb = 0;
	}
	p2p->conn_hash_table = pj_hash_create(p2p->pool, CONN_HASH_TABLE_SIZE);
	p2p->udt_conn_hash_table = pj_hash_create(p2p->pool, CONN_HASH_TABLE_SIZE);
	/* Timer */
	pj_timer_entry_init(&p2p->timer, P2P_TIMER_NONE, p2p, &on_timer_event);

	pj_list_init(&p2p->disconnect_conns);

	if(cfg->terminal_type == P2P_CLIENT_TERMINAL)
	{
		sprintf(guid, "%s%p", get_p2p_global()->client_guid, p2p);
		cfg->user = guid;
		cfg->password = P2P_CLIENT_PASSWORD;
	}
	
	status = pj_event_create(p2p->pool, "p2p_destory_event", PJ_FALSE, PJ_FALSE, &p2p->destroy_event);
	if (status != PJ_SUCCESS)
	{
		pj_grp_lock_destroy(p2p->grp_lock);
		pj_pool_release(pool);
		return status;
	}

	//add self reference count
	pj_grp_lock_add_ref(p2p->grp_lock);
	pj_grp_lock_add_handler(p2p->grp_lock, pool, p2p, &p2p_transport_on_destroy);

	//create assist ice, connect server, call back is "cb_on_ice_assist_complete"
	status = create_assist_icest(p2p, cfg);
	if (status != PJ_SUCCESS)
	{
		PJ_LOG(4,(p2p->obj_name, "p2p transport %p create_assist_icest failed", p2p));
		p2p_transport_destroy_impl(p2p);
		return status;
	}

	PJ_LOG(4,(p2p->obj_name, "p2p transport created %p ", p2p));
	*transport = p2p;
	return PJ_SUCCESS;
}



P2P_DECL(void) p2p_transport_destroy(p2p_transport *transport)
{
	if(transport == 0 || transport->destroy_req || transport->delay_destroy)
		return ;

	check_pj_thread();

	PJ_LOG(4,(transport->obj_name, "p2p_transport_destroy %p begin", transport));

	//if conn_hash_table is not empty,delay destroy
	pj_grp_lock_acquire(transport->grp_lock);
	if(!transport->udt_listener)
	{
		if(pj_hash_count(transport->conn_hash_table))
		{
			transport->delay_destroy = PJ_TRUE;
			pj_grp_lock_release(transport->grp_lock);
			PJ_LOG(4,(transport->obj_name, "p2p_transport_destroy %p delay destroy", transport));
			return;
		}
	}

	pj_grp_lock_release(transport->grp_lock);

	p2p_transport_destroy_impl(transport);
	PJ_LOG(4,("p2p_destroy", "p2p_transport_destroy %p end", transport));
}

P2P_DECL(int) p2p_transport_connect(p2p_transport *transport,
									char* remote_user,
									void *user_data,
									int conn_flag,
									int* connection_id)
{
	pj_status_t status;
	p2p_conn_arg* arg;
	pj_str_t user = pj_str((char*)remote_user);
	pj_ice_strans_p2p_conn* conn;
	pj_uint32_t hval=0;
	pj_ice_strans_cfg * ice_cfg = 0;


	check_pj_thread();

	if(user.ptr == 0 || transport == 0)
		return PJ_EINVAL;
	PJ_LOG(3,(transport->obj_name, "p2p transport %p pj_p2p_transport_connect %s %p", transport, user.ptr, user_data));
	if(!transport->connected || transport->destroy_req || transport->delay_destroy)
	{
		PJ_LOG(3,(transport->obj_name, "p2p transport %p PJ_EINVALIDOP %d %d %d", transport, transport->connected , transport->destroy_req , transport->delay_destroy));
		return PJ_EINVALIDOP;
	}

	//for connect call back, save user and transport information in a p2p_conn_arg struct
	arg = (p2p_conn_arg*)p2p_malloc(sizeof(p2p_conn_arg));
	arg->conn_id = pj_atomic_inc_and_get(get_p2p_global()->atomic_id);
	arg->user_data = user_data;
	arg->transport = transport;
	arg->conn_flag = conn_flag;
	arg->remote_user = (char*)p2p_malloc(user.slen+1);
	pj_memcpy(arg->remote_user, user.ptr, user.slen);
	arg->remote_user[user.slen] = '\0';
	arg->internal_flag = 0;
	ice_cfg = pj_ice_strans_get_cfg(transport->assist_icest);
	if(ice_cfg && ice_cfg->turn.conn_type == PJ_TURN_TP_TCP)
		arg->internal_flag |= INTERNAL_FLAG_TCP;
			
	
	*connection_id = arg->conn_id;
	conn = create_p2p_conn(&transport->proxy_addr, PJ_TRUE);
	pj_grp_lock_acquire(transport->grp_lock);
	if (pj_hash_get(transport->conn_hash_table, connection_id, sizeof(pj_int32_t),	&hval) == NULL) 
	{		
		conn->conn_id = *connection_id;
		conn->transport = transport;
		conn->hash_value = hval;
		conn->conn_flag = conn_flag;
		conn->is_initiative = PJ_TRUE;
		pj_strdup2_with_null(conn->pool, &conn->remote_user, remote_user);
		pj_strdup_with_null(conn->pool, &conn->user, &ice_cfg->turn.auth_cred.data.static_cred.username);

		pj_gettimeofday(&conn->connect_begin_time);
		pj_hash_set(transport->pool, transport->conn_hash_table, connection_id, sizeof(pj_int32_t), hval, conn);
	}
	pj_grp_lock_release(transport->grp_lock);

	//call back is "on_p2p_connect"
	status = pj_ice_strans_p2p_connnect(transport->assist_icest, &user, arg);
	if (status != PJ_SUCCESS)
	{
		PJ_LOG(4,(transport->obj_name, "p2p transport %p pj_p2p_transport_connect failed", transport));
		*connection_id = 0;
		p2p_free(arg->remote_user);
		p2p_free(arg);
		return status;
	}


	PJ_LOG(3,(transport->obj_name, "p2p transport %p pj_p2p_transport_connect conn_id %d remote_user %s end", transport, *connection_id, remote_user));
	return PJ_SUCCESS;
}

P2P_DECL(void) p2p_transport_disconnect(p2p_transport *transport, int connection_id)
{
	pj_time_val delay = {0, 0};
	p2p_disconnection_id* item;
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;

	if(transport == 0 || !transport->connected)
		return;

	check_pj_thread();

	PJ_LOG(3,(transport->obj_name, "pj_p2p_transport_disconnect %p conn_id %d", transport, connection_id));

	pj_grp_lock_acquire(transport->grp_lock);

	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn && !conn->destroy_req)
	{
		if(!conn->disconnect_req)
		{
			//schedule a timer,put it in io network thread 
			item = p2p_malloc(sizeof(p2p_disconnection_id)); 
			item->conn_id = connection_id;

			conn->disconnect_req = PJ_TRUE;

			pj_list_push_back(&transport->disconnect_conns, item);
			pj_timer_heap_schedule_w_grp_lock(get_p2p_global()->timer_heap, &transport->timer,
				&delay, P2P_DISCONNECT_CONNECTION,
				transport->grp_lock);
		}
	}
	
	pj_grp_lock_release(transport->grp_lock);

}

P2P_DECL(int) p2p_get_conn_addr(p2p_transport *transport, int connection_id, char* addr, int* addr_len, p2p_addr_type* addr_type, int is_local)
{
	pj_ice_strans_p2p_conn* conn;
	pj_status_t status = PJ_SUCCESS;
	pj_uint32_t hval=0;

	if(transport == 0 || addr == 0 ||*addr_len <= 0)
		return PJ_EINVAL;
	if(!transport->connected)
		return PJ_EINVALIDOP;
	if(transport->destroy_req)
		return PJ_EGONE;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn && !conn->destroy_req)
	{	
		char addr_info[PJ_INET6_ADDRSTRLEN+10];
		int len;
		if(is_local)
			pj_sockaddr_print(&conn->local_addr, addr_info, sizeof(addr_info), 3); //3 is include port
		else
			pj_sockaddr_print(&conn->remote_addr, addr_info, sizeof(addr_info), 3); //3 is include port
		len = strlen(addr_info);
		if(addr)
		{
			if(*addr_len > len+1)
				strcpy(addr, addr_info);
			else
				*addr_len = len+1;
		}
		else
		{
			*addr_len = len+1;
		}
		if(addr_type)
		{
			if(is_local)
				*addr_type = conn->local_addr_type;
			else
				*addr_type = conn->remote_addr_type;
		}
	}
	else
		status = PJ_EGONE;
	pj_grp_lock_release(transport->grp_lock);

	return status;
}


P2P_DECL(int) p2p_get_conn_remote_addr(p2p_transport *transport, int connection_id, char* addr, int* addr_len, p2p_addr_type* addr_type)
{
	return p2p_get_conn_addr(transport, connection_id, addr, addr_len, addr_type, 0);
}

P2P_DECL(int) p2p_get_conn_local_addr(p2p_transport *transport, int connection_id, char* addr, int* addr_len, p2p_addr_type* addr_type)
{
	return p2p_get_conn_addr(transport, connection_id, addr, addr_len, addr_type, 1);
}

P2P_DECL(int) p2p_set_conn_opt(p2p_transport *transport, int connection_id, p2p_opt opt, const void* optval, int optlen)
{
	pj_ice_strans_p2p_conn* conn;
	pj_status_t status = PJ_SUCCESS;
	pj_uint32_t hval=0;

	if(transport == 0)
		return PJ_EINVAL;
	if(!transport->connected)
		return PJ_EINVALIDOP;
	if(transport->destroy_req)
		return PJ_EGONE;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn && !conn->destroy_req)
		status = p2p_conn_set_opt(conn, opt, optval, optlen);
	else
		status = PJ_EGONE;
	pj_grp_lock_release(transport->grp_lock);

	return status;
}

static int p2p_transport_type_send(p2p_transport *transport,
							  int connection_id,
							  char* buffer,
							  int len,
							  p2p_send_model model,
							  int type,
							  int* error_code)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	if(transport == 0 || len <= 0)
	{
		if(error_code)
			*error_code = PJ_EINVAL;
		return -1;
	}
	if(!transport->connected)
	{
		if(error_code)
			*error_code = PJ_EINVALIDOP;
		return -1;
	}
	if(transport->destroy_req || transport->delay_destroy)
	{
		if(error_code)
			*error_code = PJ_EGONE;
		return 0;
	}

	check_pj_thread();

	//PJ_LOG(3,(transport->obj_name, "p2p_transport_send pj_hash_get begin"));
	
	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn)//*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_release(transport->grp_lock);

	//PJ_LOG(3,(transport->obj_name, "p2p_transport_send pj_hash_get end"));

	if(conn)
	{
		pj_status_t status = PJ_EINVALIDOP;
		if(conn->is_initiative)
		{
			if(conn->udt.p2p_udt_connector)
				status = p2p_udt_connector_model_send(conn->udt.p2p_udt_connector, buffer, len, model, type);
		}
		else
		{
			if(conn->udt.p2p_udt_accepter)
				status = p2p_udt_accepter_model_send(conn->udt.p2p_udt_accepter, buffer, len, model, type);
		}
		
		//PJ_LOG(3,(transport->obj_name, "p2p_transport_send model_send end"));

		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
		if(error_code)
			*error_code = status;

		//PJ_LOG(3,(transport->obj_name, "p2p_transport_send return"));
		return status==PJ_SUCCESS ? len : -1;	
	}
	else
	{
		if(error_code)
			*error_code = PJ_EGONE;
		return -1;
	}
}

P2P_DECL(int) p2p_transport_av_send(p2p_transport *transport,
									int connection_id,
									char* buffer,
									int len,
									int flag, //0 no resend,  1 resend
									p2p_send_model model,
									int* error_code)
{
	if(flag == 1)
	{
		return p2p_transport_type_send(transport, connection_id, buffer, len, model, 
			P2P_DATA_AV, error_code);	
	}
	else
	{
		return p2p_transport_type_send(transport, connection_id, buffer, len, model, 
			P2P_DATA_AV_NO_RESEND, error_code);	
	}
}

P2P_DECL(int) p2p_transport_send(p2p_transport *transport,
								 int connection_id,
								 char* buffer,
								 int len,
								 p2p_send_model model,
								 int* error_code)
{
	return p2p_transport_type_send(transport, connection_id, buffer, len, model, P2P_DATA_COMMON, error_code);	
}

static pj_status_t tcp_listen_proxy_send_data(p2p_tcp_listen_proxy* listen_proxy,
							 const char* buffer,
							 size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)listen_proxy->user_data;
	pj_status_t status = PJ_EGONE;
	if(conn->udt.p2p_udt_connector)
	{
		status = p2p_udt_connector_send(conn->udt.p2p_udt_connector, buffer, buffer_len);
	}
	return status;
}

#ifdef USE_UDP_PROXY
static pj_status_t udp_listen_proxy_send_data(p2p_udp_listen_proxy* listen_proxy,
											  const char* buffer,
											  size_t buffer_len)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)listen_proxy->user_data;
	pj_status_t status = PJ_EGONE;
	if(conn->udt.p2p_udt_connector)
	{
		status = p2p_udt_connector_send(conn->udt.p2p_udt_connector, buffer, buffer_len);
	}
	return status;
}

static void udp_listen_proxy_idea_timeout(p2p_udp_listen_proxy* listen_proxy)
{
	pj_ice_strans_p2p_conn* conn = (pj_ice_strans_p2p_conn*)listen_proxy->user_data;
	p2p_destroy_udp_proxy(conn->transport, conn->conn_id, listen_proxy->proxy_port);
}
#endif


P2P_DECL(int) p2p_create_tcp_proxy(p2p_transport *transport, 
								   int connection_id, 
								   unsigned short remote_listen_port,
								   unsigned short* local_proxy_port)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	p2p_tcp_listen_proxy* proxy;
	pj_status_t status = PJ_SUCCESS;
	if(transport == 0)
		return PJ_EINVAL;
	if(!transport->connected)
		return PJ_EINVALIDOP;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn && conn->is_initiative)//*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	else
		status = PJ_EGONE;
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
	{
		p2p_tcp_listen_proxy_cb cb;
		cb.send_tcp_data = tcp_listen_proxy_send_data;

		status = create_p2p_tcp_listen_proxy(remote_listen_port, &cb, conn, &proxy);
		if(status == PJ_SUCCESS)
		{
			*local_proxy_port = proxy->proxy_port;
			hval = 0;
			pj_grp_lock_acquire(conn->grp_lock);
			if (pj_hash_get(conn->tcp_listen_proxys, local_proxy_port, sizeof(pj_uint16_t),	&hval) == NULL) 
			{		
				pj_hash_set(conn->pool, conn->tcp_listen_proxys, local_proxy_port, sizeof(pj_uint16_t), hval, proxy);
				pj_grp_lock_add_ref(conn->grp_lock); //----***********release it in pj_p2p_destroy_tcp_proxy
				proxy->hash_value = hval;
			}
			pj_grp_lock_release(conn->grp_lock);
		}
		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
	}
	return status;
}

P2P_DECL(void) p2p_destroy_tcp_proxy(p2p_transport *transport,
									 int connection_id,
									 unsigned short local_proxy_port)
{
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	p2p_tcp_listen_proxy* proxy;
	if(transport == 0 || !transport->connected)
		return;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn) //*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
	{
		hval = 0;
		pj_grp_lock_acquire(conn->grp_lock);
		proxy = pj_hash_get(conn->tcp_listen_proxys, &local_proxy_port, sizeof(pj_uint16_t), &hval) ;
		if(proxy) //use pj_hash_set NULL, remove from hash table
			pj_hash_set(NULL, conn->tcp_listen_proxys, &local_proxy_port, sizeof(pj_uint16_t), proxy->hash_value, NULL);
		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
		pj_grp_lock_release(conn->grp_lock);

		if(proxy)
		{
			destroy_p2p_tcp_listen_proxy(proxy);
			pj_grp_lock_dec_ref(conn->grp_lock);//----*********** when listen proxy created,add conn reference,so release it
		}
	}
}

P2P_DECL(int) p2p_create_udp_proxy(p2p_transport *transport, 
								   int connection_id, 
								   unsigned short remote_udp_port,
								   unsigned short* local_proxy_port)
{
#ifdef USE_UDP_PROXY
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	p2p_udp_listen_proxy* proxy;
	pj_status_t status = PJ_SUCCESS;
	if(transport == 0)
		return PJ_EINVAL;
	if(!transport->connected)
		return PJ_EINVALIDOP;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn && conn->is_initiative)//*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	else
		status = PJ_EGONE;
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
	{
		p2p_udp_listen_proxy_cb cb;
		cb.send_udp_data = udp_listen_proxy_send_data;
		cb.on_idea_timeout = udp_listen_proxy_idea_timeout;
		status = create_p2p_udp_listen_proxy(remote_udp_port, &cb, conn, &proxy);
		if(status == PJ_SUCCESS)
		{
			*local_proxy_port = proxy->proxy_port;
			hval = 0;
			pj_grp_lock_acquire(conn->grp_lock);
			if (pj_hash_get(conn->udp_listen_proxys, &proxy->remote_udp_port, sizeof(pj_uint16_t),	&hval) == NULL) 
			{		
				pj_hash_set(conn->pool, conn->udp_listen_proxys, &proxy->remote_udp_port, sizeof(pj_uint16_t), hval, proxy);
				pj_grp_lock_add_ref(conn->grp_lock); //----***********release it in pj_p2p_destroy_udp_proxy
				proxy->hash_value = hval;
			}
			pj_grp_lock_release(conn->grp_lock);
		}
		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
	}
	return status;
#else
	PJ_UNUSED_ARG(transport);
	PJ_UNUSED_ARG(connection_id);
	PJ_UNUSED_ARG(remote_udp_port);
	PJ_UNUSED_ARG(local_proxy_port);
	return PJ_ENOTSUP;
#endif
}

P2P_DECL(void) p2p_destroy_udp_proxy(p2p_transport *transport,
									 int connection_id,
									 unsigned short local_proxy_port)
{
#ifdef USE_UDP_PROXY
	pj_uint32_t hval=0;
	pj_ice_strans_p2p_conn* conn;
	
	if(transport == 0 || !transport->connected)
		return;

	check_pj_thread();

	pj_grp_lock_acquire(transport->grp_lock);
	conn  = pj_hash_get(transport->conn_hash_table, &connection_id, sizeof(pj_int32_t), &hval);
	if(conn) //*************************for multithreading, so add reference
		pj_grp_lock_add_ref(conn->grp_lock);
	pj_grp_lock_release(transport->grp_lock);
	if(conn)
	{
		p2p_udp_listen_proxy* proxy = NULL;
		pj_hash_iterator_t itbuf, *it;

		pj_grp_lock_acquire(conn->grp_lock);

		it = pj_hash_first(conn->udp_listen_proxys, &itbuf);
		while(it) 
		{
			p2p_udp_listen_proxy* p = (p2p_udp_listen_proxy*)pj_hash_this(conn->udp_listen_proxys, it);
			if(p->proxy_port == local_proxy_port)
			{
				proxy = p;
				pj_hash_set(NULL, conn->udp_listen_proxys, &proxy->remote_udp_port, sizeof(pj_uint16_t), proxy->hash_value, NULL);
				break;
			}			
			it = pj_hash_next(conn->udp_listen_proxys, it);
		}
		
		pj_grp_lock_dec_ref(conn->grp_lock);//***********************for multithreading, free reference
		pj_grp_lock_release(conn->grp_lock);

		if(proxy)
		{
			destroy_p2p_udp_listen_proxy(proxy);
			pj_grp_lock_dec_ref(conn->grp_lock);//----*********** when listen proxy created,add conn reference,so release it
		}
	}
#else
	PJ_UNUSED_ARG(transport);
	PJ_UNUSED_ARG(connection_id);
	PJ_UNUSED_ARG(local_proxy_port);
#endif
}

P2P_DECL(void) p2p_strerror(int error_code,
						   char *buf,
						   int bufsize)
{
	pj_strerror(error_code, buf, bufsize);
}

typedef struct nat_type_detector 
{
	ON_DETECT_NET_TYPE callback;
	void* user_data;
}nat_type_detector;

void p2p_nat_type_detect_cb(void *user_data, const pj_stun_nat_detect_result *res)
{
	if(user_data)
	{
		nat_type_detector* d = (nat_type_detector*)user_data;
		(*d->callback)(res->status, res->nat_type, d->user_data);
		p2p_free(d);
	}
}

P2P_DECL(int) p2p_nat_type_detect(char* turn_server, unsigned short turn_port, ON_DETECT_NET_TYPE callback, void* user_data)
{
	nat_type_detector* d = p2p_malloc(sizeof(nat_type_detector));
	check_pj_thread();
	d->user_data = user_data;
	d->callback = callback;
	return p2p_detect_nat_type(turn_server, turn_port, d, p2p_nat_type_detect_cb);
}

P2P_DECL(char*) p2p_get_ver()
{
	return P2P_VERSION;
}

P2P_DECL(int) p2p_proxy_get_remote_addr(p2p_transport *transport, unsigned short port, char* addr, int* add_len)
{
	pj_hash_iterator_t itbuf, *it;
	pj_ice_strans_p2p_conn** conn = NULL;
	unsigned int conn_count = 0;
	unsigned int i=0;
	pj_bool_t found = PJ_FALSE;

	if(transport == 0 || addr == 0 || *add_len <= 0)
		return PJ_EINVAL;
	if(!transport->connected)
		return PJ_EINVALIDOP;
	if(transport->destroy_req)
		return PJ_EGONE;

	check_pj_thread();

	PJ_LOG(4,(transport->obj_name, "p2p_proxy_get_remote_addr %p port %d", transport, port));


	pj_grp_lock_acquire(transport->grp_lock);
	//prevent deadlock, get items in hash table
	conn_count = pj_hash_count(transport->conn_hash_table);
	if(conn_count)
		conn = (pj_ice_strans_p2p_conn**)p2p_malloc(sizeof(pj_ice_strans_p2p_conn*)*conn_count);

	it = pj_hash_first(transport->conn_hash_table, &itbuf);
	while (it) 
	{
		conn[i] = (pj_ice_strans_p2p_conn*) pj_hash_this(transport->conn_hash_table, it);
		pj_grp_lock_add_ref(conn[i]->grp_lock);
		i++;
		it = pj_hash_next(transport->conn_hash_table, it);
	}
	pj_grp_lock_release(transport->grp_lock);

	for(i=0; i<conn_count; i++)
	{
		if(!conn[i]->is_initiative && found == PJ_FALSE)
		{
			pj_status_t status = p2p_conn_proxy_get_remote_addr(conn[i], port, addr, add_len);
			if(status == PJ_SUCCESS)
				found = PJ_TRUE;
		}
		pj_grp_lock_dec_ref(conn[i]->grp_lock);
	}
	if(conn)
		p2p_free(conn);

	PJ_LOG(4,(transport->obj_name, "p2p_proxy_get_remote_addr %p port %d, result %d", transport, port, found));

	return found ? PJ_SUCCESS : PJ_ENOTFOUND;
	
}

P2P_DECL(int) p2p_transport_server_net_state(p2p_transport *transport){
	if(transport == 0
		|| transport->connected == PJ_FALSE
		|| transport->assist_icest == NULL)
		return PJ_STUN_MAX_TRANSMIT_COUNT;

	return pj_ice_strans_server_net_state(transport->assist_icest);
}
