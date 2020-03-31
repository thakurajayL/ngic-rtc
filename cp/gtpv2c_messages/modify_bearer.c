/* SPDX-License-Identifier: Apache-2.0
 * Copyright(c) 2017 Intel Corporation
 */

#include "ue.h"
#include "gtp_messages.h"
#include "gtpv2c_set_ie.h"
#include "../cp_dp_api/vepc_cp_dp_api.h"
#include "../pfcp_messages/pfcp_set_ie.h"

struct parse_modify_bearer_request_t {
	ue_context *context;
	pdn_connection *pdn;
	eps_bearer *bearer;

	gtpv2c_ie *bearer_context_to_be_created_ebi;
	gtpv2c_ie *s1u_enb_fteid;
	uint8_t *delay;
	uint32_t *s11_mme_gtpc_fteid;
};
extern uint32_t num_adc_rules;
extern uint32_t adc_rule_id[];

/**
 * from parameters, populates gtpv2c message 'modify bearer response' and
 * populates required information elements as defined by
 * clause 7.2.8 3gpp 29.274
 * @param gtpv2c_tx
 *   transmission buffer to contain 'modify bearer request' message
 * @param sequence
 *   sequence number as described by clause 7.6 3gpp 29.274
 * @param context
 *   UE Context data structure pertaining to the bearer to be modified
 * @param bearer
 *   bearer data structure to be modified
 */
void
set_modify_bearer_response(gtpv2c_header_t *gtpv2c_tx,
		uint32_t sequence, ue_context *context, eps_bearer *bearer)
{
	int ret = 0;
	uint64_t _ebi = bearer->eps_bearer_id;
	uint64_t ebi_index = _ebi - 5;
	upf_context_t *upf_ctx = NULL;

	/*Retrive bearer id from bearer --> context->pdns[]->upf_ip*/
	if ((ret = upf_context_entry_lookup(context->pdns[ebi_index]->upf_ipv4.s_addr,
			&upf_ctx)) < 0) {
		return;
	}

	mod_bearer_rsp_t mb_resp = {0};

	set_gtpv2c_teid_header((gtpv2c_header_t *) &mb_resp, GTP_MODIFY_BEARER_RSP,
	    context->s11_mme_gtpc_teid, sequence);

	set_cause_accepted(&mb_resp.cause, IE_INSTANCE_ZERO);

	set_ie_header(&mb_resp.bearer_contexts_modified.header, GTP_IE_BEARER_CONTEXT,
			IE_INSTANCE_ZERO, 0);

	set_cause_accepted(&mb_resp.bearer_contexts_modified.cause, IE_INSTANCE_ZERO);
	mb_resp.bearer_contexts_modified.header.len +=
		sizeof(struct cause_ie_hdr_t) + IE_HEADER_SIZE;

	set_ebi(&mb_resp.bearer_contexts_modified.eps_bearer_id, IE_INSTANCE_ZERO,
			bearer->eps_bearer_id);
	mb_resp.bearer_contexts_modified.header.len += sizeof(uint8_t) + IE_HEADER_SIZE;

	struct in_addr ip;
	ip.s_addr = upf_ctx->s1u_ip;

	set_ipv4_fteid(&mb_resp.bearer_contexts_modified.s1u_sgw_fteid,
			GTPV2C_IFTYPE_S1U_SGW_GTPU, IE_INSTANCE_ZERO, ip,
			bearer->s1u_sgw_gtpu_teid);

	mb_resp.bearer_contexts_modified.header.len += sizeof(struct fteid_ie_hdr_t) +
		sizeof(struct in_addr) + IE_HEADER_SIZE;

	uint16_t msg_len = 0;
	msg_len = encode_mod_bearer_rsp(&mb_resp, (uint8_t *)gtpv2c_tx);
	gtpv2c_tx->gtpc.message_len = htons(msg_len - 4);
}
/*MODIFY RESPONSE FUNCTION WHEN PGWC returns MBR RESPONSE to SGWC
 * in HANDOVER SCENARIO*/

void
set_modify_bearer_response_handover(gtpv2c_header_t *gtpv2c_tx,
		uint32_t sequence, ue_context *context, eps_bearer *bearer)
{
	int ret = 0;
	uint64_t _ebi = bearer->eps_bearer_id;
	uint64_t ebi_index = _ebi - 5;
	upf_context_t *upf_ctx = NULL;

	/*Retrive bearer id from bearer --> context->pdns[]->upf_ip*/
	if ((ret = upf_context_entry_lookup(context->pdns[ebi_index]->upf_ipv4.s_addr,
			&upf_ctx)) < 0) {
		return;
	}

	mod_bearer_rsp_t mb_resp = {0};

	set_gtpv2c_teid_header((gtpv2c_header_t *) &mb_resp, GTP_MODIFY_BEARER_RSP,
		bearer->pdn->s5s8_sgw_gtpc_teid, sequence);


	set_cause_accepted(&mb_resp.cause, IE_INSTANCE_ZERO);

	set_ie_header(&mb_resp.bearer_contexts_modified.header, GTP_IE_BEARER_CONTEXT,
			IE_INSTANCE_ZERO, 0);

	set_cause_accepted(&mb_resp.bearer_contexts_modified.cause, IE_INSTANCE_ZERO);
	mb_resp.bearer_contexts_modified.header.len +=
		sizeof(struct cause_ie_hdr_t) + IE_HEADER_SIZE;

	set_ebi(&mb_resp.bearer_contexts_modified.eps_bearer_id, IE_INSTANCE_ZERO,
			bearer->eps_bearer_id);
	mb_resp.bearer_contexts_modified.header.len += sizeof(uint8_t) + IE_HEADER_SIZE;

	uint16_t msg_len = 0;
	msg_len = encode_mod_bearer_rsp(&mb_resp, (uint8_t *)gtpv2c_tx);
	gtpv2c_tx->gtpc.message_len = htons(msg_len - 4);
}

int
process_modify_bearer_request(gtpv2c_header_t *gtpv2c_rx,
		gtpv2c_header_t *gtpv2c_tx)
{
	struct dp_id dp_id = { .id = DPN_ID };
	mod_bearer_req_t mb_req = {0};
	uint32_t i;
	ue_context *context = NULL;
	eps_bearer *bearer = NULL;
	pdn_connection *pdn = NULL;

	decode_mod_bearer_req((uint8_t *) gtpv2c_rx, &mb_req);

	int ret = rte_hash_lookup_data(ue_context_by_fteid_hash,
	    (const void *) &mb_req.header.teid.has_teid.teid,
	    (void **) &context);

	if (ret < 0 || !context)
		return GTPV2C_CAUSE_CONTEXT_NOT_FOUND;

	if (!mb_req.bearer_contexts_to_be_modified.eps_bearer_id.header.len
			|| !mb_req.bearer_contexts_to_be_modified.s1_enodeb_fteid.header.len) {
			fprintf(stderr, "Dropping packet\n");
			return -EPERM;
	}

	uint8_t ebi_index = mb_req.bearer_contexts_to_be_modified.eps_bearer_id.ebi_ebi - 5;
	if (!(context->bearer_bitmap & (1 << ebi_index))) {
		fprintf(stderr,
			"Received modify bearer on non-existent EBI - "
			"Dropping packet\n");
		return -EPERM;
	}

	bearer = context->eps_bearers[ebi_index];
	if (!bearer) {
		fprintf(stderr,
			"Received modify bearer on non-existent EBI - "
			"Bitmap Inconsistency - Dropping packet\n");
		return -EPERM;
	}

	pdn = bearer->pdn;

	/* TODO something with modify_bearer_request.delay if set */

	if (mb_req.sender_fteid_ctl_plane.header.len &&
			(context->s11_mme_gtpc_teid != mb_req.bearer_contexts_to_be_modified.s11_u_mme_fteid.teid_gre_key))
		context->s11_mme_gtpc_teid = mb_req.bearer_contexts_to_be_modified.s11_u_mme_fteid.teid_gre_key;

	bearer->s1u_enb_gtpu_ipv4.s_addr =
			mb_req.bearer_contexts_to_be_modified.s1_enodeb_fteid.ipv4_address;

	bearer->s1u_enb_gtpu_teid =
			mb_req.bearer_contexts_to_be_modified.s1_enodeb_fteid.teid_gre_key;

	bearer->eps_bearer_id = mb_req.bearer_contexts_to_be_modified.eps_bearer_id.ebi_ebi;

	set_modify_bearer_response(gtpv2c_tx, mb_req.header.teid.has_teid.seq,
	    context, bearer);

	/* using the s1u_sgw_gtpu_teid as unique identifier to the session */
	struct session_info session;
	memset(&session, 0, sizeof(session));
	 session.ue_addr.iptype = IPTYPE_IPV4;
	 session.ue_addr.u.ipv4_addr =
		 pdn->ipv4.s_addr;
	 session.ul_s1_info.sgw_teid =
		htonl(bearer->s1u_sgw_gtpu_teid);
	 session.ul_s1_info.sgw_addr.iptype = IPTYPE_IPV4;
	 session.ul_s1_info.sgw_addr.u.ipv4_addr =
		 htonl(bearer->s1u_sgw_gtpu_ipv4.s_addr);
	 session.ul_s1_info.enb_addr.iptype = IPTYPE_IPV4;
	 session.ul_s1_info.enb_addr.u.ipv4_addr =
		 bearer->s1u_enb_gtpu_ipv4.s_addr;
	 session.dl_s1_info.enb_teid =
		 bearer->s1u_enb_gtpu_teid;
	 session.dl_s1_info.enb_addr.iptype = IPTYPE_IPV4;
	 session.dl_s1_info.enb_addr.u.ipv4_addr =
		 bearer->s1u_enb_gtpu_ipv4.s_addr;
	 session.dl_s1_info.sgw_addr.iptype = IPTYPE_IPV4;
	 session.dl_s1_info.sgw_addr.u.ipv4_addr =
		 htonl(bearer->s1u_sgw_gtpu_ipv4.s_addr);
	 session.ul_apn_mtr_idx = 0;
	 session.dl_apn_mtr_idx = 0;
	 session.num_ul_pcc_rules = 1;
	 session.ul_pcc_rule_id[0] = FIRST_FILTER_ID;
	 session.num_dl_pcc_rules = 1;
	 session.dl_pcc_rule_id[0] = FIRST_FILTER_ID;

	 session.num_adc_rules = num_adc_rules;
	 for (i = 0; i < num_adc_rules; ++i)
			 session.adc_rule_id[i] = adc_rule_id[i];

	 session.sess_id = SESS_ID(
			context->s11_sgw_gtpc_teid,
			bearer->eps_bearer_id);

	if (session_modify(dp_id, session) < 0)
		rte_exit(EXIT_FAILURE, "Bearer Session modify fail !!!");
	return 0;
}


void set_modify_bearer_request(gtpv2c_header_t *gtpv2c_tx, /*create_sess_req_t *csr,*/
		pdn_connection *pdn, eps_bearer *bearer)
{
	mod_bearer_req_t  mbr = {0};
	set_gtpv2c_teid_header((gtpv2c_header_t *)&mbr.header, GTP_MODIFY_BEARER_REQ,
			0, pdn->context->sequence);

	mbr.header.teid.has_teid.teid = pdn->s5s8_pgw_gtpc_teid;
	bearer->s5s8_sgw_gtpu_ipv4.s_addr = htonl(bearer->s5s8_sgw_gtpu_ipv4.s_addr);
	set_ipv4_fteid(&mbr.sender_fteid_ctl_plane, GTPV2C_IFTYPE_S5S8_SGW_GTPC,
			IE_INSTANCE_ZERO,
			pdn->s5s8_sgw_gtpc_ipv4, pdn->s5s8_sgw_gtpc_teid);


	set_ie_header(&mbr.bearer_contexts_to_be_modified.header, GTP_IE_BEARER_CONTEXT,
			IE_INSTANCE_ZERO, 0);
	set_ebi(&mbr.bearer_contexts_to_be_modified.eps_bearer_id, IE_INSTANCE_ZERO,
			bearer->eps_bearer_id);

	mbr.bearer_contexts_to_be_modified.header.len += sizeof(uint8_t) + IE_HEADER_SIZE;

	/* Refer spec 23.274.Table 7.2.7-2 */
	bearer->s5s8_sgw_gtpu_ipv4.s_addr = (bearer->s5s8_sgw_gtpu_ipv4.s_addr);
	set_ipv4_fteid(&mbr.bearer_contexts_to_be_modified.s58_u_sgw_fteid,
			GTPV2C_IFTYPE_S5S8_SGW_GTPU,
			IE_INSTANCE_ONE,bearer->s5s8_sgw_gtpu_ipv4,
			(bearer->s5s8_sgw_gtpu_teid));

	mbr.bearer_contexts_to_be_modified.header.len += sizeof(struct fteid_ie_hdr_t) +
		sizeof(struct in_addr) + IE_HEADER_SIZE;

	uint16_t msg_len = 0;
	msg_len = encode_mod_bearer_req(&mbr, (uint8_t *)gtpv2c_tx);
	gtpv2c_tx->gtpc.message_len = htons(msg_len - 4);

}


#if 0
void set_modify_bearer_request(gtpv2c_header *gtpv2c_tx, create_sess_req_t *csr,
		pdn_connection *pdn, eps_bearer *bearer)
{
	mod_bearer_req_t  mbr = {0};
	set_gtpv2c_teid_header((gtpv2c_header *)&mbr.header, GTP_MODIFY_BEARER_REQ,
			0, csr->header.teid.has_teid.seq);

	//AAQUIL need to remove
	mbr.header.teid.has_teid.teid = csr->pgw_s5s8_addr_ctl_plane_or_pmip.teid_gre_key;
	//0xeeffd000;

	//set_indication(&mbr.indication, IE_INSTANCE_ZERO);
	//mbr.indication.indication_value.sgwci = 1;

	pdn->s5s8_sgw_gtpc_ipv4.s_addr = htonl(pdn->s5s8_sgw_gtpc_ipv4.s_addr);


	if(csr->uli.header.len !=0) {
		//set_ie_copy(gtpv2c_tx, current_rx_ie);
		set_uli(&mbr.uli, csr, IE_INSTANCE_ZERO);
	}

	if(csr->serving_network.header.len !=0) {
		set_serving_network(&mbr.serving_network, csr, IE_INSTANCE_ZERO);
	}
	if(csr->ue_time_zone.header.len !=0) {
		set_ue_timezone(&mbr.ue_time_zone, csr, IE_INSTANCE_ZERO);
	}

	set_ipv4_fteid(&mbr.sender_fteid_ctl_plane, GTPV2C_IFTYPE_S5S8_SGW_GTPC,
			IE_INSTANCE_ZERO,
			(pdn->s5s8_sgw_gtpc_ipv4), pdn->s5s8_sgw_gtpc_teid);

	set_ie_header(&mbr.bearer_contexts_to_be_modified.header, GTP_IE_BEARER_CONTEXT,
			IE_INSTANCE_ZERO, 0);
	set_ebi(&mbr.bearer_contexts_to_be_modified.eps_bearer_id, IE_INSTANCE_ZERO,
			bearer->eps_bearer_id);

	mbr.bearer_contexts_to_be_modified.header.len += sizeof(uint8_t) + IE_HEADER_SIZE;

	/* Refer spec 23.274.Table 7.2.7-2 */
	bearer->s5s8_sgw_gtpu_ipv4.s_addr = htonl(bearer->s5s8_sgw_gtpu_ipv4.s_addr);
	set_ipv4_fteid(&mbr.bearer_contexts_to_be_modified.s58_u_sgw_fteid,
			GTPV2C_IFTYPE_S5S8_SGW_GTPU,
			IE_INSTANCE_ONE,(bearer->s5s8_sgw_gtpu_ipv4),
			htonl(bearer->s5s8_sgw_gtpu_teid));
	//htonl(bearer->s1u_sgw_gtpu_teid));

	mbr.bearer_contexts_to_be_modified.header.len += sizeof(struct fteid_ie_hdr_t) +
		sizeof(struct in_addr) + IE_HEADER_SIZE;

	uint16_t msg_len = 0;
	encode_mod_bearer_req(&mbr, (uint8_t *)gtpv2c_tx);
	gtpv2c_tx->gtpc.length = htons(msg_len - 4);
	printf("The length of mbr is %d and gtpv2c is %d\n\n", mbr.bearer_contexts_to_be_modified.header.len,
			gtpv2c_tx->gtpc.length);
}

#endif
