package com.yhzl.p2p;

import com.yhzl.p2p.P2PClient.P2PClientCallBack;

public class p2p_transport_cfg {
	
	public final static int P2P_DEVICE_TERMINAL = 0;
	public final static int  P2P_CLIENT_TERMINAL = 1;
	
	public String server;
	public int port;
	public int terminal_type;
	public String user;
	public String password;
	public int user_data;
	public int use_tcp_connect_srv;
	public String proxy_addr;
	public P2PClientCallBack cb;
}