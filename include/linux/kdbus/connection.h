/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __KDBUS_CONNECTION_H
#define __KDBUS_CONNECTION_H

#include "defaults.h"
#include "util.h"
#include "metadata.h"
#include "pool.h"

/**
 * struct kdbus_conn - connection to a bus
 * @kref:		Reference count
 * @disconnected:	Invalidated data
 * @id:			Connection ID
 * @name:		Human-readable connection name, used for debugging
 * @bus:		The bus this connection belongs to
 * @ep:			The endpoint this connection belongs to
 * @flags:		KDBUS_HELLO_* flags
 * @attach_flags:	KDBUS_ATTACH_* flags
 * @lock:		Connection data lock
 * @msg_list:		Queue of messages
 * @msg_prio_queue:	Tree of messages, sorted by priority
 * @msg_prio_highest:	Cached entry for highest priority (lowest value) node
 * @hentry:		Entry in ID <-> connection map
 * @monitor_entry:	The connection is a monitor
 * @names_list:		List of well-known names
 * @names_queue_list:	Well-known names this connection waits for
 * @reply_list:		List of connections this connection expects
 *			a reply from.
 * @reply_count:	Number of requests this connection has issued, and
 *			waits for replies from the peer
 * @names:		Number of owned well-known names
 * @work:		Delayed work to handle timeouts
 * @match_db:		Subscription filter to broadcast messages
 * @meta:		Active connection creator's metadata/credentials,
 *			either from the handle of from HELLO
 * @owner_meta:		The connection's metadata/credentials supplied by
 *			HELLO
 * @msg_count:		Number of queued messages
 * @pool:		The user's buffer to receive messages
 * @user:		Owner of the connection;
 */
struct kdbus_conn {
	struct kref kref;
	bool disconnected;
	u64 id;
	const char *name;
	struct kdbus_bus *bus;
	struct kdbus_ep *ep;
	u64 flags;
	u64 attach_flags;
	struct mutex lock;
	struct list_head msg_list;
	struct rb_root msg_prio_queue;
	struct rb_node *msg_prio_highest;
	struct hlist_node hentry;
	struct list_head monitor_entry;
	struct list_head names_list;
	struct list_head names_queue_list;
	struct list_head reply_list;
	atomic_t reply_count;
	size_t names;
	struct delayed_work work;
	struct kdbus_match_db *match_db;
	struct kdbus_meta *meta;
	struct kdbus_meta *owner_meta;
	unsigned int msg_count;
	struct kdbus_pool *pool;
	struct kdbus_domain_user *user;
};

struct kdbus_kmsg;
struct kdbus_conn_queue;
struct kdbus_name_registry;

int kdbus_conn_new(struct kdbus_ep *ep,
		   struct kdbus_cmd_hello *hello,
		   struct kdbus_meta *meta,
		   struct kdbus_conn **conn);
struct kdbus_conn *kdbus_conn_ref(struct kdbus_conn *conn);
struct kdbus_conn *kdbus_conn_unref(struct kdbus_conn *conn);
int kdbus_conn_disconnect(struct kdbus_conn *conn, bool ensure_queue_empty);
bool kdbus_conn_active(struct kdbus_conn *conn);

int kdbus_cmd_msg_recv(struct kdbus_conn *conn,
		       struct kdbus_cmd_recv *recv);
int kdbus_cmd_msg_cancel(struct kdbus_conn *conn,
			 u64 cookie);
int kdbus_cmd_conn_info(struct kdbus_conn *conn,
			struct kdbus_cmd_conn_info *cmd_info,
			size_t size);
int kdbus_cmd_conn_update(struct kdbus_conn *conn,
			  struct kdbus_cmd_conn_update *cmd_update);
int kdbus_conn_kmsg_send(struct kdbus_ep *ep,
			 struct kdbus_conn *conn_src,
			 struct kdbus_kmsg *kmsg);
void kdbus_conn_kmsg_list_free(struct list_head *kmsg_list);
int kdbus_conn_kmsg_list_send(struct kdbus_ep *ep,
			      struct list_head *kmsg_list);
int kdbus_conn_move_messages(struct kdbus_conn *conn_dst,
			     struct kdbus_conn *conn_src,
			     u64 name_id);
bool kdbus_conn_has_name(struct kdbus_conn *conn, const char *name);
#endif
