/*++
/* NAME
/*	tlsproxy_state 3
/* SUMMARY
/*	Postfix SMTP server
/* SYNOPSIS
/*	#include <tlsproxy.h>
/*
/*	TLSP_STATE *tlsp_state_create(service, plaintext_stream)
/*	const char *service;
/*	VSTREAM	*plaintext_stream;
/*
/*	void	tlsp_state_free(state)
/*	TLSP_STATE *state;
/* DESCRIPTION
/*	This module provides TLSP_STATE constructor and destructor
/*	routines.
/*
/*	tlsp_state_create() initializes session context.
/*
/*	tlsp_state_free() destroys session context. If the handshake
/*	was in progress, it logs a 'handshake failed' message.
/*
/*	Arguments:
/* .IP service
/*	The service name for the TLS library. This argument is copied.
/*	The destructor will automatically destroy the string.
/* .IP plaintext_stream
/*	The VSTREAM between postscreen(8) and tlsproxy(8).
/*	The destructor will automatically close the stream.
/* .PP
/*	Other structure members are set by the application. The
/*	text below describes how the TLSP_STATE destructor
/*	disposes of them.
/* .IP plaintext_buf
/*	NBBIO for plaintext I/O.
/*	The destructor will automatically turn off read/write/timeout
/*	events and destroy the NBBIO.
/* .IP ciphertext_fd
/*	The file handle for the remote SMTP client socket.
/*	The destructor will automatically turn off read/write events
/*	and close the file handle.
/* .IP ciphertext_timer
/*	The destructor will automatically turn off this time event.
/* .IP timeout
/*	Time limit for plaintext and ciphertext I/O.
/* .IP remote_endpt
/*	Printable remote endpoint name.
/*	The destructor will automatically destroy the string.
/* .IP server_id
/*	TLS session cache identifier.
/*	The destructor will automatically destroy the string.
/* DIAGNOSTICS
/*	All errors are fatal.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*
/*	Wietse Venema
/*	Google, Inc.
/*	111 8th Avenue
/*	New York, NY 10011, USA
/*--*/

 /*
  * System library.
  */
#include <sys_defs.h>

 /*
  * Utility library.
  */
#include <msg.h>
#include <mymalloc.h>
#include <nbbio.h>

 /*
  * Master library.
  */
#include <mail_server.h>

 /*
  * TLS library.
  */
#ifdef USE_TLS
#define TLS_INTERNAL			/* XXX */
#include <tls.h>
#include <tls_proxy.h>

 /*
  * Application-specific.
  */
#include <tlsproxy.h>

/* tlsp_state_create - create TLS proxy state object */

TLSP_STATE *tlsp_state_create(const char *service,
			              VSTREAM *plaintext_stream)
{
    TLSP_STATE *state = (TLSP_STATE *) mymalloc(sizeof(*state));

    state->flags = 0;
    state->service = mystrdup(service);
    state->plaintext_stream = plaintext_stream;
    state->plaintext_buf = 0;
    state->ciphertext_fd = -1;
    state->ciphertext_timer = 0;
    state->timeout = -1;
    state->remote_endpt = 0;
    state->server_id = 0;
    state->tls_context = 0;
    state->tls_params = 0;
    state->server_init_props = 0;
    state->server_start_props = 0;
    state->client_init_props = 0;
    state->client_start_props = 0;

    return (state);
}

/* tlsp_state_free - destroy state objects, connection and events */

void    tlsp_state_free(TLSP_STATE *state)
{
    /* Don't log failure after plaintext EOF. */
    if (state->remote_endpt && state->server_id
	&& (state->flags & TLSP_FLAG_DO_HANDSHAKE))
	msg_info("TLS handshake failed for service=%s peer=%s",
		 state->server_id, state->remote_endpt);
    myfree(state->service);
    if (state->plaintext_buf)			/* turns off plaintext events */
	nbbio_free(state->plaintext_buf);
    else
	event_disable_readwrite(vstream_fileno(state->plaintext_stream));
    event_server_disconnect(state->plaintext_stream);
    if (state->ciphertext_fd >= 0) {
	event_disable_readwrite(state->ciphertext_fd);
	(void) close(state->ciphertext_fd);
    }
    if (state->ciphertext_timer)
	event_cancel_timer(state->ciphertext_timer, (void *) state);
    if (state->remote_endpt) {
	msg_info("DISCONNECT %s", state->remote_endpt);
	myfree(state->remote_endpt);
    }
    if (state->server_id)
	myfree(state->server_id);
    if (state->tls_context)
	tls_free_context(state->tls_context);
    if (state->tls_params)
	tls_proxy_client_param_free(state->tls_params);
    if (state->server_init_props)
	tls_proxy_server_init_free(state->server_init_props);
    if (state->server_start_props)
	tls_proxy_server_start_free(state->server_start_props);
    if (state->client_init_props)
	tls_proxy_client_init_free(state->client_init_props);
    if (state->client_start_props)
	tls_proxy_client_start_free(state->client_start_props);
    myfree((void *) state);
}

#endif
