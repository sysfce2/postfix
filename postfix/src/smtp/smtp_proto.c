/*++
/* NAME
/*	smtp_proto 3
/* SUMMARY
/*	client SMTP protocol
/* SYNOPSIS
/*	#include "smtp.h"
/*
/*	int	smtp_helo(state)
/*	SMTP_STATE *state;
/*
/*	int	smtp_xfer(state)
/*	SMTP_STATE *state;
/*
/*	int	smtp_rset(state)
/*	SMTP_STATE *state;
/*
/*	int	smtp_quit(state)
/*	SMTP_STATE *state;
/* DESCRIPTION
/*	This module implements the client side of the SMTP protocol.
/*
/*	smtp_helo() performs the initial handshake with the SMTP server.
/*
/*	smtp_xfer() sends message envelope information followed by the
/*	message data, and finishes the SMTP conversation. These operations
/*	are combined in one function, in order to implement SMTP pipelining.
/*	Recipients are marked as "done" in the mail queue file when
/*	bounced or delivered. The message delivery status is updated
/*	accordingly.
/*
/*	smtp_rset() sends a single RSET command and waits for the
/*	response. In case of no response, or negative response, it
/*	turns off caching for the current session.
/*
/*	smtp_quit() sends a single QUIT command and waits for the
/*	response if configured to do so. It always turns off caching
/*	for the current session.
/* DIAGNOSTICS
/*	smtp_helo(), smtp_xfer(), smtp_rset() and smtp_quit() return
/*	0 in case of success, -1 in case of failure. For smtp_xfer(),
/*	smtp_rset() and smtp_quit(), success means the ability to
/*	perform an SMTP conversation, not necessarily the ability
/*	to deliver mail, or the achievement of server happiness.
/*
/*	Warnings: corrupt message file. A corrupt message is marked
/*	as "corrupt" by changing its queue file permissions.
/* BUGS
/*	Some SMTP servers will abort when the number of recipients
/*	for one message exceeds their capacity. This behavior violates
/*	the SMTP protocol.
/*	The only way around this is to limit the number of recipients
/*	per transaction to an artificially-low value.
/* SEE ALSO
/*	smtp(3h) internal data structures
/*	smtp_chat(3) query/reply SMTP support
/*	smtp_trouble(3) error handlers
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
/*	Pipelining code in cooperation with:
/*	Jon Ribbens
/*	Oaktree Internet Solutions Ltd.,
/*	Internet House,
/*	Canal Basin,
/*	Coventry,
/*	CV1 4LY, United Kingdom.
/*
/*	Connection caching in cooperation with:
/*	Victor Duchovni
/*	Morgan Stanley
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <sys/socket.h>			/* shutdown(2) */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>			/* 44BSD stdarg.h uses abort() */
#include <stdarg.h>
#include <time.h>

#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif

/* Utility library. */

#include <msg.h>
#include <vstring.h>
#include <vstream.h>
#include <vstring_vstream.h>
#include <stringops.h>
#include <mymalloc.h>
#include <iostuff.h>
#include <split_at.h>
#include <name_code.h>

/* Global library. */

#include <mail_params.h>
#include <smtp_stream.h>
#include <mail_queue.h>
#include <recipient_list.h>
#include <deliver_request.h>
#include <defer.h>
#include <bounce.h>
#include <record.h>
#include <rec_type.h>
#include <off_cvt.h>
#include <mark_corrupt.h>
#include <quote_821_local.h>
#include <mail_proto.h>
#include <mime_state.h>

/* Application-specific. */

#include "smtp.h"
#include "smtp_sasl.h"

 /*
  * Sender and receiver state. A session does not necessarily go through a
  * linear progression, but states are guaranteed to not jump backwards.
  * Normal sessions go from MAIL->RCPT->DATA->DOT->QUIT->LAST. The states
  * MAIL, RCPT, and DATA may also be followed by ABORT->QUIT->LAST.
  * 
  * When connection caching is enabled, the QUIT state is suppressed. Normal
  * sessions proceed as MAIL->RCPT->DATA->DOT->LAST, while aborted sessions
  * end with ABORT->LAST. The connection is left open for a limited time. An
  * RSET probe should be sent before attempting to reuse an open connection
  * for a new transaction.
  * 
  * The code to send an RSET probe is a special case with its own initial state
  * and with its own dedicated state transitions. The session proceeds as
  * RSET->LAST. This code is kept inside the main protocol engine for
  * consistent error handling and error reporting. It is not to be confused
  * with the code that sends RSET to abort a mail transaction in progress.
  * 
  * The code to send QUIT without message delivery transaction jumps into the
  * main state machine. If this introduces complications, then we should
  * introduce a second QUIT state with its own dedicated state transitions,
  * just like we did for RSET probes.
  * 
  * By default, the receiver skips the QUIT response. Some SMTP servers
  * disconnect after responding to ".", and some SMTP servers wait before
  * responding to QUIT.
  * 
  * Client states that are associated with sending mail (up to and including
  * SMTP_STATE_DOT) must have smaller numerical values than the non-sending
  * states (SMTP_STATE_ABORT .. SMTP_STATE_LAST).
  */
#define SMTP_STATE_XFORWARD_NAME_ADDR 0
#define SMTP_STATE_XFORWARD_PROTO_HELO 1
#define SMTP_STATE_MAIL		2
#define SMTP_STATE_RCPT		3
#define SMTP_STATE_DATA		4
#define SMTP_STATE_DOT		5
#define SMTP_STATE_ABORT	6
#define SMTP_STATE_RSET		7
#define SMTP_STATE_QUIT		8
#define SMTP_STATE_LAST		9

int    *xfer_timeouts[SMTP_STATE_LAST] = {
    &var_smtp_xfwd_tmout,		/* name/addr */
    &var_smtp_xfwd_tmout,		/* helo/proto */
    &var_smtp_mail_tmout,
    &var_smtp_rcpt_tmout,
    &var_smtp_data0_tmout,
    &var_smtp_data2_tmout,
    &var_smtp_rset_tmout,
    &var_smtp_rset_tmout,
    &var_smtp_quit_tmout,
};

char   *xfer_states[SMTP_STATE_LAST] = {
    "sending XFORWARD name/address",
    "sending XFORWARD protocol/helo_name",
    "sending MAIL FROM",
    "sending RCPT TO",
    "sending DATA command",
    "sending end of data -- message may be sent more than once",
    "sending final RSET",
    "sending RSET probe",
    "sending QUIT",
};

char   *xfer_request[SMTP_STATE_LAST] = {
    "XFORWARD name/address command",
    "XFORWARD helo/protocol command",
    "MAIL FROM command",
    "RCPT TO command",
    "DATA command",
    "end of DATA command",
    "final RSET command",
    "RSET probe",
    "QUIT command",
};

/* smtp_helo - perform initial handshake with SMTP server */

int     smtp_helo(SMTP_STATE *state, int misc_flags)
{
    char   *myname = "smtp_helo";
    SMTP_SESSION *session = state->session;
    DELIVER_REQUEST *request = state->request;
    SMTP_RESP *resp;
    int     except;
    char   *lines;
    char   *words;
    char   *word;
    int     n;
    static NAME_CODE xforward_features[] = {
	XFORWARD_NAME, SMTP_FEATURE_XFORWARD_NAME,
	XFORWARD_ADDR, SMTP_FEATURE_XFORWARD_ADDR,
	XFORWARD_PROTO, SMTP_FEATURE_XFORWARD_PROTO,
	XFORWARD_HELO, SMTP_FEATURE_XFORWARD_HELO,
	0, 0,
    };
    SOCKOPT_SIZE optlen;

    /*
     * Prepare for disaster.
     */
    smtp_timeout_setup(state->session->stream, var_smtp_helo_tmout);
    if ((except = vstream_setjmp(state->session->stream)) != 0)
	return (smtp_stream_except(state, except,
				   "receiving the initial SMTP greeting"));

    /*
     * Read and parse the server's SMTP greeting banner.
     */
    switch ((resp = smtp_chat_resp(session))->code / 100) {
    case 2:
	break;
    case 5:
	if (var_smtp_skip_5xx_greeting)
	    resp->code = 400;
    default:
	return (smtp_site_fail(state, resp->code,
			       "host %s refused to talk to me: %s",
			       session->namaddr,
			       translit(resp->str, "\n", " ")));
    }

    /*
     * XXX Some PIX firewall versions require flush before ".<CR><LF>" so it
     * does not span a packet boundary. This hurts performance so it is not
     * on by default.
     */
    if (resp->str[strspn(resp->str, "20 *\t\n")] == 0)
	session->features |= SMTP_FEATURE_MAYBEPIX;

    /*
     * See if we are talking to ourself. This should not be possible with the
     * way we implement DNS lookups. However, people are known to sometimes
     * screw up the naming service. And, mailer loops are still possible when
     * our own mailer routing tables are mis-configured.
     */
    words = resp->str;
    (void) mystrtok(&words, "- \t\n");
    for (n = 0; (word = mystrtok(&words, " \t\n")) != 0; n++) {
	if (n == 0 && strcasecmp(word, var_myhostname) == 0) {
	    if (misc_flags & SMTP_MISC_FLAG_LOOP_DETECT)
		msg_warn("host %s greeted me with my own hostname %s",
			 session->namaddr, var_myhostname);
	} else if (strcasecmp(word, "ESMTP") == 0)
	    session->features |= SMTP_FEATURE_ESMTP;
    }
    if (var_smtp_always_ehlo
	&& (session->features & SMTP_FEATURE_MAYBEPIX) == 0)
	session->features |= SMTP_FEATURE_ESMTP;
    if (var_smtp_never_ehlo
	|| (session->features & SMTP_FEATURE_MAYBEPIX) != 0)
	session->features &= ~SMTP_FEATURE_ESMTP;

    /*
     * Return the compliment. Fall back to SMTP if our ESMTP recognition
     * heuristic failed.
     */
    if (session->features & SMTP_FEATURE_ESMTP) {
	smtp_chat_cmd(session, "EHLO %s", var_smtp_helo_name);
	if ((resp = smtp_chat_resp(session))->code / 100 != 2)
	    session->features &= ~SMTP_FEATURE_ESMTP;
    }
    if ((session->features & SMTP_FEATURE_ESMTP) == 0) {
	smtp_chat_cmd(session, "HELO %s", var_smtp_helo_name);
	if ((resp = smtp_chat_resp(session))->code / 100 != 2)
	    return (smtp_site_fail(state, resp->code,
				   "host %s refused to talk to me: %s",
				   session->namaddr,
				   translit(resp->str, "\n", " ")));
	return (0);
    }

    /*
     * Pick up some useful features offered by the SMTP server. XXX Until we
     * have a portable routine to convert from string to off_t with proper
     * overflow detection, ignore the message size limit advertised by the
     * SMTP server. Otherwise, we might do the wrong thing when the server
     * advertises a really huge message size limit.
     * 
     * XXX Allow for "code (SP|-) ehlo-keyword (SP|=) ehlo-param...", because
     * MicroSoft implemented AUTH based on an old draft.
     */
    lines = resp->str;
    while ((words = mystrtok(&lines, "\n")) != 0) {
	if (mystrtok(&words, "- ") && (word = mystrtok(&words, " \t=")) != 0) {
	    if (strcasecmp(word, "8BITMIME") == 0)
		session->features |= SMTP_FEATURE_8BITMIME;
	    else if (strcasecmp(word, "PIPELINING") == 0)
		session->features |= SMTP_FEATURE_PIPELINING;
	    else if (strcasecmp(word, "XFORWARD") == 0)
		while ((word = mystrtok(&words, " \t")) != 0)
		    session->features |= name_code(xforward_features,
						 NAME_CODE_FLAG_NONE, word);
	    else if (strcasecmp(word, "SIZE") == 0) {
		session->features |= SMTP_FEATURE_SIZE;
		if ((word = mystrtok(&words, " \t")) != 0) {
		    if (!alldig(word))
			msg_warn("bad size limit \"%s\" in EHLO reply from %s",
				 word, session->namaddr);
		    else
			session->size_limit = off_cvt_string(word);
		}
	    }
#ifdef USE_SASL_AUTH
	    else if (var_smtp_sasl_enable && strcasecmp(word, "AUTH") == 0)
		smtp_sasl_helo_auth(session, words);
#endif
	    else if (strcasecmp(word, var_myhostname) == 0) {
		if (misc_flags & SMTP_MISC_FLAG_LOOP_DETECT) {
		    msg_warn("host %s replied to HELO/EHLO with my own hostname %s",
			     session->namaddr, var_myhostname);
		    return (smtp_site_fail(state,
		     (session->features & SMTP_FEATURE_BEST_MX) ? 550 : 450,
					 "mail for %s loops back to myself",
					   request->nexthop));
		}
	    }
	}
    }
    if (msg_verbose)
	msg_info("server features: 0x%x size %.0f",
		 session->features, (double) session->size_limit);

    /*
     * We use SMTP command pipelining if the server said it supported it.
     * Since we use blocking I/O, RFC 2197 says that we should inspect the
     * TCP window size and not send more than this amount of information.
     * Unfortunately this information is not available using the sockets
     * interface. However, we *can* get the TCP send buffer size on the local
     * TCP/IP stack. We should be able to fill this buffer without being
     * blocked, and then the kernel will effectively do non-blocking I/O for
     * us by automatically writing out the contents of its send buffer while
     * we are reading in the responses. In addition to TCP buffering we have
     * to be aware of application-level buffering by the vstream module,
     * which is limited to a couple kbytes.
     */
    if (session->features & SMTP_FEATURE_PIPELINING) {
	optlen = sizeof(session->sndbufsize);
	if (getsockopt(vstream_fileno(session->stream), SOL_SOCKET,
		     SO_SNDBUF, (char *) &session->sndbufsize, &optlen) < 0)
	    msg_fatal("%s: getsockopt: %m", myname);
	if (session->sndbufsize > VSTREAM_BUFSIZE)
	    session->sndbufsize = VSTREAM_BUFSIZE;
	if (session->sndbufsize == 0) {
	    session->sndbufsize = VSTREAM_BUFSIZE;
	    if (setsockopt(vstream_fileno(session->stream), SOL_SOCKET,
		      SO_SNDBUF, (char *) &session->sndbufsize, optlen) < 0)
		msg_fatal("%s: setsockopt: %m", myname);
	}
	if (msg_verbose)
	    msg_info("Using ESMTP PIPELINING, TCP send buffer size is %d",
		     session->sndbufsize);
    } else {
	session->sndbufsize = 0;
    }

#ifdef USE_SASL_AUTH
    if (var_smtp_sasl_enable && (session->features & SMTP_FEATURE_AUTH))
	return (smtp_sasl_helo_login(state));
#endif

    return (0);
}

/* smtp_text_out - output one header/body record */

static void smtp_text_out(void *context, int rec_type,
			          const char *text, int len,
			          off_t unused_offset)
{
    SMTP_STATE *state = (SMTP_STATE *) context;
    SMTP_SESSION *session = state->session;
    int     data_left;
    const char *data_start;

    /*
     * Deal with an impedance mismatch between Postfix queue files (record
     * length <= $message_line_length_limit) and SMTP (DATA record length <=
     * $smtp_line_length_limit). The code below does a little too much work
     * when the SMTP line length limit is disabled, but it avoids code
     * duplication, and thus, it avoids testing and maintenance problems.
     */
    data_left = len;
    data_start = text;
    do {
	if (state->space_left == var_smtp_line_limit
	    && data_left > 0 && *data_start == '.')
	    smtp_fputc('.', session->stream);
	if (var_smtp_line_limit > 0 && data_left >= state->space_left) {
	    smtp_fputs(data_start, state->space_left, session->stream);
	    data_start += state->space_left;
	    data_left -= state->space_left;
	    state->space_left = var_smtp_line_limit;
	    if (data_left > 0 || rec_type == REC_TYPE_CONT) {
		smtp_fputc(' ', session->stream);
		state->space_left -= 1;
	    }
	} else {
	    if (rec_type == REC_TYPE_CONT) {
		smtp_fwrite(data_start, data_left, session->stream);
		state->space_left -= data_left;
	    } else {
		smtp_fputs(data_start, data_left, session->stream);
		state->space_left = var_smtp_line_limit;
	    }
	    break;
	}
    } while (data_left > 0);
}

/* smtp_header_out - output one message header */

static void smtp_header_out(void *context, int unused_header_class,
			            HEADER_OPTS *unused_info, VSTRING *buf,
			            off_t offset)
{
    char   *start = vstring_str(buf);
    char   *line;
    char   *next_line;

    for (line = start; line; line = next_line) {
	next_line = split_at(line, '\n');
	smtp_text_out(context, REC_TYPE_NORM, line, next_line ?
		      next_line - line - 1 : strlen(line), offset);
    }
}

/* smtp_loop - exercise the SMTP protocol engine */

static int smtp_loop(SMTP_STATE *state, NOCLOBBER int send_state,
		             NOCLOBBER int recv_state)
{
    char   *myname = "smtp_loop";
    DELIVER_REQUEST *request = state->request;
    SMTP_SESSION *session = state->session;
    SMTP_RESP *resp;
    RECIPIENT *rcpt;
    VSTRING *next_command = vstring_alloc(100);
    NOCLOBBER int next_state;
    NOCLOBBER int next_rcpt;
    NOCLOBBER int send_rcpt;
    NOCLOBBER int recv_rcpt;
    NOCLOBBER int nrcpt;
    int     except;
    int     rec_type;
    NOCLOBBER int prev_type = 0;
    NOCLOBBER int sndbuffree;
    NOCLOBBER int mail_from_rejected;
    NOCLOBBER int downgrading;
    int     mime_errs;

    /*
     * Macros for readability.
     */
#define REWRITE_ADDRESS(dst, mid, src) do { \
	if (*(src) && var_smtp_quote_821_env) { \
	    quote_821_local(mid, src); \
	    smtp_unalias_addr(dst, vstring_str(mid)); \
	} else { \
	    vstring_strcpy(dst, src); \
	} \
    } while (0)

#define QUOTE_ADDRESS(dst, src) do { \
	if (*(src) && var_smtp_quote_821_env) { \
	    quote_821_local(dst, src); \
	} else { \
	    vstring_strcpy(dst, src); \
	} \
    } while (0)

#define RETURN(x) do { \
	vstring_free(next_command); \
	if (session->mime_state) \
	    session->mime_state = mime_state_free(session->mime_state); \
	return (x); \
    } while (0)

#define SENDER_IS_AHEAD \
	(recv_state < send_state || recv_rcpt != send_rcpt)

#define SENDER_IN_WAIT_STATE \
	(send_state == SMTP_STATE_DOT || send_state == SMTP_STATE_LAST)

#define SENDING_MAIL \
	(recv_state <= SMTP_STATE_DOT)

#define THIS_SESSION_IS_CACHED \
	(session->reuse_count > 0)

#define DONT_CACHE_THIS_SESSION \
	(session->reuse_count = 0)

#define CANT_RSET_THIS_SESSION \
	(session->features |= SMTP_FEATURE_RSET_REJECTED)

    /*
     * Sanity check. We don't want smtp_chat() to inadvertently flush the
     * output buffer. That means someone broke pipelining support.
     */
    if (session->sndbufsize > VSTREAM_BUFSIZE)
	msg_panic("bad sndbufsize %d > VSTREAM_BUFSIZE %d",
		  session->sndbufsize, VSTREAM_BUFSIZE);

    /*
     * Miscellaneous initialization. Some of this might be done in
     * smtp_xfer() but that just complicates interfaces and data structures.
     */
    sndbuffree = session->sndbufsize;

    /*
     * Pipelining support requires two loops: one loop for sending and one
     * for receiving. Each loop has its own independent state. Most of the
     * time the sender can run ahead of the receiver by as much as the TCP
     * send buffer permits. There are only two places where the sender must
     * wait for status information from the receiver: once after sending DATA
     * and once after sending QUIT.
     * 
     * The sender state advances until the TCP send buffer would overflow, or
     * until the sender needs status information from the receiver. At that
     * point the receiver starts processing responses. Once the receiver has
     * caught up with the sender, the sender resumes sending commands. If the
     * receiver detects a serious problem (MAIL FROM rejected, all RCPT TO
     * commands rejected, DATA rejected) it forces the sender to abort the
     * SMTP dialog with RSET and QUIT.
     */
    nrcpt = 0;
    next_rcpt = send_rcpt = recv_rcpt = 0;
    mail_from_rejected = 0;

    /*
     * Prepare for disaster. This should not be needed because the design
     * guarantees that no output is flushed before smtp_chat_resp() is
     * called.
     * 
     * 1) Every SMTP command fits entirely in a VSTREAM output buffer.
     * 
     * 2) smtp_loop() never invokes smtp_chat_cmd() without making sure that
     * there is sufficient space for the command in the output buffer.
     * 
     * 3) smtp_loop() flushes the output buffer to avoid server timeouts.
     * 
     * Changing any of these would violate the design, and would likely break
     * SMTP pipelining.
     * 
     * We set up the error handler anyway (only upon entry to avoid wasting
     * resources) because 1) there is code below that expects that VSTREAM
     * timeouts are enabled, and 2) this allows us to detect if someone broke
     * Postfix by introducing spurious flush before read operations.
     */
    if (send_state < SMTP_STATE_XFORWARD_NAME_ADDR
	|| send_state > SMTP_STATE_QUIT)
	msg_panic("%s: bad sender state %d (receiver state %d)",
		  myname, send_state, recv_state);
    smtp_timeout_setup(session->stream,
		       *xfer_timeouts[send_state]);
    if ((except = vstream_setjmp(session->stream)) != 0) {
	msg_warn("smtp_proto: spurious flush before read in send state %d",
		 send_state);
	RETURN(SENDING_MAIL ? smtp_stream_except(state, except,
					     xfer_states[send_state]) : -1);
    }

    /*
     * The main protocol loop.
     */
    do {

	/*
	 * Build the next command.
	 */
	switch (send_state) {

	    /*
	     * Sanity check.
	     */
	default:
	    msg_panic("%s: bad sender state %d", myname, send_state);

	    /*
	     * Build the XFORWARD command. With properly sanitized
	     * information, the command length stays within the 512 byte
	     * command line length limit.
	     */
	case SMTP_STATE_XFORWARD_NAME_ADDR:
	    vstring_strcpy(next_command, XFORWARD_CMD);
	    if (session->features & SMTP_FEATURE_XFORWARD_NAME)
		vstring_sprintf_append(next_command, " %s=%s",
		   XFORWARD_NAME, DEL_REQ_ATTR_AVAIL(request->client_name) ?
			       request->client_name : XFORWARD_UNAVAILABLE);
	    if (session->features & SMTP_FEATURE_XFORWARD_ADDR)
		vstring_sprintf_append(next_command, " %s=%s",
		   XFORWARD_ADDR, DEL_REQ_ATTR_AVAIL(request->client_addr) ?
			       request->client_addr : XFORWARD_UNAVAILABLE);
	    if (session->send_proto_helo)
		next_state = SMTP_STATE_XFORWARD_PROTO_HELO;
	    else
		next_state = SMTP_STATE_MAIL;
	    break;

	case SMTP_STATE_XFORWARD_PROTO_HELO:
	    vstring_strcpy(next_command, XFORWARD_CMD);
	    if (session->features & SMTP_FEATURE_XFORWARD_PROTO)
		vstring_sprintf_append(next_command, " %s=%s",
		 XFORWARD_PROTO, DEL_REQ_ATTR_AVAIL(request->client_proto) ?
			      request->client_proto : XFORWARD_UNAVAILABLE);
	    if (session->features & SMTP_FEATURE_XFORWARD_HELO)
		vstring_sprintf_append(next_command, " %s=%s",
		   XFORWARD_HELO, DEL_REQ_ATTR_AVAIL(request->client_helo) ?
			       request->client_helo : XFORWARD_UNAVAILABLE);
	    next_state = SMTP_STATE_MAIL;
	    break;

	    /*
	     * Build the MAIL FROM command.
	     */
	case SMTP_STATE_MAIL:
	    QUOTE_ADDRESS(session->scratch, request->sender);
	    vstring_sprintf(next_command, "MAIL FROM:<%s>",
			    vstring_str(session->scratch));
	    if (session->features & SMTP_FEATURE_SIZE)	/* RFC 1870 */
		vstring_sprintf_append(next_command, " SIZE=%lu",
				       request->data_size);
	    if (session->features & SMTP_FEATURE_8BITMIME) {	/* RFC 1652 */
		if (strcmp(request->encoding, MAIL_ATTR_ENC_8BIT) == 0)
		    vstring_strcat(next_command, " BODY=8BITMIME");
		else if (strcmp(request->encoding, MAIL_ATTR_ENC_7BIT) == 0)
		    vstring_strcat(next_command, " BODY=7BIT");
		else if (strcmp(request->encoding, MAIL_ATTR_ENC_NONE) != 0)
		    msg_warn("%s: unknown content encoding: %s",
			     request->queue_id, request->encoding);
	    }

	    /*
	     * We authenticate the local MTA only, but not the sender.
	     */
#ifdef USE_SASL_AUTH
	    if (var_smtp_sasl_enable
		&& (session->features & SMTP_FEATURE_AUTH))
		vstring_strcat(next_command, " AUTH=<>");
#endif
	    next_state = SMTP_STATE_RCPT;
	    break;

	    /*
	     * Build one RCPT TO command before we have seen the MAIL FROM
	     * response.
	     */
	case SMTP_STATE_RCPT:
	    rcpt = request->rcpt_list.info + send_rcpt;
	    QUOTE_ADDRESS(session->scratch, rcpt->address);
	    vstring_sprintf(next_command, "RCPT TO:<%s>",
			    vstring_str(session->scratch));
	    if ((next_rcpt = send_rcpt + 1) == SMTP_RCPT_LEFT(state))
		next_state = DEL_REQ_TRACE_ONLY(request->flags) ?
		    SMTP_STATE_ABORT : SMTP_STATE_DATA;
	    break;

	    /*
	     * Build the DATA command before we have seen all the RCPT TO
	     * responses.
	     */
	case SMTP_STATE_DATA:
	    vstring_strcpy(next_command, "DATA");
	    next_state = SMTP_STATE_DOT;
	    break;

	    /*
	     * Build the "." command before we have seen the DATA response.
	     */
	case SMTP_STATE_DOT:
	    vstring_strcpy(next_command, ".");
	    next_state = THIS_SESSION_IS_CACHED ?
		SMTP_STATE_LAST : SMTP_STATE_QUIT;
	    break;

	    /*
	     * The SMTP_STATE_ABORT sender state is entered by the sender
	     * when it has verified all recipients; or it is entered by the
	     * receiver when all recipients are verified or rejected, and is
	     * then left before the bottom of the main loop.
	     */
	case SMTP_STATE_ABORT:
	    vstring_strcpy(next_command, "RSET");
	    next_state = THIS_SESSION_IS_CACHED ?
		SMTP_STATE_LAST : SMTP_STATE_QUIT;
	    break;

	    /*
	     * Build the RSET command. This is entered as initial state from
	     * smtp_rset() and has its own dedicated state transitions. It is
	     * used to find out the status of a cached session before
	     * attempting mail delivery.
	     */
	case SMTP_STATE_RSET:
	    vstring_strcpy(next_command, "RSET");
	    next_state = SMTP_STATE_LAST;
	    break;

	    /*
	     * Build the QUIT command before we have seen the "." or RSET
	     * response. This is entered as initial state from smtp_quit(),
	     * or is reached near the end of any non-cached session.
	     */
	case SMTP_STATE_QUIT:
	    vstring_strcpy(next_command, "QUIT");
	    next_state = SMTP_STATE_LAST;
	    DONT_CACHE_THIS_SESSION;
	    break;

	    /*
	     * The final sender state has no action associated with it.
	     */
	case SMTP_STATE_LAST:
	    VSTRING_RESET(next_command);
	    break;
	}
	VSTRING_TERMINATE(next_command);

	/*
	 * Process responses until the receiver has caught up. Vstreams
	 * automatically flush buffered output when reading new data.
	 * 
	 * Flush unsent output if command pipelining is off or if no I/O
	 * happened for a while. This limits the accumulation of client-side
	 * delays in pipelined sessions.
	 */
	if (SENDER_IN_WAIT_STATE
	    || (SENDER_IS_AHEAD
		&& (VSTRING_LEN(next_command) + 2 > sndbuffree
	    || time((time_t *) 0) - vstream_ftime(session->stream) > 10))) {
	    while (SENDER_IS_AHEAD) {

		/*
		 * Sanity check.
		 */
		if (recv_state < SMTP_STATE_XFORWARD_NAME_ADDR
		    || recv_state > SMTP_STATE_QUIT)
		    msg_panic("%s: bad receiver state %d (sender state %d)",
			      myname, recv_state, send_state);

		/*
		 * Receive the next server response. Use the proper timeout,
		 * and log the proper client state in case of trouble.
		 */
		smtp_timeout_setup(session->stream,
				   *xfer_timeouts[recv_state]);
		if ((except = vstream_setjmp(session->stream)) != 0)
		    RETURN(SENDING_MAIL ? smtp_stream_except(state, except,
					     xfer_states[recv_state]) : -1);
		resp = smtp_chat_resp(session);

		/*
		 * Process the response.
		 */
		switch (recv_state) {

		    /*
		     * Process the XFORWARD response.
		     */
		case SMTP_STATE_XFORWARD_NAME_ADDR:
		    if (resp->code / 100 != 2)
			msg_warn("host %s said: %s (in reply to %s)",
				 session->namaddr,
				 translit(resp->str, "\n", " "),
			       xfer_request[SMTP_STATE_XFORWARD_NAME_ADDR]);
		    if (session->send_proto_helo)
			recv_state = SMTP_STATE_XFORWARD_PROTO_HELO;
		    else
			recv_state = SMTP_STATE_MAIL;
		    break;

		case SMTP_STATE_XFORWARD_PROTO_HELO:
		    if (resp->code / 100 != 2)
			msg_warn("host %s said: %s (in reply to %s)",
				 session->namaddr,
				 translit(resp->str, "\n", " "),
			      xfer_request[SMTP_STATE_XFORWARD_PROTO_HELO]);
		    recv_state = SMTP_STATE_MAIL;
		    break;

		    /*
		     * Process the MAIL FROM response. When the server
		     * rejects the sender, set the mail_from_rejected flag so
		     * that the receiver may apply a course correction.
		     */
		case SMTP_STATE_MAIL:
		    if (resp->code / 100 != 2) {
			smtp_mesg_fail(state, resp->code,
				       "host %s said: %s (in reply to %s)",
				       session->namaddr,
				       translit(resp->str, "\n", " "),
				       xfer_request[SMTP_STATE_MAIL]);
			mail_from_rejected = 1;
		    }
		    recv_state = SMTP_STATE_RCPT;
		    break;

		    /*
		     * Process one RCPT TO response. If MAIL FROM was
		     * rejected, ignore RCPT TO responses: all recipients are
		     * dead already. When all recipients are rejected the
		     * receiver may apply a course correction.
		     * 
		     * XXX 2821: Section 4.5.3.1 says that a 552 RCPT TO reply
		     * must be treated as if the server replied with 452.
		     * However, this causes "too much mail data" to be
		     * treated as a recoverable error, which is wrong. I'll
		     * stick with RFC 821.
		     */
		case SMTP_STATE_RCPT:
		    if (!mail_from_rejected) {
#ifdef notdef
			if (resp->code == 552)
			    resp->code = 452;
#endif
			rcpt = request->rcpt_list.info + recv_rcpt;
			if (resp->code / 100 == 2) {
			    ++nrcpt;
			    /* If trace-only, mark the recipient done. */
			    if (DEL_REQ_TRACE_ONLY(request->flags))
				smtp_rcpt_done(state, resp->str, rcpt);
			} else {
			    smtp_rcpt_fail(state, resp->code, rcpt,
					"host %s said: %s (in reply to %s)",
					   session->namaddr,
					   translit(resp->str, "\n", " "),
					   xfer_request[SMTP_STATE_RCPT]);
			}
		    }
		    /* If trace-only, send RSET instead of DATA. */
		    if (++recv_rcpt == SMTP_RCPT_LEFT(state))
			recv_state = DEL_REQ_TRACE_ONLY(request->flags) ?
			    SMTP_STATE_ABORT : SMTP_STATE_DATA;
		    /* XXX Also: record if non-delivering session. */
		    break;

		    /*
		     * Process the DATA response. When the server rejects
		     * DATA, set nrcpt to a negative value so that the
		     * receiver can apply a course correction.
		     */
		case SMTP_STATE_DATA:
		    if (resp->code / 100 != 3) {
			if (nrcpt > 0)
			    smtp_mesg_fail(state, resp->code,
					"host %s said: %s (in reply to %s)",
					   session->namaddr,
					   translit(resp->str, "\n", " "),
					   xfer_request[SMTP_STATE_DATA]);
			nrcpt = -1;
		    }
		    recv_state = SMTP_STATE_DOT;
		    break;

		    /*
		     * Process the end of message response. Ignore the
		     * response when no recipient was accepted: all
		     * recipients are dead already, and the next receiver
		     * state is SMTP_STATE_LAST/QUIT regardless. Otherwise,
		     * if the message transfer fails, bounce all remaining
		     * recipients, else cross off the recipients that were
		     * delivered.
		     */
		case SMTP_STATE_DOT:
		    if (nrcpt > 0) {
			if (resp->code / 100 != 2) {
			    smtp_mesg_fail(state, resp->code,
					"host %s said: %s (in reply to %s)",
					   session->namaddr,
					   translit(resp->str, "\n", " "),
					   xfer_request[SMTP_STATE_DOT]);
			} else {
			    for (nrcpt = 0; nrcpt < recv_rcpt; nrcpt++) {
				rcpt = request->rcpt_list.info + nrcpt;
				if (!SMTP_RCPT_ISMARKED(rcpt))
				    smtp_rcpt_done(state, resp->str, rcpt);
			    }
			}
		    }
		    recv_state = (var_skip_quit_resp || THIS_SESSION_IS_CACHED ?
				  SMTP_STATE_LAST : SMTP_STATE_QUIT);
		    break;

		    /*
		     * Receive the RSET response, and disable session caching
		     * in case of failure.
		     */
		case SMTP_STATE_ABORT:
		    if (resp->code / 100 != 2)
			DONT_CACHE_THIS_SESSION;
		    recv_state = (var_skip_quit_resp || THIS_SESSION_IS_CACHED ?
				  SMTP_STATE_LAST : SMTP_STATE_QUIT);
		    break;

		    /*
		     * This is the initial receiver state from smtp_rset().
		     * It is used to find out the status of a cached session
		     * before attempting mail delivery.
		     */
		case SMTP_STATE_RSET:
		    if (resp->code / 100 != 2)
			CANT_RSET_THIS_SESSION;
		    recv_state = SMTP_STATE_LAST;
		    break;

		    /*
		     * Receive, but otherwise ignore, the QUIT response.
		     */
		case SMTP_STATE_QUIT:
		    recv_state = SMTP_STATE_LAST;
		    break;
		}
	    }

	    /*
	     * At this point, the sender and receiver are fully synchronized,
	     * so that the entire TCP send buffer becomes available again.
	     */
	    sndbuffree = session->sndbufsize;

	    /*
	     * We know the server response to every command that was sent.
	     * Apply a course correction if necessary: the sender wants to
	     * send RCPT TO but MAIL FROM was rejected; the sender wants to
	     * send DATA but all recipients were rejected; the sender wants
	     * to deliver the message but DATA was rejected.
	     */
	    if ((send_state == SMTP_STATE_RCPT && mail_from_rejected)
		|| (send_state == SMTP_STATE_DATA && nrcpt == 0)
		|| (send_state == SMTP_STATE_DOT && nrcpt < 0)) {
		send_state = recv_state = SMTP_STATE_ABORT;
		send_rcpt = recv_rcpt = 0;
		vstring_strcpy(next_command, "RSET");
		next_state = THIS_SESSION_IS_CACHED ?
		    SMTP_STATE_LAST : SMTP_STATE_QUIT;
		/* XXX Also: record if non-delivering session. */
		next_rcpt = 0;
	    }
	}

	/*
	 * Make the next sender state the current sender state.
	 */
	if (send_state == SMTP_STATE_LAST)
	    continue;

	/*
	 * Special case if the server accepted the DATA command. If the
	 * server accepted at least one recipient send the entire message.
	 * Otherwise, just send "." as per RFC 2197.
	 * 
	 * XXX If there is a hard MIME error while downgrading to 7-bit mail,
	 * disconnect ungracefully, because there is no other way to cancel a
	 * transaction in progress.
	 */
	if (send_state == SMTP_STATE_DOT && nrcpt > 0) {
	    downgrading =
		(var_disable_mime_oconv == 0
		 && (session->features & SMTP_FEATURE_8BITMIME) == 0
		 && strcmp(request->encoding, MAIL_ATTR_ENC_7BIT) != 0);
	    if (downgrading)
		session->mime_state = mime_state_alloc(MIME_OPT_DOWNGRADE
						  | MIME_OPT_REPORT_NESTING,
						       smtp_header_out,
						     (MIME_STATE_ANY_END) 0,
						       smtp_text_out,
						     (MIME_STATE_ANY_END) 0,
						   (MIME_STATE_ERR_PRINT) 0,
						       (void *) state);
	    state->space_left = var_smtp_line_limit;
	    smtp_timeout_setup(session->stream,
			       var_smtp_data1_tmout);
	    if ((except = vstream_setjmp(session->stream)) != 0)
		RETURN(smtp_stream_except(state, except,
					  "sending message body"));

	    if (vstream_fseek(state->src, request->data_offset, SEEK_SET) < 0)
		msg_fatal("seek queue file: %m");

	    while ((rec_type = rec_get(state->src, session->scratch, 0)) > 0) {
		if (rec_type != REC_TYPE_NORM && rec_type != REC_TYPE_CONT)
		    break;
		if (downgrading == 0) {
		    smtp_text_out((void *) state, rec_type,
				  vstring_str(session->scratch),
				  VSTRING_LEN(session->scratch),
				  (off_t) 0);
		} else {
		    mime_errs =
			mime_state_update(session->mime_state, rec_type,
					  vstring_str(session->scratch),
					  VSTRING_LEN(session->scratch));
		    if (mime_errs) {
			smtp_mesg_fail(state, 554,
				       "MIME 7-bit conversion failed: %s",
				       mime_state_error(mime_errs));
			RETURN(0);
		    }
		}
		prev_type = rec_type;
	    }

	    if (session->mime_state) {

		/*
		 * The cleanup server normally ends MIME content with a
		 * normal text record. The following code is needed to flush
		 * an internal buffer when someone submits 8-bit mail not
		 * ending in newline via /usr/sbin/sendmail while MIME input
		 * processing is turned off, and MIME 8bit->7bit conversion
		 * is requested upon delivery.
		 */
		mime_errs =
		    mime_state_update(session->mime_state, rec_type, "", 0);
		if (mime_errs) {
		    smtp_mesg_fail(state, 554,
				   "MIME 7-bit conversion failed: %s",
				   mime_state_error(mime_errs));
		    RETURN(0);
		}
	    } else if (prev_type == REC_TYPE_CONT)	/* missing newline */
		smtp_fputs("", 0, session->stream);
	    if ((session->features & SMTP_FEATURE_MAYBEPIX) != 0
		&& request->arrival_time < vstream_ftime(session->stream)
		- var_smtp_pix_thresh) {
		msg_info("%s: enabling PIX <CRLF>.<CRLF> workaround for %s",
			 request->queue_id, session->namaddr);
		vstream_fflush(session->stream);/* hurts performance */
		sleep(var_smtp_pix_delay);	/* not to mention this */
	    }
	    if (vstream_ferror(state->src))
		msg_fatal("queue file read error");
	    if (rec_type != REC_TYPE_XTRA)
		RETURN(mark_corrupt(state->src));
	}

	/*
	 * Copy the next command to the buffer and update the sender state.
	 */
	if (sndbuffree > 0)
	    sndbuffree -= VSTRING_LEN(next_command) + 2;
	smtp_chat_cmd(session, "%s", vstring_str(next_command));
	send_state = next_state;
	send_rcpt = next_rcpt;
    } while (recv_state != SMTP_STATE_LAST);
    RETURN(0);
}

/* smtp_xfer - send a batch of envelope information and the message data */

int     smtp_xfer(SMTP_STATE *state)
{
    DELIVER_REQUEST *request = state->request;
    SMTP_SESSION *session = state->session;
    int     send_state;
    int     recv_state;
    int     send_name_addr;

    /*
     * Sanity check. Recipients should be unmarked at this point.
     */
    if (SMTP_RCPT_LEFT(state) <= 0)
	msg_panic("smtp_xfer: bad recipient count: %d",
		  SMTP_RCPT_LEFT(state));
    if (SMTP_RCPT_ISMARKED(request->rcpt_list.info))
	msg_panic("smtp_xfer: bad recipient status: %d",
		  request->rcpt_list.info->status);

    /*
     * See if we should even try to send this message at all. This code sits
     * here rather than in the EHLO processing code, because of SMTP
     * connection caching.
     */
    if (session->size_limit > 0 && session->size_limit < request->data_size) {
	smtp_mesg_fail(state, 552,
		    "message size %lu exceeds size limit %.0f of server %s",
		       request->data_size, (double) session->size_limit,
		       session->namaddr);
	return (0);
    }

    /*
     * Use the XFORWARD command to forward client attributes only when a
     * minimal amount of information is available.
     */
    send_name_addr =
	var_smtp_send_xforward
	&& (((session->features & SMTP_FEATURE_XFORWARD_NAME)
	     && DEL_REQ_ATTR_AVAIL(request->client_name))
	    || ((session->features & SMTP_FEATURE_XFORWARD_ADDR)
		&& DEL_REQ_ATTR_AVAIL(request->client_addr)));
    session->send_proto_helo =
	var_smtp_send_xforward
	&& (((session->features & SMTP_FEATURE_XFORWARD_PROTO)
	     && DEL_REQ_ATTR_AVAIL(request->client_proto))
	    || ((session->features & SMTP_FEATURE_XFORWARD_HELO)
		&& DEL_REQ_ATTR_AVAIL(request->client_helo)));
    if (send_name_addr)
	recv_state = send_state = SMTP_STATE_XFORWARD_NAME_ADDR;
    else if (session->send_proto_helo)
	recv_state = send_state = SMTP_STATE_XFORWARD_PROTO_HELO;
    else
	recv_state = send_state = SMTP_STATE_MAIL;

    return (smtp_loop(state, send_state, recv_state));
}

/* smtp_rset - send a lone RSET command */

int     smtp_rset(SMTP_STATE *state)
{

    /*
     * This works because SMTP_STATE_RSET is a dedicated sender/recipient
     * entry state, with SMTP_STATE_LAST as next sender/recipient state.
     */
    return (smtp_loop(state, SMTP_STATE_RSET, SMTP_STATE_RSET));
}

/* smtp_quit - send a lone QUIT command */

int     smtp_quit(SMTP_STATE *state)
{

    /*
     * This works because SMTP_STATE_QUIT is the last state with a sender
     * action, with SMTP_STATE_LAST as the next sender/recipient state.
     */
    return (smtp_loop(state, SMTP_STATE_QUIT, var_skip_quit_resp ?
		      SMTP_STATE_LAST : SMTP_STATE_QUIT));
}
