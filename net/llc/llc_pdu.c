/*
 * llc_pdu.c - access to PDU internals
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 *		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/netdevice.h>
#include <linux/if_tr.h>
#include <net/llc_pdu.h>
#include <net/llc_if.h>
#include <net/llc_mac.h>
#include <net/llc_main.h>

static int llc_pdu_decode_pdu_type(struct sk_buff *skb, u8 *type);
static int llc_get_llc_hdr_length(u8 pdu_type);
static u8 llc_pdu_get_pf_bit(llc_pdu_sn_t *pdu);

/**
 *	llc_pdu_header_init - initializes pdu header
 *	@skb: input skb that header must be set into it.
 *	@pdu_type: type of PDU (U, I or S).
 *	@ssap: source sap.
 *	@dsap: destination sap.
 *	@cr: command/response bit (0 or 1).
 *
 *	This function sets DSAP, SSAP and command/Response bit in LLC header.
 */
void llc_pdu_header_init(struct sk_buff *skb, u8 pdu_type, u8 ssap,
			 u8 dsap, u8 cr)
{
	llc_pdu_un_t *p;

	skb->nh.raw = skb_push(skb, llc_get_llc_hdr_length(pdu_type));
	p = (llc_pdu_un_t *)skb->nh.raw;
	p->dsap = dsap;
	p->ssap = ssap;
	p->ssap |= cr;
}

void llc_pdu_set_cmd_rsp(struct sk_buff *skb, u8 pdu_type)
{
	((llc_pdu_un_t *)skb->nh.raw)->ssap |= pdu_type;
}

/**
 *	pdu_set_pf_bit - sets poll/final bit in LLC header
 *	@pdu_frame: input frame that p/f bit must be set into it.
 *	@bit_value: poll/final bit (0 or 1).
 *
 *	This function sets poll/final bit in LLC header (based on type of PDU).
 *	in I or S pdus, p/f bit is right bit of fourth byte in header. in U
 *	pdus p/f bit is fifth bit of third byte.
 */
void llc_pdu_set_pf_bit(struct sk_buff *skb, u8 bit_value)
{
	u8 pdu_type;

	if (llc_pdu_decode_pdu_type(skb, &pdu_type))
		goto out;
	switch (pdu_type) {
		case LLC_PDU_TYPE_I:
		case LLC_PDU_TYPE_S:
			((llc_pdu_sn_t *)skb->nh.raw)->ctrl_2 =
				(((llc_pdu_sn_t *)skb->nh.raw)->ctrl_2 & 0xFE) |
				bit_value;
			break;
		case LLC_PDU_TYPE_U:
			((llc_pdu_un_t *)skb->nh.raw)->ctrl_1 |=
				(((llc_pdu_un_t *)skb->nh.raw)->ctrl_1 & 0xEF) |
				(bit_value << 4);
			break;
	}
out:;
}

/**
 *	llc_pdu_decode_pf_bit - extracs poll/final bit from LLC header
 *	@skb: input skb that p/f bit must be extracted from it
 *	@pf_bit: poll/final bit (0 or 1)
 *
 *	This function extracts poll/final bit from LLC header (based on type of
 *	PDU). In I or S pdus, p/f bit is right bit of fourth byte in header. In
 *	U pdus p/f bit is fifth bit of third byte.
 */
int llc_pdu_decode_pf_bit(struct sk_buff *skb, u8 *pf_bit)
{
	u8 pdu_type;
	int rc = llc_pdu_decode_pdu_type(skb, &pdu_type);

	if (rc)
		goto out;
	switch (pdu_type) {
		case LLC_PDU_TYPE_I:
		case LLC_PDU_TYPE_S:
			*pf_bit = ((llc_pdu_sn_t *)skb->nh.raw)->ctrl_2 &
				  LLC_S_PF_BIT_MASK;
			break;
		case LLC_PDU_TYPE_U:
			*pf_bit = (((llc_pdu_un_t *)skb->nh.raw)->ctrl_1 &
				  LLC_U_PF_BIT_MASK) >> 4;
			break;
	}
out:
	return 0;
}

/**
 *	llc_pdu_decode_cr_bit - extracs command response bit from LLC header
 *	@skb: input skb that c/r bit must be extracted from it.
 *	@cr_bit: command/response bit (0 or 1).
 *
 *	This function extracts command/response bit from LLC header. this bit
 *	is right bit of source SAP.
 */
int llc_pdu_decode_cr_bit(struct sk_buff *skb, u8 *cr_bit)
{
	*cr_bit = ((llc_pdu_un_t *)skb->nh.raw)->ssap & LLC_PDU_CMD_RSP_MASK;
	return 0;
}

/**
 *	llc_pdu_decode_sa - extracs source address (MAC) of input frame
 *	@skb: input skb that source address must be extracted from it.
 *	@sa: pointer to source address (6 byte array).
 *
 *	This function extracts source address(MAC) of input frame.
 */
int llc_pdu_decode_sa(struct sk_buff *skb, u8 *sa)
{
	if (skb->protocol == ntohs(ETH_P_802_2))
		memcpy(sa, ((struct ethhdr *)skb->mac.raw)->h_source, ETH_ALEN);
	else if (skb->protocol == ntohs(ETH_P_TR_802_2))
		memcpy(sa, ((struct trh_hdr *)skb->mac.raw)->saddr, ETH_ALEN);
	return 0;
}

/**
 *	llc_pdu_decode_da - extracts dest address of input frame
 *	@skb: input skb that destination address must be extracted from it
 *	@sa: pointer to destination address (6 byte array).
 *
 *	This function extracts destination address(MAC) of input frame.
 */
int llc_pdu_decode_da(struct sk_buff *skb, u8 *da)
{
	if (skb->protocol == ntohs(ETH_P_802_2))
		memcpy(da, ((struct ethhdr *)skb->mac.raw)->h_dest, ETH_ALEN);
	else if (skb->protocol == ntohs(ETH_P_TR_802_2))
		memcpy(da, ((struct trh_hdr *)skb->mac.raw)->daddr, ETH_ALEN);
	return 0;
}

/**
 *	llc_pdu_decode_dsap - extracts dest SAP of input frame
 *	@skb: input skb that destination SAP must be extracted from it.
 *	@dsap: destination SAP (output argument).
 *
 *	This function extracts destination SAP of input frame. right bit of
 *	DSAP designates individual/group SAP.
 */
int llc_pdu_decode_dsap(struct sk_buff *skb, u8 *dsap)
{
	*dsap = ((llc_pdu_un_t *)skb->nh.raw)->dsap & 0xFE;
	return 0;
}

/**
 *	llc_pdu_decode_ssap - extracts source SAP of input frame
 *	@skb: input skb that source SAP must be extracted from it.
 *	@ssap: source SAP (output argument).
 *
 *	This function extracts source SAP of input frame. right bit of SSAP is
 *	command/response bit.
 */
int llc_pdu_decode_ssap(struct sk_buff *skb, u8 *ssap)
{
	*ssap = ((llc_pdu_un_t *)skb->nh.raw)->ssap & 0xFE;
	return 0;
}

/**
 *	llc_pdu_init_as_ui_cmd - sets LLC header as UI PDU
 *	@skb: input skb that header must be set into it.
 *
 *	This function sets third byte of LLC header as a UI PDU.
 */
int llc_pdu_init_as_ui_cmd(struct sk_buff *skb)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_1_PDU_CMD_UI;
	return 0;
}

/**
 *	llc_pdu_init_as_xid_cmd - sets bytes 3, 4 & 5 of LLC header as XID
 *	@skb: input skb that header must be set into it.
 *
 *	This function sets third,fourth,fifth and sixth bytes of LLC header as
 *	a XID PDU.
 */
int llc_pdu_init_as_xid_cmd(struct sk_buff *skb, u8 svcs_supported,
			    u8 rx_window)
{
	llc_xid_info_t *xid_info;
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1	 = LLC_PDU_TYPE_U;
	pdu->ctrl_1	|= LLC_1_PDU_CMD_XID;
	pdu->ctrl_1	|= LLC_U_PF_BIT_MASK;
	xid_info	 = (llc_xid_info_t *)(((u8 *)&pdu->ctrl_1) + 1);
	xid_info->fmt_id = LLC_XID_FMT_ID;	/* 0x81 */
	xid_info->type	 = svcs_supported;
	xid_info->rw	 = rx_window << 1;	/* size of recieve window */
	skb_put(skb, 3);
	return 0;
}

/**
 *	llc_pdu_init_as_test_cmd - sets PDU as TEST
 *	@skb - Address of the skb to build
 *
 * 	Sets a PDU as TEST
 */
int llc_pdu_init_as_test_cmd(struct sk_buff *skb)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_1_PDU_CMD_TEST;
	pdu->ctrl_1 |= LLC_U_PF_BIT_MASK;
	return 0;
}

/**
 *	llc_pdu_init_as_disc_cmd - Builds DISC PDU
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *
 *	Builds a pdu frame as a DISC command.
 */
int llc_pdu_init_as_disc_cmd(struct sk_buff *skb, u8 p_bit)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_2_PDU_CMD_DISC;
	pdu->ctrl_1 |= ((p_bit & 1) << 4) & LLC_U_PF_BIT_MASK;
	return 0;
}

/**
 *	pdu_init_as_i_cmd - builds I pdu
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *	@ns: The sequence number of the data PDU
 *	@nr: The seq. number of the expected I PDU from the remote
 *
 *	Builds a pdu frame as an I command.
 */
int llc_pdu_init_as_i_cmd(struct sk_buff *skb, u8 p_bit, u8 ns, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_I;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= (p_bit & LLC_I_PF_BIT_MASK); /* p/f bit */
	pdu->ctrl_1 |= (ns << 1) & 0xFE;   /* set N(S) in bits 2..8 */
	pdu->ctrl_2 |= (nr << 1) & 0xFE;   /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_rej_cmd - builds REJ PDU
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *	@nr: The seq. number of the expected I PDU from the remote
 *
 *	Builds a pdu frame as a REJ command.
 */
int llc_pdu_init_as_rej_cmd(struct sk_buff *skb, u8 p_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_CMD_REJ;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= p_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE; /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_rnr_cmd - builds RNR pdu
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *	@nr: The seq. number of the expected I PDU from the remote
 *
 *	Builds a pdu frame as an RNR command.
 */
int llc_pdu_init_as_rnr_cmd(struct sk_buff *skb, u8 p_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_CMD_RNR;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= p_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE; /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_rr_cmd - Builds RR pdu
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *	@nr: The seq. number of the expected I PDU from the remote
 *
 *	Builds a pdu frame as an RR command.
 */
int llc_pdu_init_as_rr_cmd(struct sk_buff *skb, u8 p_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_CMD_RR;
	pdu->ctrl_2  = p_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE; /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_sabme_cmd - builds SABME pdu
 *	@skb: Address of the skb to build
 *	@p_bit: The P bit to set in the PDU
 *
 *	Builds a pdu frame as an SABME command.
 */
int llc_pdu_init_as_sabme_cmd(struct sk_buff *skb, u8 p_bit)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_2_PDU_CMD_SABME;
	pdu->ctrl_1 |= ((p_bit & 1) << 4) & LLC_U_PF_BIT_MASK;
	return 0;
}

/**
 *	pdu_init_as_dm_rsp - builds DM response pdu
 *	@skb: Address of the skb to build
 *	@f_bit: The F bit to set in the PDU
 *
 *	Builds a pdu frame as a DM response.
 */
int llc_pdu_init_as_dm_rsp(struct sk_buff *skb, u8 f_bit)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_DM;
	pdu->ctrl_1 |= ((f_bit & 1) << 4) & LLC_U_PF_BIT_MASK;
	return 0;
}

/**
 *	pdu_init_as_xid_rsp - builds XID response PDU
 *	@skb: Address of the skb to build
 *	@svcs_supported: The class of the LLC (I or II)
 *	@rx_window: The size of the receive window of the LLC
 *
 *	Builds a pdu frame as an XID response.
 */
int llc_pdu_init_as_xid_rsp(struct sk_buff *skb, u8 svcs_supported,
			    u8 rx_window)
{
	llc_xid_info_t *xid_info;
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1	 = LLC_PDU_TYPE_U;
	pdu->ctrl_1	|= LLC_1_PDU_CMD_XID;
	pdu->ctrl_1	|= LLC_U_PF_BIT_MASK;

	xid_info	 = (llc_xid_info_t *)(((u8 *)&pdu->ctrl_1) + 1);
	xid_info->fmt_id = LLC_XID_FMT_ID;
	xid_info->type	 = svcs_supported;
	xid_info->rw	 = rx_window << 1;
	skb_put(skb, 3);
	return 0;
}

/**
 *	pdu_init_as_test_rsp - build TEST response PDU
 *	@skb: Address of the skb to build
 *	@ev_skb: The received TEST command PDU frame
 *
 *	Builds a pdu frame as a TEST response.
 */
int llc_pdu_init_as_test_rsp(struct sk_buff *skb, struct sk_buff *ev_skb)
{
	int dsize;
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_1_PDU_CMD_TEST;
	pdu->ctrl_1 |= LLC_U_PF_BIT_MASK;
	if (ev_skb->protocol == ntohs(ETH_P_802_2)) {
		dsize = ntohs(((struct ethhdr *)ev_skb->mac.raw)->h_proto) - 3;
		memcpy(((u8 *)skb->nh.raw) + 3,
		       ((u8 *)ev_skb->nh.raw) + 3, dsize);
		skb_put(skb, dsize);
	}
	return 0;
}

/**
 *	pdu_init_as_frmr_rsp - builds FRMR response PDU
 *	@pdu_frame: Address of the frame to build
 *	@prev_pdu: The rejected PDU frame
 *	@f_bit: The F bit to set in the PDU
 *	@vs: tx state vari value for the data link conn at the rejecting LLC
 *	@vr: rx state var value for the data link conn at the rejecting LLC
 *	@vzyxw: completely described in the IEEE Std 802.2 document (Pg 55)
 *
 *	Builds a pdu frame as a FRMR response.
 */
int llc_pdu_init_as_frmr_rsp(struct sk_buff *skb, llc_pdu_sn_t *prev_pdu,
			     u8 f_bit, u8 vs, u8 vr, u8 vzyxw)
{
	llc_frmr_info_t *frmr_info;
	u8 prev_pf = 0;
	u8 *ctrl;
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_FRMR;
	pdu->ctrl_1 |= ((f_bit & 1) << 4) & LLC_U_PF_BIT_MASK;

	frmr_info = (llc_frmr_info_t *)&pdu->ctrl_2;
	ctrl = (u8 *)&prev_pdu->ctrl_1;
	FRMR_INFO_SET_REJ_CNTRL(frmr_info,ctrl);
	FRMR_INFO_SET_Vs(frmr_info, vs);
	FRMR_INFO_SET_Vr(frmr_info, vr);
	prev_pf = llc_pdu_get_pf_bit(prev_pdu);
	FRMR_INFO_SET_C_R_BIT(frmr_info, prev_pf);
	FRMR_INFO_SET_INVALID_PDU_CTRL_IND(frmr_info, vzyxw);
	FRMR_INFO_SET_INVALID_PDU_INFO_IND(frmr_info, vzyxw);
	FRMR_INFO_SET_PDU_INFO_2LONG_IND(frmr_info, vzyxw);
	FRMR_INFO_SET_PDU_INVALID_Nr_IND(frmr_info, vzyxw);
	FRMR_INFO_SET_PDU_INVALID_Ns_IND(frmr_info, vzyxw);
	skb_put(skb, 5);
	return 0;
}

/**
 *	pdu_init_as_rr_rsp - builds RR response pdu
 *	@skb: Address of the skb to build
 *	@f_bit: The F bit to set in the PDU
 *	@nr: The seq. number of the expected data PDU from the remote
 *
 *	Builds a pdu frame as an RR response.
 */
int llc_pdu_init_as_rr_rsp(struct sk_buff *skb, u8 f_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_RR;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= f_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE;  /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_rej_rsp - builds REJ response pdu
 *	@skb: Address of the skb to build
 *	@f_bit: The F bit to set in the PDU
 *	@nr: The seq. number of the expected data PDU from the remote
 *
 *	Builds a pdu frame as a REJ response.
 */
int llc_pdu_init_as_rej_rsp(struct sk_buff *skb, u8 f_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_REJ;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= f_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE;  /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_rnr_rsp - builds RNR response pdu
 *	@pdu_frame: Address of the frame to build
 *	@f_bit: The F bit to set in the PDU
 *	@nr: The seq. number of the expected data PDU from the remote
 *
 *	Builds a pdu frame as an RNR response.
 */
int llc_pdu_init_as_rnr_rsp(struct sk_buff *skb, u8 f_bit, u8 nr)
{
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_S;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_RNR;
	pdu->ctrl_2  = 0;
	pdu->ctrl_2 |= f_bit & LLC_S_PF_BIT_MASK;
	pdu->ctrl_1 &= 0x0F;    /* setting bits 5..8 to zero(reserved) */
	pdu->ctrl_2 |= (nr << 1) & 0xFE;  /* set N(R) in bits 10..16 */
	return 0;
}

/**
 *	pdu_init_as_ua_rsp - builds UA response pdu
 *	@skb: Address of the frame to build
 *	@f_bit: The F bit to set in the PDU
 *
 *	Builds a pdu frame as a UA response.
 */
int llc_pdu_init_as_ua_rsp(struct sk_buff *skb, u8 f_bit)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_2_PDU_RSP_UA;
	pdu->ctrl_1 |= ((f_bit & 1) << 4) & LLC_U_PF_BIT_MASK;
	return 0;
}

/**
 *	llc_pdu_decode_pdu_type - designates PDU type
 *	@skb: input skb that type of it must be designated.
 *	@type: type of PDU (output argument).
 *
 *	This function designates type of PDU (I,S or U).
 */
static int llc_pdu_decode_pdu_type(struct sk_buff *skb, u8 *type)
{
	llc_pdu_un_t *pdu = (llc_pdu_un_t *)skb->nh.raw;

	if (pdu->ctrl_1 & 1) {
		if ((pdu->ctrl_1 & LLC_PDU_TYPE_U) == LLC_PDU_TYPE_U)
			*type = LLC_PDU_TYPE_U;
		else
			*type = LLC_PDU_TYPE_S;
	} else
		*type = LLC_PDU_TYPE_I;
	return 0;
}

/**
 *	llc_decode_pdu_type - designates component LLC must handle for PDU
 *	@skb: input skb
 *	@dest: destination component
 *
 *	This function designates which component of LLC must handle this PDU.
 */
int llc_decode_pdu_type(struct sk_buff *skb, u8 *dest)
{
	u8 type = LLC_DEST_CONN; /* I-PDU or S-PDU type */
	llc_pdu_sn_t *pdu = (llc_pdu_sn_t *)skb->nh.raw;

	if ((pdu->ctrl_1 & LLC_PDU_TYPE_MASK) != LLC_PDU_TYPE_U)
		goto out;
	switch (LLC_U_PDU_CMD(pdu)) {
		case LLC_1_PDU_CMD_XID:
		case LLC_1_PDU_CMD_UI:
		case LLC_1_PDU_CMD_TEST:
			type = LLC_DEST_SAP;
			break;
		case LLC_2_PDU_CMD_SABME:
		case LLC_2_PDU_CMD_DISC:
		case LLC_2_PDU_RSP_UA:
		case LLC_2_PDU_RSP_DM:
		case LLC_2_PDU_RSP_FRMR:
			break;
		default:
			type = LLC_DEST_INVALID;
			break;
	}
out:
	*dest = type;
	return 0;
}

/**
 *	get_llc_hdr_len - designates LLC header length
 *	@pdu_type: type of PDU.
 *
 *	This function designates LLC header length of PDU. header length for I
 *	and S PDU is 4 and for U is 3 bytes. Returns the length of header.
 */
static int llc_get_llc_hdr_length(u8 pdu_type)
{
	int rtn_val = 0;

	switch (pdu_type) {
		case LLC_PDU_TYPE_I:
		case LLC_PDU_TYPE_S:
			rtn_val = 4;
			break;
		case LLC_PDU_TYPE_U:
			rtn_val = 3;
			break;
	}
	return rtn_val;
}

/**
 *	llc_pdu_get_pf_bit - extracts p/f bit of input PDU
 *	@pdu: pointer to LLC header.
 *
 *	This function extracts p/f bit of input PDU. at first examines type of
 *	PDU and then extracts p/f bit. Returns the p/f bit.
 */
static u8 llc_pdu_get_pf_bit(llc_pdu_sn_t *pdu)
{
	u8 pdu_type;
	u8 pf_bit = 0;

	if (pdu->ctrl_1 & 1) {
		if ((pdu->ctrl_1 & LLC_PDU_TYPE_U) == LLC_PDU_TYPE_U)
			pdu_type = LLC_PDU_TYPE_U;
		else
			pdu_type = LLC_PDU_TYPE_S;
	} else
		pdu_type = LLC_PDU_TYPE_I;
	switch (pdu_type) {
		case LLC_PDU_TYPE_I:
		case LLC_PDU_TYPE_S:
			pf_bit = pdu->ctrl_2 & LLC_S_PF_BIT_MASK;
			break;
		case LLC_PDU_TYPE_U:
			pf_bit = (pdu->ctrl_1 & LLC_U_PF_BIT_MASK) >> 4;
			break;
	}
	return pf_bit;
}
