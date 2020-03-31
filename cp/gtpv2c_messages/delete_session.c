/* SPDX-License-Identifier: Apache-2.0
 * Copyright(c) 2017 Intel Corporation
 */

#include <rte_debug.h>

#include "gtp_messages.h"
#include "../cp_dp_api/vepc_cp_dp_api.h"
#include "gtpv2c_set_ie.h"
#include "sm_struct.h"

#ifdef C3PO_OSS
#include "cp_config.h"
#include "cp_stats.h"
#endif /* C3PO_OSS */

pfcp_config_t pfcp_config;

int
delete_context(del_sess_req_t *ds_req,
			ue_context **_context, uint32_t *s5s8_pgw_gtpc_teid,
			uint32_t *s5s8_pgw_gtpc_ipv4);

/**
 * Handles the removal of data structures internal to the control plane
 * as well as notifying the data plane of such changes.
 * @param ds_req
 *   structure containing create delete session request
 * @param _context
 *   returns the UE context structure pertaining to the session to be deleted
 * @return
 *   \- 0 if successful
 *   \- > 0 if error occurs during packet filter parsing corresponds to 3gpp
 *   specified cause error value
 *   \- < 0 for all other errors
 */
int
delete_context(del_sess_req_t *ds_req,
			ue_context **_context, uint32_t *s5s8_pgw_gtpc_teid,
			uint32_t *s5s8_pgw_gtpc_ipv4)
{
	int ret;
	int i;
	ue_context *context = NULL;

	ret = rte_hash_lookup_data(ue_context_by_fteid_hash,
	    (const void *) &ds_req->header.teid.has_teid.teid,
	    (void **) &context);

	if (ret < 0 || !context)
		return GTPV2C_CAUSE_CONTEXT_NOT_FOUND;


	if (!ds_req->lbi.header.len) {
		/* TODO: should be responding with response indicating error
		 * in request */
		fprintf(stderr, "Received delete session without ebi! - "
				"dropping\n");
		return GTPV2C_CAUSE_INVALID_MESSAGE_FORMAT;
	}

	uint8_t ebi_index = ds_req->lbi.ebi_ebi - 5;
	if (!(context->bearer_bitmap & (1 << ebi_index))) {
		fprintf(stderr,
		    "Received delete session on non-existent EBI - "
		    "Dropping packet\n");
		fprintf(stderr, "ebi %u\n", ds_req->lbi.ebi_ebi);
		fprintf(stderr, "ebi_index %u\n", ebi_index);
		fprintf(stderr, "bearer_bitmap %04x\n", context->bearer_bitmap);
		fprintf(stderr, "mask %04x\n", (1 << ebi_index));
		return GTPV2C_CAUSE_INVALID_MESSAGE_FORMAT;
	}

	pdn_connection *pdn = context->eps_bearers[ebi_index]->pdn;
	if (!pdn) {
		fprintf(stderr, "Received delete session on "
				"non-existent EBI\n");
		return GTPV2C_CAUSE_MANDATORY_IE_INCORRECT;
	}

	if (pdn->default_bearer_id != ds_req->lbi.ebi_ebi) {
		fprintf(stderr,
		    "Received delete session referencing incorrect "
		    "default bearer ebi");
		return GTPV2C_CAUSE_MANDATORY_IE_INCORRECT;
	}

	eps_bearer *bearer = context->eps_bearers[ebi_index];
	if (!bearer) {
		fprintf(stderr, "Received delete session on non-existent "
				"default EBI\n");
		return GTPV2C_CAUSE_MANDATORY_IE_INCORRECT;
	}

	if (pfcp_config.cp_type == SGWC) {
		/*VS: Fill teid and ip address */
		*s5s8_pgw_gtpc_teid = htonl(pdn->s5s8_pgw_gtpc_teid);
		*s5s8_pgw_gtpc_ipv4 = htonl(pdn->s5s8_pgw_gtpc_ipv4.s_addr);

		clLog(s5s8logger, eCLSeverityDebug, "s5s8_pgw_gtpc_teid:%u, s5s8_pgw_gtpc_ipv4:%u\n",
				*s5s8_pgw_gtpc_teid, *s5s8_pgw_gtpc_ipv4);
	}

	for (i = 0; i < MAX_BEARERS; ++i) {
		if (pdn->eps_bearers[i] == NULL)
			continue;

		if (context->eps_bearers[i] == pdn->eps_bearers[i]) {
			bearer = context->eps_bearers[i];
			struct session_info si;
			memset(&si, 0, sizeof(si));

			/**
			 * ebi and s1u_sgw_teid is set here for zmq/sdn
			 */
			si.bearer_id = ds_req->lbi.ebi_ebi;
			si.ue_addr.u.ipv4_addr =
				htonl(pdn->ipv4.s_addr);
			si.ul_s1_info.sgw_teid =
				bearer->s1u_sgw_gtpu_teid;
			si.sess_id = SESS_ID(
					context->s11_sgw_gtpc_teid,
					si.bearer_id);
		} else {
			rte_panic("Incorrect provisioning of bearers\n");
		}
	}
	*_context = context;
	return 0;
}

int
process_delete_session_request(gtpv2c_header_t *gtpv2c_rx,
		gtpv2c_header_t *gtpv2c_s11_tx, gtpv2c_header_t *gtpv2c_s5s8_tx)
{
	int ret;
	ue_context *context = NULL;
	uint32_t s5s8_pgw_gtpc_teid = 0;
	uint32_t s5s8_pgw_gtpc_ipv4 = 0;
	del_sess_req_t ds_req = {0};

	decode_del_sess_req((uint8_t *) gtpv2c_rx, &ds_req);

	if (spgw_cfg == SGWC) {
		pdn_connection *pdn = NULL;
		uint32_t s5s8_pgw_gtpc_del_teid;
		static uint32_t process_sgwc_s5s8_ds_req_cnt;

		/* s11_sgw_gtpc_teid= key->ue_context_by_fteid_hash */
		ret = rte_hash_lookup_data(ue_context_by_fteid_hash,
			(const void *) &ds_req.header.teid.has_teid.teid,
			(void **) &context);

		if (ret < 0 || !context)
			return GTPV2C_CAUSE_CONTEXT_NOT_FOUND;

		uint8_t del_ebi_index = ds_req.lbi.ebi_ebi - 5;
		pdn = context->pdns[del_ebi_index];
		/* s11_sgw_gtpc_teid = s5s8_pgw_gtpc_base_teid =
		 * key->ue_context_by_fteid_hash */
		s5s8_pgw_gtpc_del_teid = ntohl(pdn->s5s8_pgw_gtpc_teid);

		ret =
			gen_sgwc_s5s8_delete_session_request(gtpv2c_rx,
				gtpv2c_s5s8_tx, s5s8_pgw_gtpc_del_teid,
				gtpv2c_rx->teid.has_teid.seq, ds_req.lbi.ebi_ebi);
		RTE_LOG(DEBUG, CP, "NGIC- delete_session.c::"
				"\n\tprocess_delete_session_request::case= %d;"
				"\n\tprocess_sgwc_s5s8_ds_req_cnt= %u;"
				"\n\tue_ip= pdn->ipv4= %s;"
				"\n\tpdn->s5s8_sgw_gtpc_ipv4= %s;"
				"\n\tpdn->s5s8_sgw_gtpc_teid= %X;"
				"\n\tpdn->s5s8_pgw_gtpc_ipv4= %s;"
				"\n\tpdn->s5s8_pgw_gtpc_teid= %X;"
				"\n\tgen_delete_s5s8_session_request= %d\n",
				spgw_cfg, process_sgwc_s5s8_ds_req_cnt++,
				inet_ntoa(pdn->ipv4),
				inet_ntoa(pdn->s5s8_sgw_gtpc_ipv4),
				pdn->s5s8_sgw_gtpc_teid,
				inet_ntoa(pdn->s5s8_pgw_gtpc_ipv4),
				pdn->s5s8_pgw_gtpc_teid,
				ret);
		return ret;
	}

	gtpv2c_s11_tx->teid.has_teid.seq = gtpv2c_rx->teid.has_teid.seq;

	/* Lookup and get context of delete request */
	ret = delete_context(&ds_req, &context, &s5s8_pgw_gtpc_teid,
			&s5s8_pgw_gtpc_ipv4);
	if (ret)
		return ret;

	set_gtpv2c_teid_header(gtpv2c_s11_tx, GTP_DELETE_SESSION_RSP,
	    htonl(context->s11_mme_gtpc_teid), gtpv2c_rx->teid.has_teid.seq);
	set_cause_accepted_ie(gtpv2c_s11_tx, IE_INSTANCE_ZERO);

	return 0;
}
