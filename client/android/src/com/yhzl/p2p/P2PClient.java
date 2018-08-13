package com.yhzl.p2p;

public class P2PClient {

	public static final int P2P_SUCCESSED = 0;

	public interface P2PClientCallBack {
		void on_create_complete(int transport, int status, int user_data);

		void on_connect_complete(int transport, int connection_id, int status,
				int transport_user_data, int connect_user_data);

		void on_connection_disconnect(int transport, int connection_id,
				int transport_user_data, int connect_user_data);

		void on_accept_remote_connection(int transport, int connection_id,
				int transport_user_data);

		void on_connection_recv(int transport, int connection_id,
				int transport_user_data, int connect_user_data, byte[] data);
	}
	
	public interface P2PClientDispatchCallBack {
		void on_dispatch_result(int status, int user_data, String server, int port,
				int server_id);
	}

	public interface P2PDetectNatCallBack {
		void on_detect_nat(int status, int nat_type);
	}

	static native public int init();

	static native public void uninit();

	static private native P2PResult p2p_transport_create(String server,
			int port, String user, String password, int terminal_type, int user_data,
			int use_tcp_connect_srv, String proxy_addr, Object cb);

	static public P2PResult p2p_transport_create(p2p_transport_cfg cfg) {
		return p2p_transport_create(cfg.server, cfg.port, cfg.user,
				 cfg.password, cfg.terminal_type, cfg.user_data,
				cfg.use_tcp_connect_srv, cfg.proxy_addr, cfg.cb);
	}

	static public native void p2p_transport_destroy(int transport);

	static public native P2PResult p2p_transport_connect(int transport,
			String user, int user_data);

	static public native void p2p_transport_disconnect(int transport,
			int conn_id);

	static public native P2PResult p2p_transport_send(int transport,
			int conn_id, byte[] buffer, int buffer_len);

	static public native P2PResult p2p_create_tcp_proxy(int transport,
			int conn_id, int remote_listen_port);

	static public native void p2p_destroy_tcp_proxy(int transport, int conn_id,
			int local_proxy_port);

	static public native String p2p_strerror(int error);

	static public native int p2p_nat_type_detect(String server, int port,
			P2PDetectNatCallBack cb);
	
	//return value is ip:port:type
	static public native String p2p_get_conn_remote_addr(int transport, int conn_id);
	static public native String p2p_get_conn_local_addr(int transport, int conn_id);
	
	static public native int p2p_request_dispatch_server(String user,
			String password,String ds_addr, int user_data,
			P2PClientDispatchCallBack cb);

	static public native int p2p_query_dispatch_server(String dest_user, String ds_addr, int user_data,
			P2PClientDispatchCallBack cb);
	
	public static final int P2P_SNDBUF = 0;
	public static final int P2P_RCVBUF = 1;
	static public native int p2p_conn_set_buf_size(int transport, int conn_id, int opt, int size);
}
