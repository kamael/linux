/*
 * Generic HDLC support routines for Linux
 * HDLC support
 *
 * Copyright (C) 1999 - 2001 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/hdlc.h>


static void raw_rx(struct sk_buff *skb)
{
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
}



int hdlc_raw_ioctl(hdlc_device *hdlc, struct ifreq *ifr)
{
	const size_t size = sizeof(hdlc_proto);
	struct net_device *dev = hdlc_to_dev(hdlc);
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		ifr->ifr_settings.type = IF_PROTO_HDLC;
		if (ifr->ifr_settings.data_length == 0)
			return 0; /* return protocol only */
		if (ifr->ifr_settings.data_length < size)
			return -ENOMEM;	/* buffer too small */
		if (copy_to_user(ifr->ifr_settings.data,
				 &hdlc->state.raw_hdlc.settings, size))
			return -EFAULT;
		ifr->ifr_settings.data_length = size;
		return 0;

	case IF_PROTO_HDLC:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
			return -EBUSY;

		if (ifr->ifr_settings.data_length != size)
			return -ENOMEM;	/* incorrect data length */

		if (copy_from_user(&hdlc->state.raw_hdlc.settings,
				   ifr->ifr_settings.data, size))
			return -EFAULT;

		/* FIXME - put sanity checks here */
		hdlc_detach(hdlc);

		result=hdlc->attach(hdlc, hdlc->state.raw_hdlc.settings.encoding,
				    hdlc->state.raw_hdlc.settings.parity);
		if (result) {
			hdlc->proto = -1;
			return result;
		}

		hdlc->open = NULL;
		hdlc->stop = NULL;
		hdlc->netif_rx = raw_rx;
		hdlc->proto = IF_PROTO_HDLC;
		dev->hard_start_xmit = hdlc->xmit;
		dev->hard_header = NULL;
		dev->type = ARPHRD_RAWHDLC;
		dev->addr_len = 0;
		return 0;
	}

	return -EINVAL;
}
