/*
 * Copyright (C) 2013-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 */

/*
 * Basic API test
 * XXX
 */

#undef NDEBUG
#include <assert.h>
#include <c-macro.h>
#include <c-syscall.h>
#include <linux/bus1.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "org.bus1/b1-peer.h"

static void test_peer(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer1 = NULL, *peer2 = NULL, *peer3 = NULL;
        int fd, r;

        /* create three peers: peer1 and peer2 are two instances of the same */
        r = b1_peer_new(&peer1);
        assert(r >= 0);
        assert(peer1);

        fd = b1_peer_get_fd(peer1);
        assert(fd >= 0);

        r = b1_peer_new_from_fd(&peer2, fd);
        assert(r >= 0);
        assert(peer2);

        r = b1_peer_new(&peer3);
        assert(r >= 0);
        assert(peer3);
}

static void test_node(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        B1Handle *handle;
        int r;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node);
        assert(r >= 0);
        assert(node);

        assert(b1_node_get_peer(node) == peer);

        handle = b1_node_get_handle(node);
        assert(handle);

        handle = b1_handle_ref(handle);
        assert(handle);
        b1_handle_unref(handle);
}

static void test_handle(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        int r;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node), peer, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle == b1_node_get_handle(node));
}

static void test_message(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *peer = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        B1Handle *handle;
        int r, fd;

        r = b1_peer_new(&peer);
        assert(r >= 0);

        r = b1_node_new(peer, &node);
        assert(r >= 0);

        r = b1_message_new(peer, &message);
        assert(r >= 0);
        assert(message);

        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(!b1_message_get_destination_node(message));
        assert(!b1_message_get_destination_handle(message));
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);

        handle = b1_node_get_handle(node);

        r = b1_message_set_handles(message, &handle, 1);
        assert(r >= 0);

        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle == b1_node_get_handle(node));

        fd = b1_peer_get_fd(peer);

        r = b1_message_set_fds(message, &fd, 1);
        assert(r >= 0);

        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
        assert(fd != b1_peer_get_fd(peer));
}

static void test_transaction(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *src = NULL, *dst = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node = NULL;
        _c_cleanup_(b1_handle_unrefp) B1Handle *handle = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const char *payload = "WOOF";
        struct iovec vec = {
                .iov_base = (void*)payload,
                .iov_len = strlen(payload) + 1,
        };
        struct iovec *vec_out;
        size_t n_vec;
        int r, fd;

        r = b1_peer_new(&src);
        assert(r >= 0);

        r = b1_peer_new(&dst);
        assert(r >= 0);

        r = b1_node_new(dst, &node);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node), src, &handle);
        assert(r >= 0);

        r = b1_message_new(src, &message);
        assert(r >= 0);

        r = b1_message_set_payload(message, &vec, 1);
        assert(r >= 0);

        r = b1_message_set_handles(message, &handle, 1);
        assert(r >= 0);

        fd = eventfd(0, 0);
        assert(fd >= 0);

        r = b1_message_set_fds(message, &fd, 1);
        assert(r >= 0);

        assert(close(fd) >= 0);

        r = b1_message_send(message, &handle, 1);
        assert(r >= 0);

        message = b1_message_unref(message);
        assert(!message);
        handle = b1_handle_unref(handle);
        assert(!handle);

        r = b1_peer_recv(dst, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(b1_message_get_destination_node(message) == node);
        assert(b1_message_get_uid(message) == getuid());
        assert(b1_message_get_gid(message) == getgid());
        assert(b1_message_get_pid(message) == getpid());
        assert(b1_message_get_tid(message) == c_syscall_gettid());
        r = b1_message_get_payload(message, &vec_out, &n_vec);
        assert(r >= 0);
        assert(vec_out);
        assert(n_vec == 1);
        assert(vec_out->iov_len == strlen("WOOF") + 1);
        assert(strcmp(vec_out->iov_base, "WOOF") == 0);
        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle == b1_node_get_handle(node));
        handle = NULL;
        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_RELEASE);
        assert(b1_message_get_destination_node(message) == node);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_node_destroy(node);
        assert(r >= 0);

        r = b1_peer_recv(dst, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_DESTROY);
        assert(b1_message_get_destination_node(message));
        assert(b1_message_get_destination_node(message) == node);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst, &message);
        assert(r == -EAGAIN);
        r = b1_peer_recv(src, &message);
        assert(r == -EAGAIN);
}

static void test_multicast(void) {
        _c_cleanup_(b1_peer_unrefp) B1Peer *src = NULL, *dst1 = NULL, *dst2 = NULL;
        _c_cleanup_(b1_node_freep) B1Node *node1 = NULL, *node2 = NULL;
        _c_cleanup_(b1_message_unrefp) B1Message *message = NULL;
        const char *payload = "WOOF";
        struct iovec vec = {
                .iov_base = (void*)payload,
                .iov_len = strlen(payload) + 1,
        };
        struct iovec *vec_out;
        size_t n_vec;
        B1Handle *handles[2], *handle;
        int r, fd;

        r = b1_peer_new(&src);
        assert(r >= 0);

        r = b1_peer_new(&dst1);
        assert(r >= 0);

        r = b1_peer_new(&dst2);
        assert(r >= 0);

        r = b1_node_new(dst1, &node1);
        assert(r >= 0);

        r = b1_node_new(dst2, &node2);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node1), src, &handles[0]);
        assert(r >= 0);

        r = b1_handle_transfer(b1_node_get_handle(node2), src, &handles[1]);
        assert(r >= 0);

        r = b1_message_new(src, &message);
        assert(r >= 0);

        r = b1_message_set_payload(message, &vec, 1);
        assert(r >= 0);

        r = b1_message_set_handles(message, handles, 2);
        assert(r >= 0);

        fd = eventfd(0, 0);
        assert(fd >= 0);

        r = b1_message_set_fds(message, &fd, 1);
        assert(r >= 0);

        assert(close(fd) >= 0);

        r = b1_message_send(message, handles, 2);
        assert(r >= 0);

        message = b1_message_unref(message);
        assert(!message);
        handles[0] = b1_handle_unref(handles[0]);
        assert(!handles[0]);
        handles[1] = b1_handle_unref(handles[1]);
        assert(!handles[1]);

        r = b1_peer_recv(dst1, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(b1_message_get_destination_node(message) == node1);
        assert(b1_message_get_uid(message) == getuid());
        assert(b1_message_get_gid(message) == getgid());
        assert(b1_message_get_pid(message) == getpid());
        assert(b1_message_get_tid(message) == c_syscall_gettid());
        r = b1_message_get_payload(message, &vec_out, &n_vec);
        assert(r >= 0);
        assert(vec_out);
        assert(n_vec == 1);
        assert(vec_out->iov_len == strlen("WOOF") + 1);
        assert(strcmp(vec_out->iov_base, "WOOF") == 0);
        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle == b1_node_get_handle(node1));
        r = b1_message_get_handle(message, 1, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle != b1_node_get_handle(node1));
        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst2, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_DATA);
        assert(b1_message_get_destination_node(message) == node2);
        assert(b1_message_get_uid(message) == getuid());
        assert(b1_message_get_gid(message) == getgid());
        assert(b1_message_get_pid(message) == getpid());
        assert(b1_message_get_tid(message) == c_syscall_gettid());
        r = b1_message_get_payload(message, &vec_out, &n_vec);
        assert(r >= 0);
        assert(vec_out);
        assert(n_vec == 1);
        assert(vec_out->iov_len == strlen("WOOF") + 1);
        assert(strcmp(vec_out->iov_base, "WOOF") == 0);
        r = b1_message_get_handle(message, 0, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle != b1_node_get_handle(node2));
        r = b1_message_get_handle(message, 1, &handle);
        assert(r >= 0);
        assert(handle);
        assert(handle == b1_node_get_handle(node2));
        r = b1_message_get_fd(message, 0, &fd);
        assert(r >= 0);
        assert(fd >= 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst1, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_RELEASE);
        assert(b1_message_get_destination_node(message) == node1);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst2, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_RELEASE);
        assert(b1_message_get_destination_node(message) == node2);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_node_destroy(node1);
        assert(r >= 0);

        r = b1_peer_recv(dst1, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_DESTROY);
        assert(b1_message_get_destination_node(message) == node1);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_node_destroy(node2);
        assert(r >= 0);

        r = b1_peer_recv(dst2, &message);
        assert(r >= 0);
        assert(message);
        assert(b1_message_get_type(message) == BUS1_MSG_NODE_DESTROY);
        assert(b1_message_get_destination_node(message) == node2);
        assert(b1_message_get_uid(message) == (uid_t)-1);
        assert(b1_message_get_gid(message) == (gid_t)-1);
        assert(b1_message_get_pid(message) == 0);
        assert(b1_message_get_tid(message) == 0);
        message = b1_message_unref(message);

        r = b1_peer_recv(dst1, &message);
        assert(r == -EAGAIN);
        r = b1_peer_recv(dst2, &message);
        assert(r == -EAGAIN);
        r = b1_peer_recv(src, &message);
        assert(r == -EAGAIN);
}

int main(int argc, char **argv) {
        if (access("/dev/bus1", F_OK) < 0 && errno == ENOENT)
                return 77;

        test_peer();
        test_node();
        test_handle();
        test_message();
        test_transaction();
        test_multicast();

        return 0;
}
