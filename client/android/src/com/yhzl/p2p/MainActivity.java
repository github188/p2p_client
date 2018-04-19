package com.yhzl.p2p;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

import com.yhzl.p2p.R;

import android.os.Bundle;
import android.os.Environment;
import android.os.Message;
import android.app.Activity;
import android.os.Handler;
import android.widget.Button;
import android.widget.EditText;
import android.view.View;

public class MainActivity extends Activity implements
		P2PClient.P2PClientCallBack, P2PClient.P2PDetectNatCallBack, P2PClient.P2PClientDispatchCallBack {
	static {
		System.loadLibrary("p2pJni");
	}

	EditText editState;
	private P2PMsgHandler p2pMsgHandler = null;
	private SendFileThread sendFileThread = null;

	private P2PResult p2pResult;
	private int p2pTransport = 0;
	private int p2pConn = 0;
	private int p2pStatus = 0;
	private int p2pLocalListenPort = 0;
	private int natType = 0;
	
	private String server;
	private String user;
	private String pwd;
	private int port;
	
	private String ds_result_server;
	private int ds_result_port;
	private int ds_result_server_id;

	static final private int CREATE_TRANSPORT_MSG = 1;
	static final private int CONNECT_USER_MSG = 2;
	static final private int DISCONNECT_MSG = 3;
	static final private int ACCEPT_USER_MSG = 4;
	static final private int DECTET_NAT_MSG = 5;
	static final private int DISPATCH_RESULT_MSG = 6;
	
	private boolean check_input()
	{
		EditText edit = (EditText) findViewById(R.id.edtTurnServer);
		server = edit.getText().toString().trim();
		if (server.length() == 0) {
			addStateText("server is empty!");
			return false;
		}

		edit = (EditText) findViewById(R.id.edtTurnPort);
		String strPort = edit.getText().toString();
		if (strPort.length() == 0) {
			addStateText("port is empty!");
			return false;
		}
		port = Integer.parseInt(strPort);
		if(port <= 0|| port >= 65535){
			addStateText("port is invalid!");
			return false;
		}

		edit = (EditText) findViewById(R.id.edtUser);
		user = edit.getText().toString().trim();

		edit = (EditText) findViewById(R.id.edtPwd);
		pwd = edit.getText().toString().trim();
		return true;
	}
	
	private class P2pCreateTransportListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0) {
				addStateText("p2pTransport !=0 !");
				return;
			}
			
			if(!check_input())
				return;

			p2p_transport_cfg cfg = new p2p_transport_cfg();
			cfg.server = server;
			cfg.port = port;
			cfg.user = user;
			cfg.password = pwd;
			
			if(cfg.user.length() == 0)
				cfg.terminal_type = p2p_transport_cfg.P2P_CLIENT_TERMINAL;
			else
				cfg.terminal_type = p2p_transport_cfg.P2P_DEVICE_TERMINAL;
			cfg.user_data = 0;
			cfg.use_tcp_connect_srv = 0;
			cfg.cb = MainActivity.this;
			//cfg.proxy_addr = null;
			p2pResult = P2PClient.p2p_transport_create(cfg);
			addStateText("p2p_transport_create return:" + p2pResult.result
					+ "," + p2pResult.value);

			if (p2pResult.result == 0)
				p2pTransport = p2pResult.value;
		}
	}

	private class P2pdestroyTransportListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0) {
				P2PClient.p2p_transport_destroy(p2pTransport);
				p2pTransport = 0;
				addStateText("p2p transport is destoryed");
			} else {
				addStateText("p2p transport is 0");
			}
		}
	}

	private class P2pDisconnectUserListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0 && p2pConn != 0) {
				P2PClient.p2p_transport_disconnect(p2pTransport, p2pConn);
				p2pTransport = 0;
				addStateText("p2p connection is disconnected");
			} else {
				addStateText("p2p transport is 0 or p2p connection is 0");
			}
		}
	}

	private class P2pConnectUserListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0) {
				EditText edit = (EditText) findViewById(R.id.edtRemoteUser);
				String remote_user = edit.getText().toString().trim();
				if (remote_user.length() == 0) {
					addStateText("remote user is empty!");
					return;
				}
				p2pResult = P2PClient.p2p_transport_connect(p2pTransport, remote_user,
						0);

				addStateText("p2p_transport_connect return:" + p2pResult.result
						+ "," + p2pResult.value);

				if (p2pResult.result == 0)
					p2pConn = p2pResult.value;
			} else {
				addStateText("p2p transport is 0");
			}
		}
	}

	static class P2PMsgHandler extends Handler {
		private MainActivity mainActivity;

		public P2PMsgHandler(MainActivity activity) {
			mainActivity = activity;
		}

		@Override
		public void handleMessage(Message msg) {
			super.handleMessage(msg);
			mainActivity.onP2PMsg(msg);
		}
	}

	private class P2pCreateProxyListener implements Button.OnClickListener {
		public void onClick(View v) {
			EditText edit = (EditText) findViewById(R.id.edtRemotePort);
			String strPort = edit.getText().toString();
			int remote_listen_port = 0;

			try {
				remote_listen_port = Integer.parseInt(strPort);
			} catch (Exception e) {
				e.printStackTrace();
			}
			if (strPort.length() == 0 || remote_listen_port == 0) {
				addStateText("listen port invalid!");
				return;
			}
			if (p2pTransport != 0 && p2pConn != 0) {
				p2pResult = P2PClient.p2p_create_tcp_proxy(p2pTransport,
						p2pConn, remote_listen_port);
				if (p2pResult.result == 0)
					p2pLocalListenPort = p2pResult.value;

				addStateText("p2p_create_tcp_proxy return:" + p2pResult.result
						+ "," + p2pResult.value);
			} else {
				addStateText("p2p transport is 0 or p2p connection is 0");
			}
		}
	}

	private class P2pdestroyProxyListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0 && p2pConn != 0 && p2pLocalListenPort != 0) {
				P2PClient.p2p_destroy_tcp_proxy(p2pTransport, p2pConn,
						p2pLocalListenPort);
				addStateText("p2p tcp proxy is destoryed");
			} else {
				addStateText("p2p transport is 0 or p2p connection is 0");
			}
		}
	}

	private class SendFileThread extends Thread {
		public void run() {

			super.run();
			try {
				File sd = Environment.getExternalStorageDirectory();
				String path = sd.getPath() + "/p2psend";
				FileInputStream fin = new FileInputStream(path);
				byte[] buffer = new byte[40960];

				while (true) {
					int readed = fin.read(buffer);
					if (readed == -1)
						break;
					P2PResult r = P2PClient.p2p_transport_send(p2pTransport,
							p2pConn, buffer, readed);
					if (r.result <= 0)
						break;
				}
				fin.close();
			} catch (Exception e) {
				e.printStackTrace();
			}
			sendFileThread = null;
		}
	}

	private class P2pSendFileListener implements Button.OnClickListener {
		public void onClick(View v) {
			if (p2pTransport != 0 && p2pConn != 0 && sendFileThread == null) {
				sendFileThread = new SendFileThread();
				sendFileThread.start();
			} else {
				addStateText("p2p transport is 0 or p2p connection is 0 or sending");
			}
		}
	}
	
	private class P2pRequestDsListener implements Button.OnClickListener {
		public void onClick(View v) {
			if(!check_input())
				return;
			EditText edit = (EditText) findViewById(R.id.edtDsTurnPort);
			String strPort = edit.getText().toString();
			if (strPort.length() == 0) {
				addStateText("ds port is empty!");
				return ;
			}
			int ds_port = Integer.parseInt(strPort);
			if(ds_port <= 0|| ds_port >= 65535){
				addStateText("ds port is invalid!");
				return;
			}
			if (user.length() == 0) {
				addStateText("user is empty!");
				return;
			}
			if (pwd.length() == 0) {
				addStateText("password is empty!");
				return;
			}
			String ds_addr = server + ":" + ds_port;
			P2PClient.p2p_request_dispatch_server(user, pwd, ds_addr, 0, MainActivity.this);
		}
	}
	
	private class P2pQueryDsListener implements Button.OnClickListener {
		public void onClick(View v) {
			if(!check_input())
				return;
			EditText edit = (EditText) findViewById(R.id.edtDsTurnPort);
			String strPort = edit.getText().toString();
			if (strPort.length() == 0) {
				addStateText("ds port is empty!");
				return ;
			}
			int ds_port = Integer.parseInt(strPort);
			if(ds_port <= 0|| ds_port >= 65535){
				addStateText("ds port is invalid!");
				return;
			}
			String ds_addr = server + ":" + ds_port;
			
			edit = (EditText) findViewById(R.id.edtRemoteUser);
			String remote_user = edit.getText().toString().trim();
			if (remote_user.length() == 0) {
				addStateText("remote user is empty!");
				return;
			}
			P2PClient.p2p_query_dispatch_server(remote_user, ds_addr, 0, MainActivity.this);
		}
	}

	@Override
	protected void onDestroy() {
		P2PClient.uninit();
		super.onDestroy();
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		P2PClient.init();

		p2pMsgHandler = new P2PMsgHandler(this);

		editState = (EditText) findViewById(R.id.editState);
		editState.setHorizontallyScrolling(true);

		Button button = (Button) findViewById(R.id.p2p_connect_svr);
		P2pCreateTransportListener listener = new P2pCreateTransportListener();
		button.setOnClickListener(listener);

		button = (Button) findViewById(R.id.p2p_disconnect_svr);
		P2pdestroyTransportListener destory_listener = new P2pdestroyTransportListener();
		button.setOnClickListener(destory_listener);

		button = (Button) findViewById(R.id.p2p_connect_user);
		P2pConnectUserListener connect_listener = new P2pConnectUserListener();
		button.setOnClickListener(connect_listener);

		button = (Button) findViewById(R.id.p2p_disconnect_user);
		P2pDisconnectUserListener disconnect_listener = new P2pDisconnectUserListener();
		button.setOnClickListener(disconnect_listener);

		button = (Button) findViewById(R.id.p2p_create_proxy);
		P2pCreateProxyListener create_proxy_listener = new P2pCreateProxyListener();
		button.setOnClickListener(create_proxy_listener);

		button = (Button) findViewById(R.id.p2p_destory_proxy);
		P2pdestroyProxyListener destory_proxy_listener = new P2pdestroyProxyListener();
		button.setOnClickListener(destory_proxy_listener);

		button = (Button) findViewById(R.id.p2p_send_file);
		P2pSendFileListener send_file_listener = new P2pSendFileListener();
		button.setOnClickListener(send_file_listener);
		
		button = (Button) findViewById(R.id.p2p_request_dispatch);
		P2pRequestDsListener request_ds = new P2pRequestDsListener();
		button.setOnClickListener(request_ds);

		button = (Button) findViewById(R.id.p2p_query_dispatch);
		P2pQueryDsListener query_ds = new P2pQueryDsListener();
		button.setOnClickListener(query_ds);

		P2PClient.p2p_nat_type_detect("stun.ekiga.net", 3478, this);
	}

	@Override
	public void on_create_complete(int transport, int status, int user_data) {
		p2pStatus = status;
		p2pMsgHandler.sendEmptyMessage(CREATE_TRANSPORT_MSG);
	}

	@Override
	public void on_connect_complete(int transport, int connection_id,
			int status, int transport_user_data, int connect_user_data) {
		p2pStatus = status;
		// String err = P2PClient.p2p_strerror(p2pStatus);
		p2pMsgHandler.sendEmptyMessage(CONNECT_USER_MSG);
	}

	@Override
	public void on_connection_disconnect(int transport, int connection_id,
			int transport_user_data, int connect_user_data) {
		p2pMsgHandler.sendEmptyMessage(DISCONNECT_MSG);
	}

	@Override
	public void on_accept_remote_connection(int transport, int connection_id,
			int transport_user_data) {
		p2pMsgHandler.sendEmptyMessage(ACCEPT_USER_MSG);
	}

	@Override
	public void on_connection_recv(int transport, int connection_id,
			int transport_user_data, int connect_user_data, byte[] data) {

		/*try {
			File sd = Environment.getExternalStorageDirectory();
			String path = sd.getPath() + "/p2precv";
			FileOutputStream fout = new FileOutputStream(path, true);
			fout.write(data);
			fout.close();
		} catch (Exception e) {
			e.printStackTrace();
		}*/

	}

	@Override
	public void on_detect_nat(int status, int nat_type) {
		p2pStatus = status;
		natType = nat_type;
		p2pMsgHandler.sendEmptyMessage(DECTET_NAT_MSG);
	}

	private void addStateText(String text) {
		String newText = editState.getText().toString() + text + "\r\n";
		editState.setText(newText);
	}

	private void onP2PMsg(Message msg) {
		String text = "";
		switch (msg.what) {
		case CREATE_TRANSPORT_MSG:
			if (p2pStatus != 0) {
				P2PClient.p2p_transport_destroy(p2pTransport);
				p2pTransport = 0;
			}
			text = "on_create_complete status:" + p2pStatus;
			break;
		case CONNECT_USER_MSG:
			text = "on_connect_complete status:" + p2pStatus;
			if (p2pStatus == 0)
				text += ",\r\nremote ip:"
						+ P2PClient.p2p_get_conn_remote_addr(p2pTransport,
								p2pConn)
						+ ",\r\nlocal ip:"
						+ P2PClient.p2p_get_conn_local_addr(p2pTransport,
								p2pConn);
			break;
		case DISCONNECT_MSG:
			text = "p2p connection is callback disconnected";
			p2pConn = 0;
			break;
		case ACCEPT_USER_MSG:
			text = "accept a remote user";
			break;
		case DECTET_NAT_MSG:
			text = "detect nat, status:" + p2pStatus + ",type:" + natType;
			break;
		case DISPATCH_RESULT_MSG:
			if (p2pStatus == 0)
				text = "dispatch result:" + ds_result_server + ","
						+ ds_result_port + "," + ds_result_server_id;
			else
				text = "dispatch result:" + p2pStatus;
			break;
		}
		addStateText(text);
	}

	@Override
	public void on_dispatch_result(int status, int user_data, String server,
			int port, int server_id) {
		// TODO Auto-generated method stub
		p2pStatus = status;
		ds_result_server = server;
		ds_result_port = port;
		ds_result_server_id = server_id;
		p2pMsgHandler.sendEmptyMessage(DISPATCH_RESULT_MSG);
	}
}
