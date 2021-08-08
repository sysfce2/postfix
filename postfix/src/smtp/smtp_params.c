    static const CONFIG_STR_TABLE smtp_str_table[] = {
	VAR_NOTIFY_CLASSES, DEF_NOTIFY_CLASSES, &var_notify_classes, 0, 0,
	VAR_SMTP_FALLBACK, DEF_SMTP_FALLBACK, &var_fallback_relay, 0, 0,
	VAR_BESTMX_TRANSP, DEF_BESTMX_TRANSP, &var_bestmx_transp, 0, 0,
	VAR_ERROR_RCPT, DEF_ERROR_RCPT, &var_error_rcpt, 1, 0,
	VAR_SMTP_SASL_PASSWD, DEF_SMTP_SASL_PASSWD, &var_smtp_sasl_passwd, 0, 0,
	VAR_SMTP_SASL_OPTS, DEF_SMTP_SASL_OPTS, &var_smtp_sasl_opts, 0, 0,
	VAR_SMTP_SASL_PATH, DEF_SMTP_SASL_PATH, &var_smtp_sasl_path, 0, 0,
#ifdef USE_TLS
	VAR_SMTP_SASL_TLS_OPTS, DEF_SMTP_SASL_TLS_OPTS, &var_smtp_sasl_tls_opts, 0, 0,
	VAR_SMTP_SASL_TLSV_OPTS, DEF_SMTP_SASL_TLSV_OPTS, &var_smtp_sasl_tlsv_opts, 0, 0,
	VAR_SMTP_TLS_CHAIN_FILES, DEF_SMTP_TLS_CHAIN_FILES, &var_smtp_tls_chain_files, 0, 0,
	VAR_SMTP_TLS_CERT_FILE, DEF_SMTP_TLS_CERT_FILE, &var_smtp_tls_cert_file, 0, 0,
	VAR_SMTP_TLS_KEY_FILE, DEF_SMTP_TLS_KEY_FILE, &var_smtp_tls_key_file, 0, 0,
	VAR_SMTP_TLS_DCERT_FILE, DEF_SMTP_TLS_DCERT_FILE, &var_smtp_tls_dcert_file, 0, 0,
	VAR_SMTP_TLS_DKEY_FILE, DEF_SMTP_TLS_DKEY_FILE, &var_smtp_tls_dkey_file, 0, 0,
	VAR_SMTP_TLS_CA_FILE, DEF_SMTP_TLS_CA_FILE, &var_smtp_tls_CAfile, 0, 0,
	VAR_SMTP_TLS_CA_PATH, DEF_SMTP_TLS_CA_PATH, &var_smtp_tls_CApath, 0, 0,
	VAR_SMTP_TLS_MAND_CIPH, DEF_SMTP_TLS_MAND_CIPH, &var_smtp_tls_mand_ciph, 1, 0,
	VAR_SMTP_TLS_EXCL_CIPH, DEF_SMTP_TLS_EXCL_CIPH, &var_smtp_tls_excl_ciph, 0, 0,
	VAR_SMTP_TLS_MAND_EXCL, DEF_SMTP_TLS_MAND_EXCL, &var_smtp_tls_mand_excl, 0, 0,
	VAR_SMTP_TLS_MAND_PROTO, DEF_SMTP_TLS_MAND_PROTO, &var_smtp_tls_mand_proto, 0, 0,
	VAR_SMTP_TLS_VFY_CMATCH, DEF_SMTP_TLS_VFY_CMATCH, &var_smtp_tls_vfy_cmatch, 1, 0,
	VAR_SMTP_TLS_SEC_CMATCH, DEF_SMTP_TLS_SEC_CMATCH, &var_smtp_tls_sec_cmatch, 1, 0,
	VAR_SMTP_TLS_FPT_CMATCH, DEF_SMTP_TLS_FPT_CMATCH, &var_smtp_tls_fpt_cmatch, 0, 0,
	VAR_SMTP_TLS_FPT_DGST, DEF_SMTP_TLS_FPT_DGST, &var_smtp_tls_fpt_dgst, 1, 0,
	VAR_SMTP_TLS_TAFILE, DEF_SMTP_TLS_TAFILE, &var_smtp_tls_tafile, 0, 0,
	VAR_SMTP_TLS_PROTO, DEF_SMTP_TLS_PROTO, &var_smtp_tls_proto, 0, 0,
	VAR_SMTP_TLS_CIPH, DEF_SMTP_TLS_CIPH, &var_smtp_tls_ciph, 1, 0,
	VAR_SMTP_TLS_ECCERT_FILE, DEF_SMTP_TLS_ECCERT_FILE, &var_smtp_tls_eccert_file, 0, 0,
	VAR_SMTP_TLS_ECKEY_FILE, DEF_SMTP_TLS_ECKEY_FILE, &var_smtp_tls_eckey_file, 0, 0,
	VAR_SMTP_TLS_LOGLEVEL, DEF_SMTP_TLS_LOGLEVEL, &var_smtp_tls_loglevel, 0, 0,
	VAR_SMTP_TLS_SNI, DEF_SMTP_TLS_SNI, &var_smtp_tls_sni, 0, 0,
	VAR_SMTP_TLS_INSECURE_MX_POLICY, DEF_SMTP_TLS_INSECURE_MX_POLICY, &var_smtp_tls_insecure_mx_policy, 0, 0,
#endif
	VAR_SMTP_SASL_MECHS, DEF_SMTP_SASL_MECHS, &var_smtp_sasl_mechs, 0, 0,
	VAR_SMTP_SASL_TYPE, DEF_SMTP_SASL_TYPE, &var_smtp_sasl_type, 1, 0,
	VAR_SMTP_BIND_ADDR, DEF_SMTP_BIND_ADDR, &var_smtp_bind_addr, 0, 0,
	VAR_SMTP_BIND_ADDR6, DEF_SMTP_BIND_ADDR6, &var_smtp_bind_addr6, 0, 0,
	VAR_SMTP_VRFY_TGT, DEF_SMTP_VRFY_TGT, &var_smtp_vrfy_tgt, 1, 0,
	VAR_SMTP_HELO_NAME, DEF_SMTP_HELO_NAME, &var_smtp_helo_name, 1, 0,
	VAR_SMTP_HOST_LOOKUP, DEF_SMTP_HOST_LOOKUP, &var_smtp_host_lookup, 1, 0,
	VAR_SMTP_DNS_SUPPORT, DEF_SMTP_DNS_SUPPORT, &var_smtp_dns_support, 0, 0,
	VAR_SMTP_CACHE_DEST, DEF_SMTP_CACHE_DEST, &var_smtp_cache_dest, 0, 0,
	VAR_SCACHE_SERVICE, DEF_SCACHE_SERVICE, &var_scache_service, 1, 0,
	VAR_SMTP_EHLO_DIS_WORDS, DEF_SMTP_EHLO_DIS_WORDS, &var_smtp_ehlo_dis_words, 0, 0,
	VAR_SMTP_EHLO_DIS_MAPS, DEF_SMTP_EHLO_DIS_MAPS, &var_smtp_ehlo_dis_maps, 0, 0,
	VAR_SMTP_TLS_PER_SITE, DEF_SMTP_TLS_PER_SITE, &var_smtp_tls_per_site, 0, 0,
	VAR_SMTP_TLS_LEVEL, DEF_SMTP_TLS_LEVEL, &var_smtp_tls_level, 0, 0,
	VAR_SMTP_TLS_POLICY, DEF_SMTP_TLS_POLICY, &var_smtp_tls_policy, 0, 0,
	VAR_PROP_EXTENSION, DEF_PROP_EXTENSION, &var_prop_extension, 0, 0,
	VAR_SMTP_GENERIC_MAPS, DEF_SMTP_GENERIC_MAPS, &var_smtp_generic_maps, 0, 0,
	VAR_SMTP_TCP_PORT, DEF_SMTP_TCP_PORT, &var_smtp_tcp_port, 0, 0,
	VAR_SMTP_PIX_BUG_WORDS, DEF_SMTP_PIX_BUG_WORDS, &var_smtp_pix_bug_words, 0, 0,
	VAR_SMTP_PIX_BUG_MAPS, DEF_SMTP_PIX_BUG_MAPS, &var_smtp_pix_bug_maps, 0, 0,
	VAR_SMTP_SASL_AUTH_CACHE_NAME, DEF_SMTP_SASL_AUTH_CACHE_NAME, &var_smtp_sasl_auth_cache_name, 0, 0,
	VAR_CYRUS_CONF_PATH, DEF_CYRUS_CONF_PATH, &var_cyrus_conf_path, 0, 0,
	VAR_SMTP_HEAD_CHKS, DEF_SMTP_HEAD_CHKS, &var_smtp_head_chks, 0, 0,
	VAR_SMTP_MIME_CHKS, DEF_SMTP_MIME_CHKS, &var_smtp_mime_chks, 0, 0,
	VAR_SMTP_NEST_CHKS, DEF_SMTP_NEST_CHKS, &var_smtp_nest_chks, 0, 0,
	VAR_SMTP_BODY_CHKS, DEF_SMTP_BODY_CHKS, &var_smtp_body_chks, 0, 0,
	VAR_SMTP_RESP_FILTER, DEF_SMTP_RESP_FILTER, &var_smtp_resp_filter, 0, 0,
	VAR_SMTP_ADDR_PREF, DEF_SMTP_ADDR_PREF, &var_smtp_addr_pref, 1, 0,
	VAR_SMTP_DNS_RES_OPT, DEF_SMTP_DNS_RES_OPT, &var_smtp_dns_res_opt, 0, 0,
	VAR_SMTP_DSN_FILTER, DEF_SMTP_DSN_FILTER, &var_smtp_dsn_filter, 0, 0,
	VAR_SMTP_DNS_RE_FILTER, DEF_SMTP_DNS_RE_FILTER, &var_smtp_dns_re_filter, 0, 0,
	VAR_TLSPROXY_SERVICE, DEF_TLSPROXY_SERVICE, &var_tlsproxy_service, 1, 0,
	0,
    };
    static const CONFIG_TIME_TABLE smtp_time_table[] = {
	VAR_SMTP_CONN_TMOUT, DEF_SMTP_CONN_TMOUT, &var_smtp_conn_tmout, 0, 0,
	VAR_SMTP_HELO_TMOUT, DEF_SMTP_HELO_TMOUT, &var_smtp_helo_tmout, 1, 0,
	VAR_SMTP_XFWD_TMOUT, DEF_SMTP_XFWD_TMOUT, &var_smtp_xfwd_tmout, 1, 0,
	VAR_SMTP_MAIL_TMOUT, DEF_SMTP_MAIL_TMOUT, &var_smtp_mail_tmout, 1, 0,
	VAR_SMTP_RCPT_TMOUT, DEF_SMTP_RCPT_TMOUT, &var_smtp_rcpt_tmout, 1, 0,
	VAR_SMTP_DATA0_TMOUT, DEF_SMTP_DATA0_TMOUT, &var_smtp_data0_tmout, 1, 0,
	VAR_SMTP_DATA1_TMOUT, DEF_SMTP_DATA1_TMOUT, &var_smtp_data1_tmout, 1, 0,
	VAR_SMTP_DATA2_TMOUT, DEF_SMTP_DATA2_TMOUT, &var_smtp_data2_tmout, 1, 0,
	VAR_SMTP_RSET_TMOUT, DEF_SMTP_RSET_TMOUT, &var_smtp_rset_tmout, 1, 0,
	VAR_SMTP_QUIT_TMOUT, DEF_SMTP_QUIT_TMOUT, &var_smtp_quit_tmout, 1, 0,
	VAR_SMTP_PIX_THRESH, DEF_SMTP_PIX_THRESH, &var_smtp_pix_thresh, 0, 0,
	VAR_SMTP_PIX_DELAY, DEF_SMTP_PIX_DELAY, &var_smtp_pix_delay, 1, 0,
	VAR_QUEUE_RUN_DELAY, DEF_QUEUE_RUN_DELAY, &var_queue_run_delay, 1, 0,
	VAR_MIN_BACKOFF_TIME, DEF_MIN_BACKOFF_TIME, &var_min_backoff_time, 1, 0,
	VAR_SMTP_CACHE_CONNT, DEF_SMTP_CACHE_CONNT, &var_smtp_cache_conn, 1, 0,
	VAR_SMTP_REUSE_TIME, DEF_SMTP_REUSE_TIME, &var_smtp_reuse_time, 1, 0,
#ifdef USE_TLS
	VAR_SMTP_STARTTLS_TMOUT, DEF_SMTP_STARTTLS_TMOUT, &var_smtp_starttls_tmout, 1, 0,
#endif
	VAR_SCACHE_PROTO_TMOUT, DEF_SCACHE_PROTO_TMOUT, &var_scache_proto_tmout, 1, 0,
	VAR_SMTP_SASL_AUTH_CACHE_TIME, DEF_SMTP_SASL_AUTH_CACHE_TIME, &var_smtp_sasl_auth_cache_time, 0, 0,
	0,
    };
    static const CONFIG_INT_TABLE smtp_int_table[] = {
	VAR_SMTP_LINE_LIMIT, DEF_SMTP_LINE_LIMIT, &var_smtp_line_limit, 0, 0,
	VAR_SMTP_MXADDR_LIMIT, DEF_SMTP_MXADDR_LIMIT, &var_smtp_mxaddr_limit, 0, 0,
	VAR_SMTP_MXSESS_LIMIT, DEF_SMTP_MXSESS_LIMIT, &var_smtp_mxsess_limit, 0, 0,
	VAR_SMTP_REUSE_COUNT, DEF_SMTP_REUSE_COUNT, &var_smtp_reuse_count, 0, 0,
#ifdef USE_TLS
	VAR_SMTP_TLS_SCERT_VD, DEF_SMTP_TLS_SCERT_VD, &var_smtp_tls_scert_vd, 0, 0,
#endif
	VAR_SMTP_MIN_DATA_RATE, DEF_SMTP_MIN_DATA_RATE, &var_smtp_min_data_rate, 1, 0,
	0,
    };
    static const CONFIG_BOOL_TABLE smtp_bool_table[] = {
	VAR_SMTP_SKIP_5XX, DEF_SMTP_SKIP_5XX, &var_smtp_skip_5xx_greeting,
	VAR_IGN_MX_LOOKUP_ERR, DEF_IGN_MX_LOOKUP_ERR, &var_ign_mx_lookup_err,
	VAR_SMTP_SKIP_QUIT_RESP, DEF_SMTP_SKIP_QUIT_RESP, &var_skip_quit_resp,
	VAR_SMTP_ALWAYS_EHLO, DEF_SMTP_ALWAYS_EHLO, &var_smtp_always_ehlo,
	VAR_SMTP_NEVER_EHLO, DEF_SMTP_NEVER_EHLO, &var_smtp_never_ehlo,
	VAR_SMTP_SASL_ENABLE, DEF_SMTP_SASL_ENABLE, &var_smtp_sasl_enable,
	VAR_SMTP_RAND_ADDR, DEF_SMTP_RAND_ADDR, &var_smtp_rand_addr,
	VAR_SMTP_QUOTE_821_ENV, DEF_SMTP_QUOTE_821_ENV, &var_smtp_quote_821_env,
	VAR_SMTP_DEFER_MXADDR, DEF_SMTP_DEFER_MXADDR, &var_smtp_defer_mxaddr,
	VAR_SMTP_SEND_XFORWARD, DEF_SMTP_SEND_XFORWARD, &var_smtp_send_xforward,
	VAR_SMTP_CACHE_DEMAND, DEF_SMTP_CACHE_DEMAND, &var_smtp_cache_demand,
	VAR_SMTP_USE_TLS, DEF_SMTP_USE_TLS, &var_smtp_use_tls,
	VAR_SMTP_ENFORCE_TLS, DEF_SMTP_ENFORCE_TLS, &var_smtp_enforce_tls,
	VAR_SMTP_TLS_CONN_REUSE, DEF_SMTP_TLS_CONN_REUSE, &var_smtp_tls_conn_reuse,
#ifdef USE_TLS
	VAR_SMTP_TLS_ENFORCE_PN, DEF_SMTP_TLS_ENFORCE_PN, &var_smtp_tls_enforce_peername,
	VAR_SMTP_TLS_NOTEOFFER, DEF_SMTP_TLS_NOTEOFFER, &var_smtp_tls_note_starttls_offer,
	VAR_SMTP_TLS_BLK_EARLY_MAIL_REPLY, DEF_SMTP_TLS_BLK_EARLY_MAIL_REPLY, &var_smtp_tls_blk_early_mail_reply,
	VAR_SMTP_TLS_FORCE_TLSA, DEF_SMTP_TLS_FORCE_TLSA, &var_smtp_tls_force_tlsa,
#endif
	VAR_SMTP_TLS_WRAPPER, DEF_SMTP_TLS_WRAPPER, &var_smtp_tls_wrappermode,
	VAR_SMTP_SENDER_AUTH, DEF_SMTP_SENDER_AUTH, &var_smtp_sender_auth,
	VAR_SMTP_CNAME_OVERR, DEF_SMTP_CNAME_OVERR, &var_smtp_cname_overr,
	VAR_SMTP_SASL_AUTH_SOFT_BOUNCE, DEF_SMTP_SASL_AUTH_SOFT_BOUNCE, &var_smtp_sasl_auth_soft_bounce,
	VAR_LMTP_ASSUME_FINAL, DEF_LMTP_ASSUME_FINAL, &var_lmtp_assume_final,
	VAR_SMTP_DUMMY_MAIL_AUTH, DEF_SMTP_DUMMY_MAIL_AUTH, &var_smtp_dummy_mail_auth,
	VAR_SMTP_BALANCE_INET_PROTO, DEF_SMTP_BALANCE_INET_PROTO, &var_smtp_balance_inet_proto,
	VAR_SMTP_REQ_DEADLINE, DEF_SMTP_REQ_DEADLINE, &var_smtp_req_deadline,
	0,
    };
