/*
 * Copyright (c) 2019 Sprint
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtpv2c.h"
#include "sm_pcnd.h"
#include "cp_stats.h"
#include "debug_str.h"
#include "pfcp_util.h"

#ifdef C3PO_OSS
#include "cp_config.h"
#endif /* C3PO_OSS */

pfcp_config_t pfcp_config;
extern struct cp_stats_t cp_stats;

uint8_t
gtpc_s5s8_pcnd_check(gtpv2c_header *gtpv2c_rx, msg_info *msg, int bytes_rx)
{
	int ret = 0;

	if ((ret = gtpv2c_pcnd_check(gtpv2c_rx, bytes_rx)) != 0)
		return ret;

	msg->msg_type = gtpv2c_rx->gtpc.type;

	switch(msg->msg_type) {
	case GTP_CREATE_SESSION_REQ: {
			if ((ret = decode_check_csr(gtpv2c_rx, &msg->s11_msg.csr)) != 0)
				return ret;

#ifdef USE_DNS_QUERY
			upfs_dnsres_t *entry = NULL;

			if (get_upf_list(&msg->s11_msg.csr) == 0)
				return -1;

			/* Fill msg->upf_ipv4 address */
			if ((get_upf_ip((&msg->s11_msg.csr), &entry, &msg->upf_ipv4.s_addr)) != 0) {
				fprintf(stderr, "Failed to get upf ip address\n");
				return -1;
			}
#else
			msg->upf_ipv4 = pfcp_config.upf_pfcp_ip;
			csUpdateIp(inet_ntoa(*((struct in_addr *)&msg->upf_ipv4)), 0, 0);
#endif /* USE_DNS_QUERY */

			/*VS:TODO: Need to think on it */
			cp_stats.sgwc_status = 1;

			upf_context_t *upf_context = NULL;

			ret = rte_hash_lookup_data(upf_context_by_ip_hash,
					(const void*) &(msg->upf_ipv4.s_addr), (void **) &(upf_context));
			if (ret == 0) {
					if (upf_context->state == ASSOC_RESP_RCVD_STATE) {
						memcpy(msg->sgwu_fqdn, upf_context->fqdn, strlen(upf_context->fqdn));
					}
					msg->state = upf_context->state;
			} else {
					msg->state = NONE_STATE;
			}

			msg->event = CS_REQ_RCVD_EVNT;

			clLog(s5s8logger, eCLSeverityDebug, "%s: Callback called for"
					"Msg_Type:%s[%u], Teid:%u, "
					"State:%s, Event:%s\n",
					__func__, gtp_type_str(msg->msg_type), msg->msg_type,
					gtpv2c_rx->teid_u.has_teid.teid,
					get_state_string(msg->state), get_event_string(msg->event));
		break;
	}

	case GTP_CREATE_SESSION_RSP: {
			/*TODO: This part will remove after getting libgtpv2c support on S5S8.*/
			/*Parse the CS Resp received msg from PGWC and stored into the struct. */
			/*ret = parse_sgwc_s5s8_create_session_response(gtpv2c_rx,
			 *		&msg->s5s8_msg.csr_resp);
			 *if (ret)
			 *	return ret;*/

			/*Need to Think about stats*/
			cp_stats.pgwc_status = 1;
			cp_stats.nbr_of_pgwc_to_sgwc_timeouts = 0;
			cp_stats.create_session_resp_acc_rcvd++;
			get_current_time(cp_stats.create_session_resp_acc_rcvd_time);

			/* Remove after libgtpv2 support */
			msg->s5s8_msg.gtpv2c_rx = *gtpv2c_rx;


			gtpv2c_rx->teid_u.has_teid.teid = ntohl(gtpv2c_rx->teid_u.has_teid.teid);

			/*Retrive UE state. */
			if ((ret = get_ue_state(gtpv2c_rx->teid_u.has_teid.teid)) > 0) {
				msg->state = ret;
			} else {
				return -1;
			}

			/*Set the appropriate event type.*/
			msg->event = CS_RESP_RCVD_EVNT;

			clLog(s5s8logger, eCLSeverityDebug, "%s: Callback called for"
					"Msg_Type:%s[%u], Teid:%u, "
					"State:%s, Event:%s\n",
					__func__, gtp_type_str(msg->msg_type), msg->msg_type,
					gtpv2c_rx->teid_u.has_teid.teid,
					get_state_string(msg->state), get_event_string(msg->event));
		break;
	}

	case GTP_DELETE_SESSION_REQ: {
			/* Decode delete session request */
			//ret = decode_delete_session_request_t((uint8_t *) gtpv2c_rx,
			//		&msg->s11_msg.dsr);
			//if (ret < 0)
			//	return -1;

			gtpv2c_rx->teid_u.has_teid.teid = ntohl(gtpv2c_rx->teid_u.has_teid.teid);

			/* Retrive ue state and set in msg state and event */
			if((ret = get_ue_state(gtpv2c_rx->teid_u.has_teid.teid)) > 0){
				msg->state = ret;
			}else{
				return -1;
			}

			/*Set the appropriate event type.*/
			msg->event = DS_REQ_RCVD_EVNT;

			clLog(s5s8logger, eCLSeverityDebug, "%s: Callback called for"
					"Msg_Type:%s[%u], Teid:%u, "
					"State:%s, Event:%s\n",
					__func__, gtp_type_str(msg->msg_type), msg->msg_type,
					gtpv2c_rx->teid_u.has_teid.teid,
					get_state_string(msg->state), get_event_string(msg->event));
		break;
	}

	case GTP_DELETE_SESSION_RSP: {
			/* Decode delete session response */
			//ret = decode_delete_session_response_t((uint8_t *) gtpv2c_rx,
			//		&msg->s11_msg.dsr);
			//if (ret < 0)
			//	return -1;

			/* Retrive ue state and set in msg state and event */

			gtpv2c_rx->teid_u.has_teid.teid = ntohl(gtpv2c_rx->teid_u.has_teid.teid);

			if((ret = get_ue_state(gtpv2c_rx->teid_u.has_teid.teid)) > 0){
				msg->state = ret;
			}else{
				return -1;
			}

			/*Set the appropriate event type.*/
			msg->event = DS_RESP_RCVD_EVNT;

			clLog(s5s8logger, eCLSeverityDebug, "%s: Callback called for"
					"Msg_Type:%s[%u], Teid:%u, "
					"State:%s, Event:%s\n",
					__func__, gtp_type_str(msg->msg_type), msg->msg_type,
					gtpv2c_rx->teid_u.has_teid.teid,
					get_state_string(msg->state), get_event_string(msg->event));
		break;
	}
	default:
			/*If Event is not supported then we will called default handler. */
			/*Retrive UE state. */
			if ((ret = get_ue_state(ntohl(gtpv2c_rx->teid_u.has_teid.teid))) > 0) {
			     msg->state = ret;
			} else {
			     msg->state = NONE_STATE;
			}

			msg->event = NONE_EVNT;

			fprintf(stderr, "%s::process_msgs-"
					"\n\tcase: SAEGWC::spgw_cfg= %d;"
					"\n\tReceived unprocessed GTPv2c Message Type: "
					"%s (%u 0x%x)... Discarding\n", __func__,
					spgw_cfg, gtp_type_str(gtpv2c_rx->gtpc.type),
					gtpv2c_rx->gtpc.type,
					gtpv2c_rx->gtpc.type);
			return -1;
	}

	return 0;
}
