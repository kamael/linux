/*
 * Moxa C101 synchronous serial card driver for Linux
 *
 * Copyright (C) 2000-2003 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * For information see http://hq.pm.waw.pl/hdlc/
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    Moxa C101 User's Manual
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "hd64570.h"


static const char* version = "Moxa C101 driver version: 1.15";
static const char* devname = "C101";

#undef DEBUG_PKT
#define DEBUG_RINGS

#define C101_PAGE 0x1D00
#define C101_DTR 0x1E00
#define C101_SCA 0x1F00
#define C101_WINDOW_SIZE 0x2000
#define C101_MAPPED_RAM_SIZE 0x4000

#define RAM_SIZE (256 * 1024)
#define TX_RING_BUFFERS 10
#define RX_RING_BUFFERS ((RAM_SIZE - C101_WINDOW_SIZE) /		\
			 (sizeof(pkt_desc) + HDLC_MAX_MRU) - TX_RING_BUFFERS)

#define CLOCK_BASE 9830400	/* 9.8304 MHz */
#define PAGE0_ALWAYS_MAPPED

static char *hw;		/* pointer to hw=xxx command line string */


typedef struct card_s {
	hdlc_device hdlc;	/* HDLC device struct - must be first */
	spinlock_t lock;	/* TX lock */
	u8 *win0base;		/* ISA window base address */
	u32 phy_winbase;	/* ISA physical base address */
	sync_serial_settings settings;
	int rxpart;		/* partial frame received, next frame invalid*/
	unsigned short encoding;
	unsigned short parity;
	u16 rx_ring_buffers;	/* number of buffers in a ring */
	u16 tx_ring_buffers;
	u16 buff_offset;	/* offset of first buffer of first channel */
	u16 rxin;		/* rx ring buffer 'in' pointer */
	u16 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u16 txlast;
	u8 rxs, txs, tmc;	/* SCA registers */
	u8 irq;			/* IRQ (3-15) */
	u8 page;

	struct card_s *next_card;
}card_t;

typedef card_t port_t;

static card_t *first_card;
static card_t **new_card = &first_card;


#define sca_in(reg, card)	   readb((card)->win0base + C101_SCA + (reg))
#define sca_out(value, reg, card)  writeb(value, (card)->win0base + C101_SCA + (reg))
#define sca_inw(reg, card)	   readw((card)->win0base + C101_SCA + (reg))

/* EDA address register must be set in EDAL, EDAH order - 8 bit ISA bus */
#define sca_outw(value, reg, card) do { \
	writeb(value & 0xFF, (card)->win0base + C101_SCA + (reg)); \
	writeb((value >> 8 ) & 0xFF, (card)->win0base + C101_SCA + (reg+1));\
} while(0)

#define port_to_card(port)	   (port)
#define log_node(port)		   (0)
#define phy_node(port)		   (0)
#define winsize(card)		   (C101_WINDOW_SIZE)
#define win0base(card)		   ((card)->win0base)
#define winbase(card)      	   ((card)->win0base + 0x2000)
#define get_port(card, port)	   (card)
static void sca_msci_intr(port_t *port);


static inline u8 sca_get_page(card_t *card)
{
	return card->page;
}

static inline void openwin(card_t *card, u8 page)
{
	card->page = page;
	writeb(page, card->win0base + C101_PAGE);
}


#define close_windows(card) {} /* no hardware support */


#include "hd6457x.c"


static void sca_msci_intr(port_t *port)
{
	card_t* card = port_to_card(port);
	u8 stat = sca_in(MSCI1_OFFSET + ST1, card); /* read MSCI ST1 status */

	/* Reset MSCI TX underrun status bit */
	sca_out(stat & ST1_UDRN, MSCI0_OFFSET + ST1, card);

	if (stat & ST1_UDRN) {
		port->hdlc.stats.tx_errors++; /* TX Underrun error detected */
		port->hdlc.stats.tx_fifo_errors++;
	}

	/* Reset MSCI CDCD status bit - uses ch#2 DCD input */
	sca_out(stat & ST1_CDCD, MSCI1_OFFSET + ST1, card);

	if (stat & ST1_CDCD)
		hdlc_set_carrier(!(sca_in(MSCI1_OFFSET + ST3, card) & ST3_DCD),
				 &port->hdlc);
}


static void c101_set_iface(port_t *port)
{
	u8 rxs = port->rxs & CLK_BRG_MASK;
	u8 txs = port->txs & CLK_BRG_MASK;

	switch(port->settings.clock_type) {
	case CLOCK_INT:
		rxs |= CLK_BRG_RX; /* TX clock */
		txs |= CLK_RXCLK_TX; /* BRG output */
		break;

	case CLOCK_TXINT:
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_BRG_TX; /* BRG output */
		break;

	case CLOCK_TXFROMRX:
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_RXCLK_TX; /* RX clock */
		break;

	default:	/* EXTernal clock */
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_LINE_TX; /* TXC input */
	}

	port->rxs = rxs;
	port->txs = txs;
	sca_out(rxs, MSCI1_OFFSET + RXS, port);
	sca_out(txs, MSCI1_OFFSET + TXS, port);
	sca_set_port(port);
}


static int c101_open(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);
	int result;

	result = hdlc_open(dev);
	if (result)
		return result;

	writeb(1, port->win0base + C101_DTR);
	sca_out(0, MSCI1_OFFSET + CTL, port); /* RTS uses ch#2 output */
	sca_open(hdlc);
	/* DCD is connected to port 2 !@#$%^& - disable MSCI0 CDCD interrupt */
	sca_out(IE1_UDRN, MSCI0_OFFSET + IE1, port);
	sca_out(IE0_TXINT, MSCI0_OFFSET + IE0, port);

	hdlc_set_carrier(!(sca_in(MSCI1_OFFSET + ST3, port) & ST3_DCD), hdlc);
	printk(KERN_DEBUG "0x%X\n", sca_in(MSCI1_OFFSET + ST3, port));

	/* enable MSCI1 CDCD interrupt */
	sca_out(IE1_CDCD, MSCI1_OFFSET + IE1, port);
	sca_out(IE0_RXINTA, MSCI1_OFFSET + IE0, port);
	sca_out(0x48, IER0, port); /* TXINT #0 and RXINT #1 */
	c101_set_iface(port);
	return 0;
}


static int c101_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);

	sca_close(hdlc);
	writeb(0, port->win0base + C101_DTR);
	sca_out(CTL_NORTS, MSCI1_OFFSET + CTL, port);
	hdlc_close(hdlc);
	return 0;
}


static int c101_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line, *line = ifr->ifr_settings.ifs_ifsu.sync;
	hdlc_device *hdlc = dev_to_hdlc(dev);
	port_t *port = hdlc_to_port(hdlc);

#ifdef DEBUG_RINGS
	if (cmd == SIOCDEVPRIVATE) {
		sca_dump_rings(hdlc);
		printk(KERN_DEBUG "MSCI1: ST: %02x %02x %02x %02x\n",
		       sca_in(MSCI1_OFFSET + ST0, port),
		       sca_in(MSCI1_OFFSET + ST1, port),
		       sca_in(MSCI1_OFFSET + ST2, port),
		       sca_in(MSCI1_OFFSET + ST3, port));
		return 0;
	}
#endif
	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE:
		ifr->ifr_settings.type = IF_IFACE_SYNC_SERIAL;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(line, &port->settings, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&new_line, line, size))
			return -EFAULT;

		if (new_line.clock_type != CLOCK_EXT &&
		    new_line.clock_type != CLOCK_TXFROMRX &&
		    new_line.clock_type != CLOCK_INT &&
		    new_line.clock_type != CLOCK_TXINT)
		return -EINVAL;	/* No such clock setting */

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		memcpy(&port->settings, &new_line, size); /* Update settings */
		c101_set_iface(port);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}



static void c101_destroy_card(card_t *card)
{
	readb(card->win0base + C101_PAGE); /* Resets SCA? */

	if (card->irq)
		free_irq(card->irq, card);

	if (card->win0base) {
		iounmap(card->win0base);
		release_mem_region(card->phy_winbase, C101_MAPPED_RAM_SIZE);
	}

	kfree(card);
}



static int __init c101_run(unsigned long irq, unsigned long winbase)
{
	struct net_device *dev;
	card_t *card;
	int result;

	if (irq<3 || irq>15 || irq == 6) /* FIXME */ {
		printk(KERN_ERR "c101: invalid IRQ value\n");
		return -ENODEV;
	}

	if (winbase < 0xC0000 || winbase > 0xDFFFF || (winbase & 0x3FFF) !=0) {
		printk(KERN_ERR "c101: invalid RAM value\n");
		return -ENODEV;
	}

	card = kmalloc(sizeof(card_t), GFP_KERNEL);
	if (card == NULL) {
		printk(KERN_ERR "c101: unable to allocate memory\n");
		return -ENOBUFS;
	}
	memset(card, 0, sizeof(card_t));

	if (request_irq(irq, sca_intr, 0, devname, card)) {
		printk(KERN_ERR "c101: could not allocate IRQ\n");
		c101_destroy_card(card);
		return(-EBUSY);
	}
	card->irq = irq;

	if (!request_mem_region(winbase, C101_MAPPED_RAM_SIZE, devname)) {
		printk(KERN_ERR "c101: could not request RAM window\n");
		c101_destroy_card(card);
		return(-EBUSY);
	}
	card->phy_winbase = winbase;
	card->win0base = ioremap(winbase, C101_MAPPED_RAM_SIZE);
	if (!card->win0base) {
		printk(KERN_ERR "c101: could not map I/O address\n");
		c101_destroy_card(card);
		return -EBUSY;
	}

	card->tx_ring_buffers = TX_RING_BUFFERS;
	card->rx_ring_buffers = RX_RING_BUFFERS;
	card->buff_offset = C101_WINDOW_SIZE; /* Bytes 1D00-1FFF reserved */

	readb(card->win0base + C101_PAGE); /* Resets SCA? */
	udelay(100);
	writeb(0, card->win0base + C101_PAGE);
	writeb(0, card->win0base + C101_DTR); /* Power-up for RAM? */

	sca_init(card, 0);

	dev = hdlc_to_dev(&card->hdlc);

	spin_lock_init(&card->lock);
	SET_MODULE_OWNER(dev);
	dev->irq = irq;
	dev->mem_start = winbase;
	dev->mem_end = winbase + C101_MAPPED_RAM_SIZE - 1;
	dev->tx_queue_len = 50;
	dev->do_ioctl = c101_ioctl;
	dev->open = c101_open;
	dev->stop = c101_close;
	card->hdlc.attach = sca_attach;
	card->hdlc.xmit = sca_xmit;
	card->settings.clock_type = CLOCK_EXT;

	result = register_hdlc_device(&card->hdlc);
	if (result) {
		printk(KERN_WARNING "c101: unable to register hdlc device\n");
		c101_destroy_card(card);
		return result;
	}

	sca_init_sync_port(card); /* Set up C101 memory */
	hdlc_set_carrier(!(sca_in(MSCI1_OFFSET + ST3, card) & ST3_DCD),
			 &card->hdlc);

	printk(KERN_INFO "%s: Moxa C101 on IRQ%u,"
	       " using %u TX + %u RX packets rings\n",
	       hdlc_to_name(&card->hdlc), card->irq,
	       card->tx_ring_buffers, card->rx_ring_buffers);

	*new_card = card;
	new_card = &card->next_card;
	return 0;
}



static int __init c101_init(void)
{
	if (hw == NULL) {
#ifdef MODULE
		printk(KERN_INFO "c101: no card initialized\n");
#endif
		return -ENOSYS;	/* no parameters specified, abort */
	}

	printk(KERN_INFO "%s\n", version);

	do {
		unsigned long irq, ram;

		irq = simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		ram = simple_strtoul(hw, &hw, 0);

		if (*hw == ':' || *hw == '\x0')
			c101_run(irq, ram);

		if (*hw == '\x0')
			return first_card ? 0 : -ENOSYS;
	}while(*hw++ == ':');

	printk(KERN_ERR "c101: invalid hardware parameters\n");
	return first_card ? 0 : -ENOSYS;
}


static void __exit c101_cleanup(void)
{
	card_t *card = first_card;

	while (card) {
		card_t *ptr = card;
		card = card->next_card;
		unregister_hdlc_device(&ptr->hdlc);
		c101_destroy_card(ptr);
	}
}


module_init(c101_init);
module_exit(c101_cleanup);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Moxa C101 serial port driver");
MODULE_LICENSE("GPL v2");
module_param(hw, charp, 0444);	/* hw=irq,ram:irq,... */
