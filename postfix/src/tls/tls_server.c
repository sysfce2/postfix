/*++
/* NAME
/*	tls_server 3
/* SUMMARY
/*	server-side TLS engine
/* SYNOPSIS
/*	#include <tls.h>
/*
/*	TLS_APPL_STATE *tls_server_init(props)
/*	const TLS_SERVER_INIT_PROPS *props;
/*
/*	TLS_SESS_STATE *tls_server_start(props)
/*	const TLS_SERVER_START_PROPS *props;
/*
/*	TLS_SESS_STATE *tls_server_post_accept(TLScontext)
/*	TLS_SESS_STATE *TLScontext;
/*
/*	void	tls_server_stop(app_ctx, stream, failure, TLScontext)
/*	TLS_APPL_STATE *app_ctx;
/*	VSTREAM	*stream;
/*	int	failure;
/*	TLS_SESS_STATE *TLScontext;
/* DESCRIPTION
/*	This module is the interface between Postfix TLS servers,
/*	the OpenSSL library, and the TLS entropy and cache manager.
/*
/*	See "EVENT_DRIVEN APPLICATIONS" below for using this code
/*	in event-driven programs.
/*
/*	tls_server_init() is called once when the SMTP server
/*	initializes.
/*	Certificate details are also decided during this phase,
/*	so that peer-specific behavior is not possible.
/*
/*	tls_server_start() activates the TLS feature for the VSTREAM
/*	passed as argument. We assume that network buffers are flushed
/*	and the TLS handshake can begin	immediately.
/*
/*	tls_server_stop() sends the "close notify" alert via
/*	SSL_shutdown() to the peer and resets all connection specific
/*	TLS data. As RFC2487 does not specify a separate shutdown, it
/*	is assumed that the underlying TCP connection is shut down
/*	immediately afterwards. Any further writes to the channel will
/*	be discarded, and any further reads will report end-of-file.
/*	If the failure flag is set, no SSL_shutdown() handshake is performed.
/*
/*	Once the TLS connection is initiated, information about the TLS
/*	state is available via the TLScontext structure:
/* .IP TLScontext->protocol
/*	the protocol name (SSLv2, SSLv3, TLSv1),
/* .IP TLScontext->cipher_name
/*	the cipher name (e.g. RC4/MD5),
/* .IP TLScontext->cipher_usebits
/*	the number of bits actually used (e.g. 40),
/* .IP TLScontext->cipher_algbits
/*	the number of bits the algorithm is based on (e.g. 128).
/* .PP
/*	The last two values may differ from each other when export-strength
/*	encryption is used.
/*
/*	If the peer offered a certificate, part of the certificate data are
/*	available as:
/* .IP TLScontext->peer_status
/*	A bitmask field that records the status of the peer certificate
/*	verification. One or more of TLS_CRED_FLAG_CERT, TLS_CRED_FLAG_RPK
/*	and TLS_CERT_FLAG_TRUSTED.
/* .IP TLScontext->peer_CN
/*	Extracted CommonName of the peer, or zero-length string
/*	when information could not be extracted.
/* .IP TLScontext->issuer_CN
/*	Extracted CommonName of the issuer, or zero-length string
/*	when information could not be extracted.
/* .IP TLScontext->peer_cert_fprint
/*	Fingerprint of the certificate, or zero-length string when no peer
/*	certificate is available.
/* .PP
/*	If no peer certificate is presented the peer_status is set to 0.
/* EVENT_DRIVEN APPLICATIONS
/* .ad
/* .fi
/*	Event-driven programs manage multiple I/O channels.  Such
/*	programs cannot use the synchronous VSTREAM-over-TLS
/*	implementation that the current TLS library provides,
/*	including tls_server_stop() and the underlying tls_stream(3)
/*	and tls_bio_ops(3) routines.
/*
/*	With the current TLS library implementation, this means
/*	that the application is responsible for calling and retrying
/*	SSL_accept(), SSL_read(), SSL_write() and SSL_shutdown().
/*
/*	To maintain control over TLS I/O, an event-driven server
/*	invokes tls_server_start() with a null VSTREAM argument and
/*	with an fd argument that specifies the I/O file descriptor.
/*	Then, tls_server_start() performs all the necessary
/*	preparations before the TLS handshake and returns a partially
/*	populated TLS context. The event-driven application is then
/*	responsible for invoking SSL_accept(), and if successful,
/*	for invoking tls_server_post_accept() to finish the work
/*	that was started by tls_server_start(). In case of unrecoverable
/*	failure, tls_server_post_accept() destroys the TLS context
/*	and returns a null pointer value.
/* LICENSE
/* .ad
/* .fi
/*	This software is free. You can do with it whatever you want.
/*	The original author kindly requests that you acknowledge
/*	the use of his software.
/* AUTHOR(S)
/*	Originally written by:
/*	Lutz Jaenicke
/*	BTU Cottbus
/*	Allgemeine Elektrotechnik
/*	Universitaetsplatz 3-4
/*	D-03044 Cottbus, Germany
/*
/*	Updated by:
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*
/*	Victor Duchovni
/*	Morgan Stanley
/*--*/

/* System library. */

#include <sys_defs.h>

#ifdef USE_TLS
#include <unistd.h>
#include <string.h>

/* Utility library. */

#include <mymalloc.h>
#include <vstring.h>
#include <vstream.h>
#include <dict.h>
#include <stringops.h>
#include <msg.h>
#include <hex_code.h>
#include <iostuff.h>			/* non-blocking */

/* Global library. */

#include <mail_params.h>

/* TLS library. */

#include <tls_mgr.h>
#define TLS_INTERNAL
#include <tls.h>
#if OPENSSL_VERSION_PREREQ(3,0)
#include <openssl/core_names.h>		/* EVP_MAC parameters */
#endif

#define STR(x)	vstring_str(x)
#define LEN(x)	VSTRING_LEN(x)

/* Application-specific. */

 /*
  * The session_id_context identifies the service that created a session.
  * This information is used to distinguish between multiple TLS-based
  * servers running on the same server. We use the name of the mail system.
  */
static const char server_session_id_context[] = "Postfix/TLS";

#ifndef OPENSSL_NO_TLSEXT

 /*
  * We retain the cipher handle for the lifetime of the process.
  */
static const EVP_CIPHER *tkt_cipher;

#endif

#define GET_SID(s, v, lptr)	((v) = SSL_SESSION_get_id((s), (lptr)))

typedef const unsigned char *session_id_t;

/* get_server_session_cb - callback to retrieve session from server cache */

static SSL_SESSION *get_server_session_cb(SSL *ssl, session_id_t session_id,
					          int session_id_length,
					          int *unused_copy)
{
    const char *myname = "get_server_session_cb";
    TLS_SESS_STATE *TLScontext;
    VSTRING *cache_id;
    VSTRING *session_data = vstring_alloc(2048);
    SSL_SESSION *session = 0;

    if ((TLScontext = SSL_get_ex_data(ssl, TLScontext_index)) == 0)
	msg_panic("%s: null TLScontext in session lookup callback", myname);

#define GEN_CACHE_ID(buf, id, len, service) \
    do { \
	buf = vstring_alloc(2 * (len + strlen(service))); \
	hex_encode(buf, (char *) (id), (len)); \
	vstring_sprintf_append(buf, "&s=%s", (service)); \
	vstring_sprintf_append(buf, "&l=%ld", (long) OpenSSL_version_num()); \
    } while (0)


    GEN_CACHE_ID(cache_id, session_id, session_id_length, TLScontext->serverid);

    if (TLScontext->log_mask & TLS_LOG_CACHE)
	msg_info("%s: looking up session %s in %s cache", TLScontext->namaddr,
		 STR(cache_id), TLScontext->cache_type);

    /*
     * Load the session from cache and decode it.
     */
    if (tls_mgr_lookup(TLScontext->cache_type, STR(cache_id),
		       session_data) == TLS_MGR_STAT_OK) {
	session = tls_session_activate(STR(session_data), LEN(session_data));
	if (session && (TLScontext->log_mask & TLS_LOG_CACHE))
	    msg_info("%s: reloaded session %s from %s cache",
		     TLScontext->namaddr, STR(cache_id),
		     TLScontext->cache_type);
    }

    /*
     * Clean up.
     */
    vstring_free(cache_id);
    vstring_free(session_data);

    return (session);
}

/* uncache_session - remove session from internal & external cache */

static void uncache_session(SSL_CTX *ctx, TLS_SESS_STATE *TLScontext)
{
    VSTRING *cache_id;
    SSL_SESSION *session = SSL_get_session(TLScontext->con);
    const unsigned char *sid;
    unsigned int sid_length;

    SSL_CTX_remove_session(ctx, session);

    if (TLScontext->cache_type == 0)
	return;

    GET_SID(session, sid, &sid_length);
    GEN_CACHE_ID(cache_id, sid, sid_length, TLScontext->serverid);

    if (TLScontext->log_mask & TLS_LOG_CACHE)
	msg_info("%s: remove session %s from %s cache", TLScontext->namaddr,
		 STR(cache_id), TLScontext->cache_type);

    tls_mgr_delete(TLScontext->cache_type, STR(cache_id));
    vstring_free(cache_id);
}

/* new_server_session_cb - callback to save session to server cache */

static int new_server_session_cb(SSL *ssl, SSL_SESSION *session)
{
    const char *myname = "new_server_session_cb";
    VSTRING *cache_id;
    TLS_SESS_STATE *TLScontext;
    VSTRING *session_data;
    const unsigned char *sid;
    unsigned int sid_length;

    if ((TLScontext = SSL_get_ex_data(ssl, TLScontext_index)) == 0)
	msg_panic("%s: null TLScontext in new session callback", myname);

    GET_SID(session, sid, &sid_length);
    GEN_CACHE_ID(cache_id, sid, sid_length, TLScontext->serverid);

    if (TLScontext->log_mask & TLS_LOG_CACHE)
	msg_info("%s: save session %s to %s cache", TLScontext->namaddr,
		 STR(cache_id), TLScontext->cache_type);

    /*
     * Passivate and save the session state.
     */
    session_data = tls_session_passivate(session);
    if (session_data)
	tls_mgr_update(TLScontext->cache_type, STR(cache_id),
		       STR(session_data), LEN(session_data));

    /*
     * Clean up.
     */
    if (session_data)
	vstring_free(session_data);
    vstring_free(cache_id);
    SSL_SESSION_free(session);			/* 200502 */

    return (1);
}

#define NOENGINE	((ENGINE *) 0)
#define TLS_TKT_NOKEYS -1		/* No keys for encryption */
#define TLS_TKT_STALE	0		/* No matching keys for decryption */
#define TLS_TKT_ACCEPT	1		/* Ticket decryptable and re-usable */
#define TLS_TKT_REISSUE	2		/* Ticket decryptable, not re-usable */

#if !defined(OPENSSL_NO_TLSEXT)

#if OPENSSL_VERSION_PREREQ(3,0)

/* ticket_cb - configure tls session ticket encrypt/decrypt context */

static int ticket_cb(SSL *con, unsigned char name[], unsigned char iv[],
		         EVP_CIPHER_CTX *ctx, EVP_MAC_CTX *hctx, int create)
{
    OSSL_PARAM params[3];
    TLS_TICKET_KEY *key;
    TLS_SESS_STATE *TLScontext = SSL_get_ex_data(con, TLScontext_index);
    int     timeout = ((int) SSL_CTX_get_timeout(SSL_get_SSL_CTX(con))) / 2;

    if ((key = tls_mgr_key(create ? 0 : name, timeout)) == 0
	|| (create && RAND_bytes(iv, TLS_TICKET_IVLEN) <= 0))
	return (create ? TLS_TKT_NOKEYS : TLS_TKT_STALE);

    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
						 LN_sha256, 0);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY,
						  (char *) key->hmac,
						  TLS_TICKET_MACLEN);
    params[2] = OSSL_PARAM_construct_end();
    if (!EVP_MAC_CTX_set_params(hctx, params))
	return (create ? TLS_TKT_NOKEYS : TLS_TKT_STALE);

    if (create) {
	EVP_EncryptInit_ex(ctx, tkt_cipher, NOENGINE, key->bits, iv);
	memcpy((void *) name, (void *) key->name, TLS_TICKET_NAMELEN);
	if (TLScontext->log_mask & TLS_LOG_CACHE)
	    msg_info("%s: Issuing session ticket, key expiration: %ld",
		     TLScontext->namaddr, (long) key->tout);
    } else {
	EVP_DecryptInit_ex(ctx, tkt_cipher, NOENGINE, key->bits, iv);
	if (TLScontext->log_mask & TLS_LOG_CACHE)
	    msg_info("%s: Decrypting session ticket, key expiration: %ld",
		     TLScontext->namaddr, (long) key->tout);
	TLScontext->ticketed = 1;
    }
    return (TLS_TKT_ACCEPT);
}

#else					/* OPENSSL_VERSION_PREREQ(3,0) */

/* ticket_cb - configure tls session ticket encrypt/decrypt context */

static int ticket_cb(SSL *con, unsigned char name[], unsigned char iv[],
		             EVP_CIPHER_CTX *ctx, HMAC_CTX *hctx, int create)
{
    static const EVP_MD *sha256;
    TLS_TICKET_KEY *key;
    TLS_SESS_STATE *TLScontext = SSL_get_ex_data(con, TLScontext_index);
    int     timeout = ((int) SSL_CTX_get_timeout(SSL_get_SSL_CTX(con))) / 2;

    if ((!sha256 && (sha256 = EVP_sha256()) == 0)
	|| (key = tls_mgr_key(create ? 0 : name, timeout)) == 0
	|| (create && RAND_bytes(iv, TLS_TICKET_IVLEN) <= 0))
	return (create ? TLS_TKT_NOKEYS : TLS_TKT_STALE);

    HMAC_Init_ex(hctx, key->hmac, TLS_TICKET_MACLEN, sha256, NOENGINE);

    if (create) {
	EVP_EncryptInit_ex(ctx, tkt_cipher, NOENGINE, key->bits, iv);
	memcpy((void *) name, (void *) key->name, TLS_TICKET_NAMELEN);
	if (TLScontext->log_mask & TLS_LOG_CACHE)
	    msg_info("%s: Issuing session ticket, key expiration: %ld",
		     TLScontext->namaddr, (long) key->tout);
    } else {
	EVP_DecryptInit_ex(ctx, tkt_cipher, NOENGINE, key->bits, iv);
	if (TLScontext->log_mask & TLS_LOG_CACHE)
	    msg_info("%s: Decrypting session ticket, key expiration: %ld",
		     TLScontext->namaddr, (long) key->tout);
	TLScontext->ticketed = 1;
    }
    return (TLS_TKT_ACCEPT);
}

#endif					/* OPENSSL_VERSION_PREREQ(3,0) */

#endif					/* defined(SSL_OP_NO_TICKET) &&
					 * !defined(OPENSSL_NO_TLSEXT) */

/* tls_server_init - initialize the server-side TLS engine */

TLS_APPL_STATE *tls_server_init(const TLS_SERVER_INIT_PROPS *props)
{
    SSL_CTX *server_ctx;
    SSL_CTX *sni_ctx;
    X509_STORE *cert_store;
    long    off = 0;
    int     verify_flags = SSL_VERIFY_NONE;
    int     cachable;
    int     scache_timeout;
    int     ticketable = 0;
    int     protomask;
    int     min_proto;
    int     max_proto;
    TLS_APPL_STATE *app_ctx;
    int     log_mask;

    /*
     * Convert user loglevel to internal logmask.
     */
    log_mask = tls_log_mask(props->log_param, props->log_level);

    if (log_mask & TLS_LOG_VERBOSE)
	msg_info("initializing the server-side TLS engine");

    /*
     * Load (mostly cipher related) TLS-library internal main.cf parameters.
     */
    tls_param_init();

    /*
     * Detect mismatch between compile-time headers and run-time library.
     */
    tls_check_version();

    /*
     * Initialize the OpenSSL library, possibly loading its configuration
     * file.
     */
    if (tls_library_init() == 0)
	return (0);

    /*
     * First validate the protocols. If these are invalid, we can't continue.
     */
    protomask = tls_proto_mask_lims(props->protocols, &min_proto, &max_proto);
    if (protomask == TLS_PROTOCOL_INVALID) {
	/* tls_protocol_mask() logs no warning. */
	msg_warn("Invalid TLS protocol list \"%s\": disabling TLS support",
		 props->protocols);
	return (0);
    }

    /*
     * Create an application data index for SSL objects, so that we can
     * attach TLScontext information; this information is needed inside
     * tls_verify_certificate_callback().
     */
    if (TLScontext_index < 0) {
	if ((TLScontext_index = SSL_get_ex_new_index(0, 0, 0, 0, 0)) < 0) {
	    msg_warn("Cannot allocate SSL application data index: "
		     "disabling TLS support");
	    return (0);
	}
    }

    /*
     * If the administrator specifies an unsupported digest algorithm, fail
     * now, rather than in the middle of a TLS handshake.
     */
    if (!tls_validate_digest(props->mdalg)) {
	msg_warn("disabling TLS support");
	return (0);
    }

    /*
     * Initialize the PRNG (Pseudo Random Number Generator) with some seed
     * from external and internal sources. Don't enable TLS without some real
     * entropy.
     */
    if (tls_ext_seed(var_tls_daemon_rand_bytes) < 0) {
	msg_warn("no entropy for TLS key generation: disabling TLS support");
	return (0);
    }
    tls_int_seed();

    /*
     * The SSL/TLS specifications require the client to send a message in the
     * oldest specification it understands with the highest level it
     * understands in the message. Netscape communicator can still
     * communicate with SSLv2 servers, so it sends out a SSLv2 client hello.
     * To deal with it, our server must be SSLv2 aware (even if we don't like
     * SSLv2), so we need to have the SSLv23 server here. If we want to limit
     * the protocol level, we can add an option to not use SSLv2/v3/TLSv1
     * later.
     */
    ERR_clear_error();
    server_ctx = SSL_CTX_new(TLS_server_method());
    if (server_ctx == 0) {
	msg_warn("cannot allocate server SSL_CTX: disabling TLS support");
	tls_print_errors();
	return (0);
    }
    sni_ctx = SSL_CTX_new(TLS_server_method());
    if (sni_ctx == 0) {
	SSL_CTX_free(server_ctx);
	msg_warn("cannot allocate server SNI SSL_CTX: disabling TLS support");
	tls_print_errors();
	return (0);
    }
#ifdef SSL_SECOP_PEER
    /* Backwards compatible security as a base for opportunistic TLS. */
    SSL_CTX_set_security_level(server_ctx, 0);
    SSL_CTX_set_security_level(sni_ctx, 0);
#endif

    /*
     * See the verify callback in tls_verify.c
     */
    SSL_CTX_set_verify_depth(server_ctx, props->verifydepth + 1);
    SSL_CTX_set_verify_depth(sni_ctx, props->verifydepth + 1);

    /*
     * The session cache is implemented by the tlsmgr(8) server.
     * 
     * XXX 200502 Surprise: when OpenSSL purges an entry from the in-memory
     * cache, it also attempts to purge the entry from the on-disk cache.
     * This is undesirable, especially when we set the in-memory cache size
     * to 1. For this reason we don't allow OpenSSL to purge on-disk cache
     * entries, and leave it up to the tlsmgr process instead. Found by
     * Victor Duchovni.
     */
    if (tls_mgr_policy(props->cache_type, &cachable,
		       &scache_timeout) != TLS_MGR_STAT_OK)
	scache_timeout = 0;
    if (scache_timeout <= 0)
	cachable = 0;

    /*
     * Presently we use TLS only with SMTP where truncation attacks are not
     * possible as a result of application framing.  If we ever use TLS in
     * some other application protocol where truncation could be relevant,
     * we'd need to disable truncation detection conditionally, or explicitly
     * clear the option in that code path.
     */
    off |= SSL_OP_IGNORE_UNEXPECTED_EOF;

    /*
     * Protocol work-arounds, OpenSSL version dependent.
     */
    off |= tls_bug_bits();

    /*
     * Add SSL_OP_NO_TICKET when the timeout is zero or library support is
     * incomplete.
     */
#ifndef OPENSSL_NO_TLSEXT
    ticketable = (*var_tls_tkt_cipher && scache_timeout > 0
		  && !(off & SSL_OP_NO_TICKET));
    if (ticketable) {
#if OPENSSL_VERSION_PREREQ(3,0)
	tkt_cipher = EVP_CIPHER_fetch(NULL, var_tls_tkt_cipher, NULL);
#else
	tkt_cipher = EVP_get_cipherbyname(var_tls_tkt_cipher);
#endif
	if (tkt_cipher == 0
	    || EVP_CIPHER_mode(tkt_cipher) != EVP_CIPH_CBC_MODE
	    || EVP_CIPHER_iv_length(tkt_cipher) != TLS_TICKET_IVLEN
	    || EVP_CIPHER_key_length(tkt_cipher) < TLS_TICKET_IVLEN
	    || EVP_CIPHER_key_length(tkt_cipher) > TLS_TICKET_KEYLEN) {
	    msg_warn("%s: invalid value: %s; session tickets disabled",
		     VAR_TLS_TKT_CIPHER, var_tls_tkt_cipher);
	    ticketable = 0;
	}
    }
    if (ticketable) {
#if OPENSSL_VERSION_PREREQ(3,0)
	SSL_CTX_set_tlsext_ticket_key_evp_cb(server_ctx, ticket_cb);
#else
	SSL_CTX_set_tlsext_ticket_key_cb(server_ctx, ticket_cb);
#endif

	/*
	 * OpenSSL 1.1.1 introduces support for TLS 1.3, which can issue more
	 * than one ticket per handshake.  While this may be appropriate for
	 * communication between browsers and webservers, it is not terribly
	 * useful for MTAs, many of which other than Postfix don't do TLS
	 * session caching at all, and Postfix has no mechanism for storing
	 * multiple session tickets, if more than one sent, the second
	 * clobbers the first.  OpenSSL 1.1.1 servers default to issuing two
	 * tickets for non-resumption handshakes, we reduce this to one.  Our
	 * ticket decryption callback already (since 2.11) asks OpenSSL to
	 * avoid issuing new tickets when the presented ticket is re-usable.
	 */
	SSL_CTX_set_num_tickets(server_ctx, 1);
    }
#endif
    if (!ticketable)
	off |= SSL_OP_NO_TICKET;

    SSL_CTX_set_options(server_ctx, off);

    /*
     * Global protocol selection.
     */
    if (protomask != 0)
	SSL_CTX_set_options(server_ctx, TLS_SSL_OP_PROTOMASK(protomask));
    SSL_CTX_set_min_proto_version(server_ctx, min_proto);
    SSL_CTX_set_max_proto_version(server_ctx, max_proto);
    SSL_CTX_set_min_proto_version(sni_ctx, min_proto);
    SSL_CTX_set_max_proto_version(sni_ctx, max_proto);

    /*
     * Some sites may want to give the client less rope. On the other hand,
     * this could trigger inter-operability issues, the client should not
     * offer ciphers it implements poorly, but this hasn't stopped some
     * vendors from getting it wrong.
     */
    if (var_tls_preempt_clist)
	SSL_CTX_set_options(server_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

    /* Done with server_ctx options, clone to sni_ctx */
    SSL_CTX_clear_options(sni_ctx, ~0);
    SSL_CTX_set_options(sni_ctx, SSL_CTX_get_options(server_ctx));

    /*
     * Set the call-back routine to debug handshake progress.
     */
    if (log_mask & TLS_LOG_DEBUG) {
	SSL_CTX_set_info_callback(server_ctx, tls_info_callback);
	SSL_CTX_set_info_callback(sni_ctx, tls_info_callback);
    }

    /*
     * Load the CA public key certificates for both the server cert and for
     * the verification of client certificates. As provided by OpenSSL we
     * support two types of CA certificate handling: One possibility is to
     * add all CA certificates to one large CAfile, the other possibility is
     * a directory pointed to by CApath, containing separate files for each
     * CA with softlinks named after the hash values of the certificate. The
     * first alternative has the advantage that the file is opened and read
     * at startup time, so that you don't have the hassle to maintain another
     * copy of the CApath directory for chroot-jail.
     */
    if (tls_set_ca_certificate_info(server_ctx,
				    props->CAfile, props->CApath) < 0) {
	/* tls_set_ca_certificate_info() already logs a warning. */
	SSL_CTX_free(server_ctx);		/* 200411 */
	SSL_CTX_free(sni_ctx);
	return (0);
    }

    /*
     * Always support server->client raw public keys, if they're good enough
     * for the client, they're good enough for us.
     */
    tls_enable_server_rpk(server_ctx, NULL);
    tls_enable_server_rpk(sni_ctx, NULL);

    /*
     * Upref and share the cert store.  Sadly we can't yet use
     * SSL_CTX_set1_cert_store(3) which was added in OpenSSL 1.1.0.
     */
    cert_store = SSL_CTX_get_cert_store(server_ctx);
    X509_STORE_up_ref(cert_store);
    SSL_CTX_set_cert_store(sni_ctx, cert_store);

    /*
     * Load the server public key certificate and private key from file and
     * check whether the cert matches the key. We can use RSA certificates
     * ("cert") DSA certificates ("dcert") or ECDSA certificates ("eccert").
     * All three can be made available at the same time. The CA certificates
     * for all three are handled in the same setup already finished. Which
     * one is used depends on the cipher negotiated (that is: the first
     * cipher listed by the client which does match the server). A client
     * with RSA only (e.g. Netscape) will use the RSA certificate only. A
     * client with openssl-library will use RSA first if not especially
     * changed in the cipher setup.
     */
    if (tls_set_my_certificate_key_info(server_ctx,
					props->chain_files,
					props->cert_file,
					props->key_file,
					props->dcert_file,
					props->dkey_file,
					props->eccert_file,
					props->eckey_file) < 0) {
	/* tls_set_my_certificate_key_info() already logs a warning. */
	SSL_CTX_free(server_ctx);		/* 200411 */
	SSL_CTX_free(sni_ctx);
	return (0);
    }

    /*
     * Diffie-Hellman key generation parameters can either be loaded from
     * files (preferred) or taken from compiled in values. First, set the
     * callback that will select the values when requested, then load the
     * (possibly) available DH parameters from files. We are generous with
     * the error handling, since we do have default values compiled in, so we
     * will not abort but just log the error message.
     */
    if (*props->dh1024_param_file != 0)
	tls_set_dh_from_file(props->dh1024_param_file);
    tls_tmp_dh(server_ctx, 1);
    tls_tmp_dh(sni_ctx, 1);

    /*
     * Enable EECDH if available, errors are not fatal, we just keep going
     * with any remaining key-exchange algorithms.  With OpenSSL 3.0 and TLS
     * 1.3, the same applies to the FFDHE groups which become part of a
     * unified "groups" list.
     */
    tls_auto_groups(server_ctx, var_tls_eecdh_auto, var_tls_ffdhe_auto);
    tls_auto_groups(sni_ctx, var_tls_eecdh_auto, var_tls_ffdhe_auto);

    /*
     * If we want to check client certificates, we have to indicate it in
     * advance. By now we only allow to decide on a global basis. If we want
     * to allow certificate based relaying, we must ask the client to provide
     * one with SSL_VERIFY_PEER. The client now can decide, whether it
     * provides one or not. We can enforce a failure of the negotiation with
     * SSL_VERIFY_FAIL_IF_NO_PEER_CERT, if we do not allow a connection
     * without one. In the "server hello" following the initialization by the
     * "client hello" the server must provide a list of CAs it is willing to
     * accept. Some clever clients will then select one from the list of
     * available certificates matching these CAs. Netscape Communicator will
     * present the list of certificates for selecting the one to be sent, or
     * it will issue a warning, if there is no certificate matching the
     * available CAs.
     * 
     * With regard to the purpose of the certificate for relaying, we might like
     * a later negotiation, maybe relaying would already be allowed for other
     * reasons, but this would involve severe changes in the internal postfix
     * logic, so we have to live with it the way it is.
     */
    if (props->ask_ccert)
	verify_flags = SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE;
    SSL_CTX_set_verify(server_ctx, verify_flags,
		       tls_verify_certificate_callback);
    SSL_CTX_set_verify(sni_ctx, verify_flags,
		       tls_verify_certificate_callback);
    if (props->ask_ccert && *props->CAfile) {
	STACK_OF(X509_NAME) *calist = SSL_load_client_CA_file(props->CAfile);

	if (calist == 0) {
	    /* Not generally critical */
	    msg_warn("error loading client CA names from: %s",
		     props->CAfile);
	    tls_print_errors();
	}
	SSL_CTX_set_client_CA_list(server_ctx, calist);

	if (calist != 0 && sk_X509_NAME_num(calist) > 0) {
	    calist = SSL_dup_CA_list(calist);

	    if (calist == 0) {
		msg_warn("error duplicating client CA names for SNI");
		tls_print_errors();
	    } else {
		SSL_CTX_set_client_CA_list(sni_ctx, calist);
	    }
	}
    }

    /*
     * Initialize our own TLS server handle, before diving into the details
     * of TLS session cache management.
     */
    app_ctx = tls_alloc_app_context(server_ctx, sni_ctx, log_mask);

    if (cachable || ticketable || props->set_sessid) {

	/*
	 * Initialize the session cache.
	 * 
	 * With a large number of concurrent smtpd(8) processes, it is not a
	 * good idea to cache multiple large session objects in each process.
	 * We set the internal cache size to 1, and don't register a
	 * "remove_cb" so as to avoid deleting good sessions from the
	 * external cache prematurely (when the internal cache is full,
	 * OpenSSL removes sessions from the external cache also)!
	 * 
	 * This makes SSL_CTX_remove_session() not useful for flushing broken
	 * sessions from the external cache, so we must delete them directly
	 * (not via a callback).
	 * 
	 * Set a session id context to identify to what type of server process
	 * created a session. In our case, the context is simply the name of
	 * the mail system: "Postfix/TLS".
	 */
	SSL_CTX_sess_set_cache_size(server_ctx, 1);
	SSL_CTX_set_session_id_context(server_ctx,
				       (void *) &server_session_id_context,
				       sizeof(server_session_id_context));
	SSL_CTX_set_session_cache_mode(server_ctx,
				       SSL_SESS_CACHE_SERVER |
				       SSL_SESS_CACHE_NO_INTERNAL |
				       SSL_SESS_CACHE_NO_AUTO_CLEAR);
	if (cachable) {
	    app_ctx->cache_type = mystrdup(props->cache_type);

	    SSL_CTX_sess_set_get_cb(server_ctx, get_server_session_cb);
	    SSL_CTX_sess_set_new_cb(server_ctx, new_server_session_cb);
	}

	/*
	 * OpenSSL ignores timed-out sessions. We need to set the internal
	 * cache timeout at least as high as the external cache timeout. This
	 * applies even if no internal cache is used.  We set the session
	 * lifetime to twice the cache lifetime, which is also the issuing
	 * and retired key validation lifetime of session tickets keys. This
	 * way a session always lasts longer than the server's ability to
	 * decrypt its session ticket.  Otherwise, a bug in OpenSSL may fail
	 * to re-issue tickets when sessions decrypt, but are expired.
	 */
	SSL_CTX_set_timeout(server_ctx, 2 * scache_timeout);
    } else {

	/*
	 * If we have no external cache, disable all caching. No use wasting
	 * server memory resources with sessions they are unlikely to be able
	 * to reuse.
	 */
	SSL_CTX_set_session_cache_mode(server_ctx, SSL_SESS_CACHE_OFF);
    }

    return (app_ctx);
}

 /*
  * This is the actual startup routine for a new connection. We expect that
  * the SMTP buffers are flushed and the "220 Ready to start TLS" was sent to
  * the client, so that we can immediately start the TLS handshake process.
  */
TLS_SESS_STATE *tls_server_start(const TLS_SERVER_START_PROPS *props)
{
    int     sts;
    TLS_SESS_STATE *TLScontext;
    const char *cipher_list;
    TLS_APPL_STATE *app_ctx = props->ctx;
    int     log_mask = app_ctx->log_mask;

    /*
     * Implicitly enable logging of trust chain errors when verified certs
     * are required.
     */
    if (props->requirecert)
	log_mask |= TLS_LOG_UNTRUSTED;

    if (log_mask & TLS_LOG_VERBOSE)
	msg_info("setting up TLS connection from %s", props->namaddr);

    /*
     * Allocate a new TLScontext for the new connection and get an SSL
     * structure. Add the location of TLScontext to the SSL to later retrieve
     * the information inside the tls_verify_certificate_callback().
     */
    TLScontext = tls_alloc_sess_context(log_mask, props->namaddr);
    TLScontext->cache_type = app_ctx->cache_type;

    ERR_clear_error();
    if ((TLScontext->con = (SSL *) SSL_new(app_ctx->ssl_ctx)) == 0) {
	msg_warn("Could not allocate 'TLScontext->con' with SSL_new()");
	tls_print_errors();
	tls_free_context(TLScontext);
	return (0);
    }
    cipher_list = tls_set_ciphers(TLScontext, props->cipher_grade,
				  props->cipher_exclusions);
    if (cipher_list == 0) {
	/* already warned */
	tls_free_context(TLScontext);
	return (0);
    }
    if (log_mask & TLS_LOG_VERBOSE)
	msg_info("%s: TLS cipher list \"%s\"", props->namaddr, cipher_list);

    TLScontext->serverid = mystrdup(props->serverid);
    TLScontext->am_server = 1;
    TLScontext->stream = props->stream;
    TLScontext->mdalg = props->mdalg;

    if (!SSL_set_ex_data(TLScontext->con, TLScontext_index, TLScontext)) {
	msg_warn("Could not set application data for 'TLScontext->con'");
	tls_print_errors();
	tls_free_context(TLScontext);
	return (0);
    }

    /*
     * When encryption is mandatory use the 80-bit plus OpenSSL security
     * level.
     */
    if (props->requirecert)
	SSL_set_security_level(TLScontext->con, 1);

    /*
     * Also enable client->server raw public keys, provided we're not
     * interested in client certificate fingerprints.
     */
    if (props->enable_rpk)
	tls_enable_client_rpk(NULL, TLScontext->con);

    /*
     * Before really starting anything, try to seed the PRNG a little bit
     * more.
     */
    tls_int_seed();
    (void) tls_ext_seed(var_tls_daemon_rand_bytes);

    /*
     * Connect the SSL connection with the network socket.
     */
    if (SSL_set_fd(TLScontext->con, props->stream == 0 ? props->fd :
		   vstream_fileno(props->stream)) != 1) {
	msg_info("SSL_set_fd error to %s", props->namaddr);
	tls_print_errors();
	uncache_session(app_ctx->ssl_ctx, TLScontext);
	tls_free_context(TLScontext);
	return (0);
    }

    /*
     * If the debug level selected is high enough, all of the data is dumped:
     * TLS_LOG_TLSPKTS will dump the SSL negotiation, TLS_LOG_ALLPKTS will
     * dump everything.
     * 
     * We do have an SSL_set_fd() and now suddenly a BIO_ routine is called?
     * Well there is a BIO below the SSL routines that is automatically
     * created for us, so we can use it for debugging purposes.
     */
    if (log_mask & TLS_LOG_TLSPKTS)
	tls_set_bio_callback(SSL_get_rbio(TLScontext->con), tls_bio_dump_cb);

    /*
     * If we don't trigger the handshake in the library, leave control over
     * SSL_accept/read/write/etc with the application.
     */
    if (props->stream == 0)
	return (TLScontext);

    /*
     * Turn on non-blocking I/O so that we can enforce timeouts on network
     * I/O.
     */
    non_blocking(vstream_fileno(props->stream), NON_BLOCKING);

    /*
     * Start TLS negotiations. This process is a black box that invokes our
     * call-backs for session caching and certificate verification.
     * 
     * Error handling: If the SSL handshake fails, we print out an error message
     * and remove all TLS state concerning this session.
     */
    sts = tls_bio_accept(vstream_fileno(props->stream), props->timeout,
			 TLScontext);
    if (sts <= 0) {
	if (ERR_peek_error() != 0) {
	    msg_info("SSL_accept error from %s: %d", props->namaddr, sts);
	    tls_print_errors();
	} else if (errno != 0) {
	    msg_info("SSL_accept error from %s: %m", props->namaddr);
	} else {
	    msg_info("SSL_accept error from %s: lost connection",
		     props->namaddr);
	}
	tls_free_context(TLScontext);
	return (0);
    }
    return (tls_server_post_accept(TLScontext));
}

/* tls_server_post_accept - post-handshake processing */

TLS_SESS_STATE *tls_server_post_accept(TLS_SESS_STATE *TLScontext)
{
    const SSL_CIPHER *cipher;
    X509   *peer;
    EVP_PKEY *pkey = 0;
    char    buf[CCERT_BUFSIZ];

    /* Turn off packet dump if only dumping the handshake */
    if ((TLScontext->log_mask & TLS_LOG_ALLPKTS) == 0)
	tls_set_bio_callback(SSL_get_rbio(TLScontext->con), 0);

    /*
     * The caller may want to know if this session was reused or if a new
     * session was negotiated.
     */
    TLScontext->session_reused = SSL_session_reused(TLScontext->con);
    if ((TLScontext->log_mask & TLS_LOG_CACHE) && TLScontext->session_reused)
	msg_info("%s: Reusing old session%s", TLScontext->namaddr,
		 TLScontext->ticketed ? " (RFC 5077 session ticket)" : "");

    /*
     * Let's see whether a peer certificate is available and what is the
     * actual information. We want to save it for later use.
     */
    peer = TLS_PEEK_PEER_CERT(TLScontext->con);
    if (peer) {
	pkey = X509_get0_pubkey(peer);
    }
#if OPENSSL_VERSION_PREREQ(3,2)
    else {
	pkey = SSL_get0_peer_rpk(TLScontext->con);
    }
#endif

    if (peer != NULL) {
	TLScontext->peer_status |= TLS_CRED_FLAG_CERT;
	if (SSL_get_verify_result(TLScontext->con) == X509_V_OK)
	    TLScontext->peer_status |= TLS_CERT_FLAG_TRUSTED;

	if (TLScontext->log_mask & TLS_LOG_VERBOSE) {
	    X509_NAME_oneline(X509_get_subject_name(peer),
			      buf, sizeof(buf));
	    msg_info("subject=%s", printable(buf, '?'));
	    X509_NAME_oneline(X509_get_issuer_name(peer),
			      buf, sizeof(buf));
	    msg_info("issuer=%s", printable(buf, '?'));
	}
	TLScontext->peer_CN = tls_peer_CN(peer, TLScontext);
	TLScontext->issuer_CN = tls_issuer_CN(peer, TLScontext);
	TLScontext->peer_cert_fprint =
	    tls_cert_fprint(peer, TLScontext->mdalg);
	TLScontext->peer_pkey_fprint =
	    tls_pkey_fprint(pkey, TLScontext->mdalg);

	if (TLScontext->log_mask & (TLS_LOG_VERBOSE | TLS_LOG_PEERCERT)) {
	    msg_info("%s: subject_CN=%s, issuer=%s%s%s%s%s",
		     TLScontext->namaddr,
		     TLScontext->peer_CN, TLScontext->issuer_CN,
		     *TLScontext->peer_cert_fprint ?
		     ", cert fingerprint=" : "",
		     *TLScontext->peer_cert_fprint ?
		     TLScontext->peer_cert_fprint : "",
		     *TLScontext->peer_pkey_fprint ?
		     ", pkey fingerprint=" : "",
		     *TLScontext->peer_pkey_fprint ?
		     TLScontext->peer_pkey_fprint : "");
	}
	TLS_FREE_PEER_CERT(peer);

	/*
	 * Give them a clue. Problems with trust chain verification are
	 * logged when the session is first negotiated, before the session is
	 * stored into the cache. We don't want mystery failures, so log the
	 * fact the real problem is to be found in the past.
	 */
	if (!TLS_CERT_IS_TRUSTED(TLScontext)
	    && (TLScontext->log_mask & TLS_LOG_UNTRUSTED)) {
	    if (TLScontext->session_reused == 0)
		tls_log_verify_error(TLScontext, (struct TLSRPT_WRAPPER *) 0);
	    else
		msg_info("%s: re-using session with untrusted certificate, "
			 "look for details earlier in the log",
			 TLScontext->namaddr);
	}
    } else {
	TLScontext->peer_CN = mystrdup("");
	TLScontext->issuer_CN = mystrdup("");
	TLScontext->peer_cert_fprint = mystrdup("");
	if (!pkey) {
	    TLScontext->peer_pkey_fprint = mystrdup("");
	} else {

	    /*
	     * Raw public keys don't involve CA trust, and we don't have a
	     * way to associate DANE TLSA RRs with clients just yet, we just
	     * make the fingerprint available to the access(5) layer.
	     */
	    TLScontext->peer_status |= TLS_CRED_FLAG_RPK;
	    TLScontext->peer_pkey_fprint =
		tls_pkey_fprint(pkey, TLScontext->mdalg);
	    if (TLScontext->log_mask & (TLS_LOG_VERBOSE | TLS_LOG_PEERCERT))
		msg_info("%s: raw public key fingerprint=%s",
			 TLScontext->namaddr, TLScontext->peer_pkey_fprint);
	}
    }

    /*
     * Finally, collect information about protocol and cipher for logging
     */
    TLScontext->protocol = SSL_get_version(TLScontext->con);
    cipher = SSL_get_current_cipher(TLScontext->con);
    TLScontext->cipher_name = SSL_CIPHER_get_name(cipher);
    TLScontext->cipher_usebits = SSL_CIPHER_get_bits(cipher,
					     &(TLScontext->cipher_algbits));

    /*
     * If the library triggered the SSL handshake, switch to the
     * tls_timed_read/write() functions and make the TLScontext available to
     * those functions. Otherwise, leave control over SSL_read/write/etc.
     * with the application.
     */
    if (TLScontext->stream != 0)
	tls_stream_start(TLScontext->stream, TLScontext);

    /*
     * With the handshake done, extract TLS 1.3 signature metadata.
     */
    tls_get_signature_params(TLScontext);

    /*
     * All the key facts in a single log entry.
     */
    if (TLScontext->log_mask & TLS_LOG_SUMMARY)
	tls_log_summary(TLS_ROLE_SERVER, TLS_USAGE_NEW, TLScontext);

    tls_int_seed();

    return (TLScontext);
}

#endif					/* USE_TLS */
