#ifndef LLC_STATION_H
#define LLC_STATION_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <linux/skbuff.h>

#define LLC_EVENT		 1
#define LLC_PACKET		 2
#define LLC_TYPE_1		 1
#define LLC_TYPE_2		 2
#define LLC_P_TIME		 2
#define LLC_ACK_TIME		 1
#define LLC_REJ_TIME		 3
#define LLC_BUSY_TIME		 3

/**
 * struct llc_station - LLC station component
 *
 * SAP and connection resource manager, one per adapter.
 *
 * @state - state of station
 * @xid_r_count - XID response PDU counter
 * @mac_sa - MAC source address
 * @sap_list - list of related SAPs
 * @ev_q - events entering state mach.
 * @mac_pdu_q - PDUs ready to send to MAC
 */
struct llc_station {
	u8			    state;
	u8			    xid_r_count;
	struct timer_list	    ack_timer;
	u8			    retry_count;
	u8			    maximum_retry;
	struct {
		struct sk_buff_head list;
		spinlock_t	    lock;
	} ev_q;
	struct sk_buff_head	    mac_pdu_q;
};

extern int __init llc_station_init(void);
extern void __exit llc_station_exit(void);
#endif /* LLC_STATION_H */
