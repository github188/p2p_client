LOCAL_PATH := $(call my-dir)
SOURCE_PATH := ../..

include $(CLEAR_VARS)

LOCAL_MODULE := p2pJni

LOCAL_C_INCLUDES := \
$(LOCAL_PATH)/$(SOURCE_PATH)/miniupnpc-1.9.20150206/ \
$(LOCAL_PATH)/$(SOURCE_PATH)/pjproject-2.3/pjlib/include \
$(LOCAL_PATH)/$(SOURCE_PATH)/pjproject-2.3/pjlib-util/include \
$(LOCAL_PATH)/$(SOURCE_PATH)/pjproject-2.3/pjlib-util/include \
$(LOCAL_PATH)/$(SOURCE_PATH)/pjproject-2.3/pjnath/include \
$(LOCAL_PATH)/$(SOURCE_PATH)/udt4.11/src \
$(LOCAL_PATH)/$(SOURCE_PATH)/sdk/include \
$(LOCAL_PATH)/$(SOURCE_PATH) \
$(LOCAL_PATH)/$(SOURCE_PATH)/.. \

LOCAL_SRC_FILES :=\
$(SOURCE_PATH)/miniupnpc-1.9.20150206/connecthostport.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/igd_desc_parse.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/minisoap.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/miniupnpc.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/miniwget.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/minixml.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/portlistingparse.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/receivedata.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/upnpcommands.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/upnperrors.c \
$(SOURCE_PATH)/miniupnpc-1.9.20150206/upnpreplyparse.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/activesock.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/addr_resolv_sock.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/array.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/config.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ctype.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/errno.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/except.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/fifobuf.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/file_access_unistd.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/file_io_ansi.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/guid.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/guid_simple.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/hash.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ioqueue_select.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ip_helper_generic.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/list.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/lock.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/log.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/log_writer_stdout.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_core_unix.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_error_unix.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_info.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_time_unix.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_time_common.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_timestamp_common.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/os_timestamp_posix.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/pool.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/pool_buf.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/pool_caching.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/pool_dbg.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/pool_policy_malloc.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/rand.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/rbtree.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_bsd.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_common.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_qos_bsd.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_qos_common.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_qos_dummy.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/sock_select.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ssl_sock_common.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ssl_sock_dump.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/ssl_sock_ossl.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/string.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/timer.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib/src/pj/types.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/base64.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/crc32.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/dns.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/dns_dump.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/dns_server.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/errno.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/getopt.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/hmac_md5.c\
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/hmac_sha1.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/http_client.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/md5.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/pcap.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/resolver.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/scanner.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/sha1.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/srv_resolver.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/string.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/stun_simple.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/stun_simple_client.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/symbols.c \
$(SOURCE_PATH)/pjproject-2.3/pjlib-util/src/pjlib-util/xml.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/errno.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/ice_session.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/ice_strans.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/nat_detect.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_global.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_tcp_connect_proxy.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_tcp_listen_proxy.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_transport.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_udt.cpp \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_upnp.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_dispatch.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/socket_pair.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_auth.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_msg.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_msg_dump.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_session.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_sock.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/stun_transaction.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/turn_session.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/turn_sock.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/p2p_tcp.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_client_signaling_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_client_av_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_client_pull_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_dev_main_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_dev_push_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_dev_av_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/gss_conn.c \
$(SOURCE_PATH)/pjproject-2.3/pjnath/src/pjnath/rbtree.c \
$(SOURCE_PATH)/udt4.11/src/api.cpp \
$(SOURCE_PATH)/udt4.11/src/buffer.cpp \
$(SOURCE_PATH)/udt4.11/src/cache.cpp \
$(SOURCE_PATH)/udt4.11/src/ccc.cpp \
$(SOURCE_PATH)/udt4.11/src/channel.cpp \
$(SOURCE_PATH)/udt4.11/src/common.cpp \
$(SOURCE_PATH)/udt4.11/src/core.cpp \
$(SOURCE_PATH)/udt4.11/src/epoll.cpp \
$(SOURCE_PATH)/udt4.11/src/list.cpp \
$(SOURCE_PATH)/udt4.11/src/md5.cpp \
$(SOURCE_PATH)/udt4.11/src/packet.cpp \
$(SOURCE_PATH)/udt4.11/src/queue.cpp \
$(SOURCE_PATH)/udt4.11/src/StreamData.cpp \
$(SOURCE_PATH)/udt4.11/src/window.cpp \
P2PClient.c \
JniCallback.c \


LOCAL_CPPFLAGS += -fexceptions -DP2P_CROSS_PLATFORM -DPJ_M_I386 -DPJ_SOCK_HAS_INET_NTOP=1 -DPJ_HAS_STDLIB_H -DANDROID_BUILD
LOCAL_CFLAGS += -DP2P_CROSS_PLATFORM -DPJ_M_I386 -DPJ_SOCK_HAS_INET_NTOP=1 -DPJ_HAS_STDLIB_H -DANDROID_BUILD

LOCAL_LDLIBS := -llog
LOCAL_LDLIBS := $(LOCAL_LDLIBS) 

include $(BUILD_SHARED_LIBRARY)


