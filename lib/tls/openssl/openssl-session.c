/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 - 2021 Andy Green <andy@warmcat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "private-lib-core.h"

typedef struct lws_tls_session_cache_openssl {
	lws_dll2_t			list;

	SSL_SESSION			*session;
	lws_sorted_usec_list_t		sul_ttl;

	uint8_t				in_use:1;
	uint8_t				expire_on_unused:1;
					/* ttl over but in use */

	/* name is overallocated here */
} lws_tls_sco_t;

static int
lws_tls_session_name_from_wsi(struct lws *wsi, char *buf, size_t len)
{
	lws_sa46_write_numeric_address(&wsi->sa46_peer, buf, len - 8);
	lws_snprintf(buf + strlen(buf), 8, ":%u", wsi->c_port);

	return 0;
}

static void
__lws_tls_session_destroy(lws_tls_sco_t *ts)
{
	struct lws_vhost *vh = lws_container_of(ts->list.owner, struct lws_vhost,
						tls_sessions);

	lwsl_notice("%s: %s.%s\n", __func__, vh->name, (const char *)&ts[1]);

	assert(!ts->in_use);

	lws_sul_cancel(&ts->sul_ttl);		/* pt lock */
	SSL_SESSION_free(ts->session);
	lws_dll2_remove(&ts->list);		/* vh lock */

	lws_free(ts);
}

static void
lws_tls_session_ttl_exp(lws_sorted_usec_list_t *sul)
{
	lws_tls_sco_t *ts = lws_container_of(sul, lws_tls_sco_t, sul_ttl);
	struct lws_vhost *vh = lws_container_of(ts->list.owner, struct lws_vhost,
						tls_sessions);

	/* if it's in use, just mark as expired */
	if (ts->in_use) {
		ts->expire_on_unused = 1;
		return;
	}

	lws_vhost_lock(vh); /* -------------- vh { */
	__lws_tls_session_destroy(ts);
	lws_vhost_unlock(vh); /* } vh --------------  */
}

static lws_tls_sco_t *
__lws_tls_session_lookup_by_name(struct lws_vhost *vh, const char *name)
{
	if (!(vh->options & LWS_SERVER_OPTION_ENABLE_TLS_SESSION_CACHE))
		return NULL;

	lws_start_foreach_dll(struct lws_dll2 *, p,
			      lws_dll2_get_head(&vh->tls_sessions)) {
		lws_tls_sco_t *ts = lws_container_of(p, lws_tls_sco_t, list);
		const char *ts_name = (const char *)&ts[1];

		if (!strcmp(name, ts_name))
			return ts;

	} lws_end_foreach_dll(p);

	return NULL;
}

/*
 * If possible, reuse the session
 */

void
lws_tls_reuse_session(struct lws *wsi)
{
	char buf[INET6_ADDRSTRLEN + 1 + 8 + 1];
	lws_tls_sco_t *ts;

	lws_tls_session_name_from_wsi(wsi, buf, sizeof(buf));
	ts = __lws_tls_session_lookup_by_name(wsi->a.vhost, buf);

	if (!ts)
		return;

	if (ts->in_use) {
		assert(0);
		return;
	}

	lwsl_notice("%s: %s.%s\n", __func__,
		    wsi->a.vhost->name, (const char *)&ts[1]);

	SSL_set_session(wsi->tls.ssl, ts->session);
}

void
lws_tls_session_release(struct lws *wsi)
{
	SSL_SESSION *sess;

	if (!wsi->a.vhost ||
	    !(wsi->a.vhost->options & LWS_SERVER_OPTION_ENABLE_TLS_SESSION_CACHE))
		return;

	sess = SSL_get_session(wsi->tls.ssl);

	lws_start_foreach_dll(struct lws_dll2 *, p,
			      lws_dll2_get_head(&wsi->a.vhost->tls_sessions)) {
		lws_tls_sco_t *ts = lws_container_of(p, lws_tls_sco_t, list);

		if (ts->session == sess) {

			assert(ts->in_use);

			ts->in_use = 0;
			if (ts->expire_on_unused) {
				lwsl_notice("%s: expiring %s\n", __func__,
							(const char *)&ts[1]);
				__lws_tls_session_destroy(ts);
			} else
				lwsl_notice("%s: %s\n", __func__,
							(const char *)&ts[1]);

			return;
		}
	} lws_end_foreach_dll(p);
}


static int
__lws_tls_session_create(struct lws_vhost *vh, int tsi, SSL_SESSION *session,
		       const char *name, unsigned int ttl)
{
	size_t nl = strlen(name);
	lws_tls_sco_t *ts;

	if (!(vh->options & LWS_SERVER_OPTION_ENABLE_TLS_SESSION_CACHE))
		return 0;

	ts = __lws_tls_session_lookup_by_name(vh, name);
	if (!ts) {
		ts = lws_malloc(sizeof(*ts) + nl + 1, __func__);

		if (!ts)
			return 1;

		memset(ts, 0, sizeof(*ts));
		memcpy(&ts[1], name, nl + 1);

		lws_vhost_lock(vh); /* -------------- vh { */
		lws_dll2_add_tail(&ts->list, &vh->tls_sessions);
		lws_vhost_unlock(vh); /* } vh --------------  */

		lwsl_notice("%s: new %s ttl %us\n", __func__,  name, ttl);
	} else {
		SSL_SESSION_free(ts->session);
		lwsl_notice("%s: update %s ttl %us\n", __func__,  name, ttl);
	}

	ts->session = session;
	ts->expire_on_unused = 0;
	ts->in_use = 1;

	lws_sul_schedule(vh->context, tsi, &ts->sul_ttl,
			 lws_tls_session_ttl_exp, ttl * LWS_US_PER_SEC);

	return 0;
}

static int
lws_tls_session_destroy_dll(struct lws_dll2 *d, void *user)
{
	lws_tls_sco_t *ts = lws_container_of(d, lws_tls_sco_t, list);

	/* the ones that are in use will be destroyed with their wsi */

	if (!ts->in_use)
		__lws_tls_session_destroy(ts);

	return 0;
}

void
lws_tls_session_vh_destroy(struct lws_vhost *vh)
{
	lws_dll2_foreach_safe(&vh->tls_sessions, NULL,
			      lws_tls_session_destroy_dll);
}

static int
lws_tls_session_new_cb(SSL *ssl, SSL_SESSION *sess)
{
	struct lws *wsi = (struct lws *)SSL_get_ex_data(ssl,
					openssl_websocket_private_data_index);
	char buf[INET6_ADDRSTRLEN + 1 + 8 + 1];

	if (!wsi) {
		lwsl_err("%s: can't get wsi from ssl privdata\n", __func__);

		return 0;
	}

	lws_tls_session_name_from_wsi(wsi, buf, sizeof(buf));

	if (__lws_tls_session_create(wsi->a.vhost, wsi->tsi, sess, buf, 300)) {
		lwsl_warn("%s: unable to create session\n", __func__);

		return 0;
	}

	/*
	 * indicate we will hold on to the SSL_SESSION reference, and take
	 * responsibility to call SSL_SESSION_free() on it ourselves
	 */

	return 1;
}

void
lws_tls_session_cache(struct lws_vhost *vh)
{
	long cmode;

	if (!(vh->options & LWS_SERVER_OPTION_ENABLE_TLS_SESSION_CACHE))
		return;

	cmode = SSL_CTX_get_session_cache_mode(vh->tls.ssl_client_ctx);

	SSL_CTX_set_session_cache_mode(vh->tls.ssl_client_ctx, (int)(cmode |
							SSL_SESS_CACHE_CLIENT));

	SSL_CTX_sess_set_new_cb(vh->tls.ssl_client_ctx, lws_tls_session_new_cb);
}
