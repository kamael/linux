/*
	Written 1998-2000 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support information and updates available at
	http://www.scyld.com/network/pci-skeleton.html

	Linux kernel updates:

	Version 2.51, Nov 17, 2001 (jgarzik):
	- Add ethtool support
	- Replace some MII-related magic numbers with constants

*/

#define DRV_NAME	"fealnx"
#define DRV_VERSION	"2.51"
#define DRV_RELDATE	"Nov-17-2001"

static int debug;		/* 1-> print debug message */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast). */
static int multicast_filter_limit = 32;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme. */
/* Setting to > 1518 effectively disables this feature.          */
static int rx_copybreak;

/* Used to pass the media type, etc.                            */
/* Both 'options[]' and 'full_duplex[]' should exist for driver */
/* interoperability.                                            */
/* The media type is usually passed in 'options[]'.             */
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int full_duplex[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };

/* Operational parameters that are set at compile time.                 */
/* Keep the ring sizes a power of two for compile efficiency.           */
/* The compiler will convert <unsigned>'%'<2^N> into a bit mask.        */
/* Making the Tx ring too large decreases the effectiveness of channel  */
/* bonding and packet priority.                                         */
/* There are no ill effects from too-large receive rings.               */
// 88-12-9 modify,
// #define TX_RING_SIZE    16
// #define RX_RING_SIZE    32
#define TX_RING_SIZE    6
#define RX_RING_SIZE    12
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct fealnx_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct fealnx_desc)

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT      (2*HZ)

#define PKT_BUF_SZ      1536	/* Size of each temporary Rx buffer. */


/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>

#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE "\n";


/* This driver was written to use PCI memory space, however some x86 systems
   work only with I/O space accesses. */
#ifndef __alpha__
#define USE_IO_OPS
#endif

#ifdef USE_IO_OPS
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb inb
#define readw inw
#define readl inl
#define writeb outb
#define writew outw
#define writel outl
#endif

/* Kernel compatibility defines, some common to David Hinds' PCMCIA package. */
/* This is only in the support-all-kernels source code. */

#define RUN_AT(x) (jiffies + (x))

MODULE_AUTHOR("Myson or whoever");
MODULE_DESCRIPTION("Myson MTD-8xx 100/10M Ethernet PCI Adapter Driver");
MODULE_LICENSE("GPL");
MODULE_PARM(max_interrupt_work, "i");
//MODULE_PARM(min_pci_latency, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM_DESC(max_interrupt_work, "fealnx maximum events handled per interrupt");
MODULE_PARM_DESC(debug, "fealnx enable debugging (0-1)");
MODULE_PARM_DESC(rx_copybreak, "fealnx copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(multicast_filter_limit, "fealnx maximum number of filtered multicast addresses");
MODULE_PARM_DESC(options, "fealnx: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "fealnx full duplex setting(s) (1)");

#define MIN_REGION_SIZE 136

enum pci_flags_bit {
	PCI_USES_IO = 1,
	PCI_USES_MEM = 2,
	PCI_USES_MASTER = 4,
	PCI_ADDR0 = 0x10 << 0,
	PCI_ADDR1 = 0x10 << 1,
	PCI_ADDR2 = 0x10 << 2,
	PCI_ADDR3 = 0x10 << 3,
};

/* A chip capabilities table, matching the entries in pci_tbl[] above. */
enum chip_capability_flags {
	HAS_MII_XCVR,
	HAS_CHIP_XCVR,
};

/* 89/6/13 add, */
/* for different PHY */
enum phy_type_flags {
	MysonPHY = 1,
	AhdocPHY = 2,
	SeeqPHY = 3,
	MarvellPHY = 4,
	Myson981 = 5,
	LevelOnePHY = 6,
	OtherPHY = 10,
};

struct chip_info {
	char *chip_name;
	int io_size;
	int flags;
};

static struct chip_info skel_netdrv_tbl[] = {
	{"100/10M Ethernet PCI Adapter", 136, HAS_MII_XCVR},
	{"100/10M Ethernet PCI Adapter", 136, HAS_CHIP_XCVR},
	{"1000/100/10M Ethernet PCI Adapter", 136, HAS_MII_XCVR},
};

/* Offsets to the Command and Status Registers. */
enum fealnx_offsets {
	PAR0 = 0x0,		/* physical address 0-3 */
	PAR1 = 0x04,		/* physical address 4-5 */
	MAR0 = 0x08,		/* multicast address 0-3 */
	MAR1 = 0x0C,		/* multicast address 4-7 */
	FAR0 = 0x10,		/* flow-control address 0-3 */
	FAR1 = 0x14,		/* flow-control address 4-5 */
	TCRRCR = 0x18,		/* receive & transmit configuration */
	BCR = 0x1C,		/* bus command */
	TXPDR = 0x20,		/* transmit polling demand */
	RXPDR = 0x24,		/* receive polling demand */
	RXCWP = 0x28,		/* receive current word pointer */
	TXLBA = 0x2C,		/* transmit list base address */
	RXLBA = 0x30,		/* receive list base address */
	ISR = 0x34,		/* interrupt status */
	IMR = 0x38,		/* interrupt mask */
	FTH = 0x3C,		/* flow control high/low threshold */
	MANAGEMENT = 0x40,	/* bootrom/eeprom and mii management */
	TALLY = 0x44,		/* tally counters for crc and mpa */
	TSR = 0x48,		/* tally counter for transmit status */
	BMCRSR = 0x4c,		/* basic mode control and status */
	PHYIDENTIFIER = 0x50,	/* phy identifier */
	ANARANLPAR = 0x54,	/* auto-negotiation advertisement and link
				   partner ability */
	ANEROCR = 0x58,		/* auto-negotiation expansion and pci conf. */
	BPREMRPSR = 0x5c,	/* bypass & receive error mask and phy status */
};

/* Bits in the interrupt status/enable registers. */
/* The bits in the Intr Status/Enable registers, mostly interrupt sources. */
enum intr_status_bits {
	RFCON = 0x00020000,	/* receive flow control xon packet */
	RFCOFF = 0x00010000,	/* receive flow control xoff packet */
	LSCStatus = 0x00008000,	/* link status change */
	ANCStatus = 0x00004000,	/* autonegotiation completed */
	FBE = 0x00002000,	/* fatal bus error */
	FBEMask = 0x00001800,	/* mask bit12-11 */
	ParityErr = 0x00000000,	/* parity error */
	TargetErr = 0x00001000,	/* target abort */
	MasterErr = 0x00000800,	/* master error */
	TUNF = 0x00000400,	/* transmit underflow */
	ROVF = 0x00000200,	/* receive overflow */
	ETI = 0x00000100,	/* transmit early int */
	ERI = 0x00000080,	/* receive early int */
	CNTOVF = 0x00000040,	/* counter overflow */
	RBU = 0x00000020,	/* receive buffer unavailable */
	TBU = 0x00000010,	/* transmit buffer unavilable */
	TI = 0x00000008,	/* transmit interrupt */
	RI = 0x00000004,	/* receive interrupt */
	RxErr = 0x00000002,	/* receive error */
};

/* Bits in the NetworkConfig register. */
enum rx_mode_bits {
	RxModeMask = 0xe0,
	PROM = 0x80,		/* promiscuous mode */
	AB = 0x40,		/* accept broadcast */
	AM = 0x20,		/* accept mutlicast */
	ARP = 0x08,		/* receive runt pkt */
	ALP = 0x04,		/* receive long pkt */
	SEP = 0x02,		/* receive error pkt */
};

/* The Tulip Rx and Tx buffer descriptors. */
struct fealnx_desc {
	s32 status;
	s32 control;
	u32 buffer;
	u32 next_desc;
	struct fealnx_desc *next_desc_logical;
	struct sk_buff *skbuff;
	u32 reserved1;
	u32 reserved2;
};

/* Bits in network_desc.status */
enum rx_desc_status_bits {
	RXOWN = 0x80000000,	/* own bit */
	FLNGMASK = 0x0fff0000,	/* frame length */
	FLNGShift = 16,
	MARSTATUS = 0x00004000,	/* multicast address received */
	BARSTATUS = 0x00002000,	/* broadcast address received */
	PHYSTATUS = 0x00001000,	/* physical address received */
	RXFSD = 0x00000800,	/* first descriptor */
	RXLSD = 0x00000400,	/* last descriptor */
	ErrorSummary = 0x80,	/* error summary */
	RUNT = 0x40,		/* runt packet received */
	LONG = 0x20,		/* long packet received */
	FAE = 0x10,		/* frame align error */
	CRC = 0x08,		/* crc error */
	RXER = 0x04,		/* receive error */
};

enum rx_desc_control_bits {
	RXIC = 0x00800000,	/* interrupt control */
	RBSShift = 0,
};

enum tx_desc_status_bits {
	TXOWN = 0x80000000,	/* own bit */
	JABTO = 0x00004000,	/* jabber timeout */
	CSL = 0x00002000,	/* carrier sense lost */
	LC = 0x00001000,	/* late collision */
	EC = 0x00000800,	/* excessive collision */
	UDF = 0x00000400,	/* fifo underflow */
	DFR = 0x00000200,	/* deferred */
	HF = 0x00000100,	/* heartbeat fail */
	NCRMask = 0x000000ff,	/* collision retry count */
	NCRShift = 0,
};

enum tx_desc_control_bits {
	TXIC = 0x80000000,	/* interrupt control */
	ETIControl = 0x40000000,	/* early transmit interrupt */
	TXLD = 0x20000000,	/* last descriptor */
	TXFD = 0x10000000,	/* first descriptor */
	CRCEnable = 0x08000000,	/* crc control */
	PADEnable = 0x04000000,	/* padding control */
	RetryTxLC = 0x02000000,	/* retry late collision */
	PKTSMask = 0x3ff800,	/* packet size bit21-11 */
	PKTSShift = 11,
	TBSMask = 0x000007ff,	/* transmit buffer bit 10-0 */
	TBSShift = 0,
};

/* BootROM/EEPROM/MII Management Register */
#define MASK_MIIR_MII_READ       0x00000000
#define MASK_MIIR_MII_WRITE      0x00000008
#define MASK_MIIR_MII_MDO        0x00000004
#define MASK_MIIR_MII_MDI        0x00000002
#define MASK_MIIR_MII_MDC        0x00000001

/* ST+OP+PHYAD+REGAD+TA */
#define OP_READ             0x6000	/* ST:01+OP:10+PHYAD+REGAD+TA:Z0 */
#define OP_WRITE            0x5002	/* ST:01+OP:01+PHYAD+REGAD+TA:10 */

/* ------------------------------------------------------------------------- */
/*      Constants for Myson PHY                                              */
/* ------------------------------------------------------------------------- */
#define MysonPHYID      0xd0000302
/* 89-7-27 add, (begin) */
#define MysonPHYID0     0x0302
#define StatusRegister  18
#define SPEED100        0x0400	// bit10
#define FULLMODE        0x0800	// bit11
/* 89-7-27 add, (end) */

/* ------------------------------------------------------------------------- */
/*      Constants for Seeq 80225 PHY                                         */
/* ------------------------------------------------------------------------- */
#define SeeqPHYID0      0x0016

#define MIIRegister18   18
#define SPD_DET_100     0x80
#define DPLX_DET_FULL   0x40

/* ------------------------------------------------------------------------- */
/*      Constants for Ahdoc 101 PHY                                          */
/* ------------------------------------------------------------------------- */
#define AhdocPHYID0     0x0022

#define DiagnosticReg   18
#define DPLX_FULL       0x0800
#define Speed_100       0x0400

/* 89/6/13 add, */
/* -------------------------------------------------------------------------- */
/*      Constants                                                             */
/* -------------------------------------------------------------------------- */
#define MarvellPHYID0           0x0141
#define LevelOnePHYID0		0x0013

#define MII1000BaseTControlReg  9
#define MII1000BaseTStatusReg   10
#define SpecificReg		17

/* for 1000BaseT Control Register */
#define PHYAbletoPerform1000FullDuplex  0x0200
#define PHYAbletoPerform1000HalfDuplex  0x0100
#define PHY1000AbilityMask              0x300

// for phy specific status register, marvell phy.
#define SpeedMask       0x0c000
#define Speed_1000M     0x08000
#define Speed_100M      0x4000
#define Speed_10M       0
#define Full_Duplex     0x2000

// 89/12/29 add, for phy specific status register, levelone phy, (begin)
#define LXT1000_100M    0x08000
#define LXT1000_1000M   0x0c000
#define LXT1000_Full    0x200
// 89/12/29 add, for phy specific status register, levelone phy, (end)

/* for 3-in-1 case */
#define PS10            0x00080000
#define FD              0x00100000
#define PS1000          0x00010000
#define LinkIsUp2	0x00040000

/* for PHY */
#define LinkIsUp        0x0004


struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct fealnx_desc *rx_ring;
	struct fealnx_desc *tx_ring;

	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;

	spinlock_t lock;

	struct net_device_stats stats;

	/* Media monitoring timer. */
	struct timer_list timer;

	/* Frequently used values: keep some adjacent for cache effect. */
	int flags;
	struct pci_dev *pci_dev;
	unsigned long crvalue;
	unsigned long bcrvalue;
	unsigned long imrvalue;
	struct fealnx_desc *cur_rx;
	struct fealnx_desc *lack_rxbuf;
	int really_rx_count;
	struct fealnx_desc *cur_tx;
	struct fealnx_desc *cur_tx_copy;
	int really_tx_count;
	int free_tx_count;
	unsigned int rx_buf_sz;	/* Based on MTU+slack. */

	/* These values are keep track of the transceiver/media in use. */
	unsigned int linkok;
	unsigned int line_speed;
	unsigned int duplexmode;
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int PHYType;

	/* MII transceiver section. */
	int mii_cnt;		/* MII device addresses. */
	unsigned char phys[2];	/* MII device addresses. */
	struct mii_if_info mii;
};


static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int netdev_open(struct net_device *dev);
static void getlinktype(struct net_device *dev);
static void getlinkstatus(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct net_device *dev);
static void init_ring(struct net_device *dev);
static int start_tx(struct sk_buff *skb, struct net_device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static int netdev_rx(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int netdev_close(struct net_device *dev);
static void reset_rx_descriptors(struct net_device *dev);

void stop_nic_tx(long ioaddr, long crvalue)
{
	writel(crvalue & (~0x40000), ioaddr + TCRRCR);

	/* wait for tx stop */
	{
		int i = 0, delay = 0x1000;

		while ((!(readl(ioaddr + TCRRCR) & 0x04000000)) && (i < delay)) {
			++i;
		}
	}
}


void stop_nic_rx(long ioaddr, long crvalue)
{
	writel(crvalue & (~0x1), ioaddr + TCRRCR);

	/* wait for rx stop */
	{
		int i = 0, delay = 0x1000;

		while ((!(readl(ioaddr + TCRRCR) & 0x00008000)) && (i < delay)) {
			++i;
		}
	}
}



static int __devinit fealnx_init_one(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct netdev_private *np;
	int i, option, err, irq;
	static int card_idx = -1;
	char boardname[12];
	long ioaddr;
	unsigned int chip_id = ent->driver_data;
	struct net_device *dev;
	void *ring_space;
	dma_addr_t ring_dma;
	
/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk (version);
#endif
	
	card_idx++;
	sprintf(boardname, "fealnx%d", card_idx);
	
	option = card_idx < MAX_UNITS ? options[card_idx] : 0;

	i = pci_enable_device(pdev);
	if (i) return i;
	pci_set_master(pdev);
	
#ifdef USE_IO_OPS
	ioaddr = pci_resource_len(pdev, 0);
#else
	ioaddr = pci_resource_len(pdev, 1);
#endif
	if (ioaddr < MIN_REGION_SIZE) {
		printk(KERN_ERR "%s: region size %ld too small, aborting\n",
		       boardname, ioaddr);
		return -ENODEV;
	}

	i = pci_request_regions(pdev, boardname);
	if (i) return i;
	
	irq = pdev->irq;

#ifdef USE_IO_OPS
	ioaddr = pci_resource_start(pdev, 0);
#else
	ioaddr = (long) ioremap(pci_resource_start(pdev, 1),
				pci_resource_len(pdev, 1));
	if (!ioaddr) {
		err = -ENOMEM;
		goto err_out_res;
	}
#endif
	
	dev = alloc_etherdev(sizeof(struct netdev_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_out_unmap;
	}
	SET_MODULE_OWNER(dev);

	/* read ethernet id */
	for (i = 0; i < 6; ++i)
		dev->dev_addr[i] = readb(ioaddr + PAR0 + i);

	/* Reset the chip to erase previous misconfiguration. */
	writel(0x00000001, ioaddr + BCR);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the descriptor lists are aligned. */
	np = dev->priv;
	spin_lock_init(&np->lock);
	np->pci_dev = pdev;
	np->flags = skel_netdrv_tbl[chip_id].flags;
	pci_set_drvdata(pdev, dev);
	np->mii.dev = dev;
	np->mii.mdio_read = mdio_read;
	np->mii.mdio_write = mdio_write;
	np->mii.phy_id_mask = 0x1f;
	np->mii.reg_num_mask = 0x1f;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space) {
		err = -ENOMEM;
		goto err_out_free_dev;
	}
	np->rx_ring = (struct fealnx_desc *)ring_space;
	np->rx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space) {
		err = -ENOMEM;
		goto err_out_free_rx;
	}
	np->tx_ring = (struct fealnx_desc *)ring_space;
	np->tx_ring_dma = ring_dma;

	/* find the connected MII xcvrs */
	if (np->flags == HAS_MII_XCVR) {
		int phy, phy_idx = 0;

		for (phy = 1; phy < 32 && phy_idx < 4; phy++) {
			int mii_status = mdio_read(dev, phy, 1);

			if (mii_status != 0xffff && mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				printk(KERN_INFO
				       "%s: MII PHY found at address %d, status "
				       "0x%4.4x.\n", dev->name, phy, mii_status);
				/* get phy type */
				{
					unsigned int data;

					data = mdio_read(dev, np->phys[0], 2);
					if (data == SeeqPHYID0)
						np->PHYType = SeeqPHY;
					else if (data == AhdocPHYID0)
						np->PHYType = AhdocPHY;
					else if (data == MarvellPHYID0)
						np->PHYType = MarvellPHY;
					else if (data == MysonPHYID0)
						np->PHYType = Myson981;
					else if (data == LevelOnePHYID0)
						np->PHYType = LevelOnePHY;
					else
						np->PHYType = OtherPHY;
				}
			}
		}

		np->mii_cnt = phy_idx;
		if (phy_idx == 0) {
			printk(KERN_WARNING "%s: MII PHY not found -- this device may "
			       "not operate correctly.\n", dev->name);
		}
	} else {
		np->phys[0] = 32;
/* 89/6/23 add, (begin) */
		/* get phy type */
		if (readl(dev->base_addr + PHYIDENTIFIER) == MysonPHYID)
			np->PHYType = MysonPHY;
		else
			np->PHYType = OtherPHY;
	}
	np->mii.phy_id = np->phys[0];

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->mii.full_duplex = 1;
		np->default_port = option & 15;
	}

	if (card_idx < MAX_UNITS && full_duplex[card_idx] > 0)
		np->mii.full_duplex = full_duplex[card_idx];

	if (np->mii.full_duplex) {
		printk(KERN_INFO "%s: Media type forced to Full Duplex.\n", dev->name);
/* 89/6/13 add, (begin) */
//      if (np->PHYType==MarvellPHY)
		if ((np->PHYType == MarvellPHY) || (np->PHYType == LevelOnePHY)) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], 9);
			data = (data & 0xfcff) | 0x0200;
			mdio_write(dev, np->phys[0], 9, data);
		}
/* 89/6/13 add, (end) */
		if (np->flags == HAS_MII_XCVR)
			mdio_write(dev, np->phys[0], MII_ADVERTISE, ADVERTISE_FULL);
		else
			writel(ADVERTISE_FULL, dev->base_addr + ANARANLPAR);
		np->mii.force_media = 1;
	}

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;
	dev->tx_timeout = tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	
	err = register_netdev(dev);
	if (err)
		goto err_out_free_tx;

	printk(KERN_INFO "%s: %s at 0x%lx, ",
	       dev->name, skel_netdrv_tbl[chip_id].chip_name, ioaddr);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	return 0;

err_out_free_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring, np->tx_ring_dma);
err_out_free_rx:
	pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring, np->rx_ring_dma);
err_out_free_dev:
	kfree(dev);
err_out_unmap:
#ifndef USE_IO_OPS
	iounmap((void *)ioaddr);
err_out_res:
#endif
	pci_release_regions(pdev);
	return err;
}

static void __devexit fealnx_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct netdev_private *np = dev->priv;

		pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring,
			np->tx_ring_dma);
		pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring,
			np->rx_ring_dma);
		unregister_netdev(dev);
#ifndef USE_IO_OPS
		iounmap((void *)dev->base_addr);
#endif
		kfree(dev);
		pci_release_regions(pdev);
		pci_set_drvdata(pdev, NULL);
	} else
		printk(KERN_ERR "fealnx: remove for unknown device\n");
}

unsigned int m80x_read_tick(void)
/* function: Reads the Timer tick count register which decrements by 2 from  */
/*           65536 to 0 every 1/36.414 of a second. Each 2 decrements of the *//*           count represents 838 nsec's.                                    */
/* input   : none.                                                           */
/* output  : none.                                                           */
{
	unsigned char tmp;
	int value;

	writeb((char) 0x06, 0x43);	// Command 8254 to latch T0's count

	// now read the count.
	tmp = (unsigned char) readb(0x40);
	value = ((int) tmp) << 8;
	tmp = (unsigned char) readb(0x40);
	value |= (((int) tmp) & 0xff);
	return (value);
}


void m80x_delay(unsigned int interval)
/* function: to wait for a specified time.                                   */
/* input   : interval ... the specified time.                                */
/* output  : none.                                                           */
{
	unsigned int interval1, interval2, i = 0;

	interval1 = m80x_read_tick();	// get initial value
	do {
		interval2 = m80x_read_tick();
		if (interval1 < interval2)
			interval1 = interval2;
		++i;
	} while (((interval1 - interval2) < (ushort) interval) && (i < 65535));
}


static ulong m80x_send_cmd_to_phy(long miiport, int opcode, int phyad, int regad)
{
	ulong miir;
	int i;
	unsigned int mask, data;

	/* enable MII output */
	miir = (ulong) readl(miiport);
	miir &= 0xfffffff0;

	miir |= MASK_MIIR_MII_WRITE + MASK_MIIR_MII_MDO;

	/* send 32 1's preamble */
	for (i = 0; i < 32; i++) {
		/* low MDC; MDO is already high (miir) */
		miir &= ~MASK_MIIR_MII_MDC;
		writel(miir, miiport);

		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		writel(miir, miiport);
	}

	/* calculate ST+OP+PHYAD+REGAD+TA */
	data = opcode | (phyad << 7) | (regad << 2);

	/* sent out */
	mask = 0x8000;
	while (mask) {
		/* low MDC, prepare MDO */
		miir &= ~(MASK_MIIR_MII_MDC + MASK_MIIR_MII_MDO);
		if (mask & data)
			miir |= MASK_MIIR_MII_MDO;

		writel(miir, miiport);
		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		writel(miir, miiport);
		m80x_delay(30);

		/* next */
		mask >>= 1;
		if (mask == 0x2 && opcode == OP_READ)
			miir &= ~MASK_MIIR_MII_WRITE;
	}
	return miir;
}


static int mdio_read(struct net_device *dev, int phyad, int regad)
{
	long miiport = dev->base_addr + MANAGEMENT;
	ulong miir;
	unsigned int mask, data;

	miir = m80x_send_cmd_to_phy(miiport, OP_READ, phyad, regad);

	/* read data */
	mask = 0x8000;
	data = 0;
	while (mask) {
		/* low MDC */
		miir &= ~MASK_MIIR_MII_MDC;
		writel(miir, miiport);

		/* read MDI */
		miir = readl(miiport);
		if (miir & MASK_MIIR_MII_MDI)
			data |= mask;

		/* high MDC, and wait */
		miir |= MASK_MIIR_MII_MDC;
		writel(miir, miiport);
		m80x_delay((int) 30);

		/* next */
		mask >>= 1;
	}

	/* low MDC */
	miir &= ~MASK_MIIR_MII_MDC;
	writel(miir, miiport);

	return data & 0xffff;
}


static void mdio_write(struct net_device *dev, int phyad, int regad, int data)
{
	long miiport = dev->base_addr + MANAGEMENT;
	ulong miir;
	unsigned int mask;

	miir = m80x_send_cmd_to_phy(miiport, OP_WRITE, phyad, regad);

	/* write data */
	mask = 0x8000;
	while (mask) {
		/* low MDC, prepare MDO */
		miir &= ~(MASK_MIIR_MII_MDC + MASK_MIIR_MII_MDO);
		if (mask & data)
			miir |= MASK_MIIR_MII_MDO;
		writel(miir, miiport);

		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		writel(miir, miiport);

		/* next */
		mask >>= 1;
	}

	/* low MDC */
	miir &= ~MASK_MIIR_MII_MDC;
	writel(miir, miiport);

	return;
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;

	writel(0x00000001, ioaddr + BCR);	/* Reset */

	if (request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	init_ring(dev);

	writel(np->rx_ring_dma, ioaddr + RXLBA);
	writel(np->tx_ring_dma, ioaddr + TXLBA);

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds.
	   486: Set 8 longword burst.
	   586: no burst limit.
	   Burst length 5:3
	   0 0 0   1
	   0 0 1   4
	   0 1 0   8
	   0 1 1   16
	   1 0 0   32
	   1 0 1   64
	   1 1 0   128
	   1 1 1   256
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list.
	   FIXME (Ueimor): optimistic for alpha + posted writes ? */
#if defined(__powerpc__) || defined(__sparc__)
// 89/9/1 modify, 
//   np->bcrvalue=0x04 | 0x0x38;  /* big-endian, 256 burst length */
	np->bcrvalue = 0x04 | 0x10;	/* big-endian, tx 8 burst length */
	np->crvalue = 0xe00;	/* rx 128 burst length */
#elif defined(__alpha__) || defined(__x86_64__)
// 89/9/1 modify, 
//   np->bcrvalue=0x38;           /* little-endian, 256 burst length */
	np->bcrvalue = 0x10;	/* little-endian, 8 burst length */
	np->crvalue = 0xe00;	/* rx 128 burst length */
#elif defined(__i386__)
#if defined(MODULE)
// 89/9/1 modify, 
//   np->bcrvalue=0x38;           /* little-endian, 256 burst length */
	np->bcrvalue = 0x10;	/* little-endian, 8 burst length */
	np->crvalue = 0xe00;	/* rx 128 burst length */
#else
	/* When not a module we can work around broken '486 PCI boards. */
#define x86 boot_cpu_data.x86
// 89/9/1 modify, 
//   np->bcrvalue=(x86 <= 4 ? 0x10 : 0x38);
	np->bcrvalue = 0x10;
	np->crvalue = (x86 <= 4 ? 0xa00 : 0xe00);
	if (x86 <= 4)
		printk(KERN_INFO "%s: This is a 386/486 PCI system, setting burst "
		       "length to %x.\n", dev->name, (x86 <= 4 ? 0x10 : 0x38));
#endif
#else
// 89/9/1 modify,
//   np->bcrvalue=0x38;
	np->bcrvalue = 0x10;
	np->cralue = 0xe00;	/* rx 128 burst length */
#warning Processor architecture undefined!
#endif
// 89/12/29 add,
// 90/1/16 modify,
//   np->imrvalue=FBE|TUNF|CNTOVF|RBU|TI|RI;
	np->imrvalue = TUNF | CNTOVF | RBU | TI | RI;
	if (np->pci_dev->device == 0x891) {
		np->bcrvalue |= 0x200;	/* set PROG bit */
		np->crvalue |= 0x02000000;	/* set enhanced bit */
		np->imrvalue |= ETI;
	}
	writel(np->bcrvalue, ioaddr + BCR);

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	writel(0, dev->base_addr + RXPDR);
// 89/9/1 modify,
//   np->crvalue = 0x00e40001;    /* tx store and forward, tx/rx enable */
	np->crvalue |= 0x00e40001;	/* tx store and forward, tx/rx enable */
	np->mii.full_duplex = np->mii.force_media;
	getlinkstatus(dev);
	if (np->linkok)
		getlinktype(dev);
	set_rx_mode(dev);

	netif_start_queue(dev);

	/* Clear and Enable interrupts by setting the interrupt mask. */
	writel(FBE | TUNF | CNTOVF | RBU | TI | RI, ioaddr + ISR);
	writel(np->imrvalue, ioaddr + IMR);

	if (debug)
		printk(KERN_DEBUG "%s: Done netdev_open().\n", dev->name);

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = RUN_AT(3 * HZ);
	np->timer.data = (unsigned long) dev;
	np->timer.function = &netdev_timer;

	/* timer handler */
	add_timer(&np->timer);

	return 0;
}


static void getlinkstatus(struct net_device *dev)
/* function: Routine will read MII Status Register to get link status.       */
/* input   : dev... pointer to the adapter block.                            */
/* output  : none.                                                           */
{
	struct netdev_private *np = dev->priv;
	unsigned int i, DelayTime = 0x1000;

	np->linkok = 0;

	if (np->PHYType == MysonPHY) {
		for (i = 0; i < DelayTime; ++i) {
			if (readl(dev->base_addr + BMCRSR) & LinkIsUp2) {
				np->linkok = 1;
				return;
			}
			// delay
			m80x_delay(100);
		}
	} else {
		for (i = 0; i < DelayTime; ++i) {
			if (mdio_read(dev, np->phys[0], MII_BMSR) & BMSR_LSTATUS) {
				np->linkok = 1;
				return;
			}
			// delay
			m80x_delay(100);
		}
	}
}


static void getlinktype(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	if (np->PHYType == MysonPHY) {	/* 3-in-1 case */
		if (readl(dev->base_addr + TCRRCR) & FD)
			np->duplexmode = 2;	/* full duplex */
		else
			np->duplexmode = 1;	/* half duplex */
		if (readl(dev->base_addr + TCRRCR) & PS10)
			np->line_speed = 1;	/* 10M */
		else
			np->line_speed = 2;	/* 100M */
	} else {
		if (np->PHYType == SeeqPHY) {	/* this PHY is SEEQ 80225 */
			unsigned int data;

			data = mdio_read(dev, np->phys[0], MIIRegister18);
			if (data & SPD_DET_100)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
			if (data & DPLX_DET_FULL)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
		} else if (np->PHYType == AhdocPHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], DiagnosticReg);
			if (data & Speed_100)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
			if (data & DPLX_FULL)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
		}
/* 89/6/13 add, (begin) */
		else if (np->PHYType == MarvellPHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], SpecificReg);
			if (data & Full_Duplex)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
			data &= SpeedMask;
			if (data == Speed_1000M)
				np->line_speed = 3;	/* 1000M */
			else if (data == Speed_100M)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
		}
/* 89/6/13 add, (end) */
/* 89/7/27 add, (begin) */
		else if (np->PHYType == Myson981) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], StatusRegister);

			if (data & SPEED100)
				np->line_speed = 2;
			else
				np->line_speed = 1;

			if (data & FULLMODE)
				np->duplexmode = 2;
			else
				np->duplexmode = 1;
		}
/* 89/7/27 add, (end) */
/* 89/12/29 add */
		else if (np->PHYType == LevelOnePHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], SpecificReg);
			if (data & LXT1000_Full)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
			data &= SpeedMask;
			if (data == LXT1000_1000M)
				np->line_speed = 3;	/* 1000M */
			else if (data == LXT1000_100M)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
		}
		// chage crvalue
		// np->crvalue&=(~PS10)&(~FD);
		np->crvalue &= (~PS10) & (~FD) & (~PS1000);
		if (np->line_speed == 1)
			np->crvalue |= PS10;
		else if (np->line_speed == 3)
			np->crvalue |= PS1000;
		if (np->duplexmode == 2)
			np->crvalue |= FD;
	}
}


static void allocate_rx_buffers(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	/*  allocate skb for rx buffers */
	while (np->really_rx_count != RX_RING_SIZE) {
		struct sk_buff *skb;

		skb = dev_alloc_skb(np->rx_buf_sz);
		np->lack_rxbuf->skbuff = skb;

		if (skb == NULL)
			break;	/* Better luck next round. */

		skb->dev = dev;	/* Mark as being used by this device. */
		np->lack_rxbuf->buffer = pci_map_single(np->pci_dev, skb->tail,
			np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		np->lack_rxbuf = np->lack_rxbuf->next_desc_logical;
		++np->really_rx_count;
	}
}


static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10 * HZ;
	int old_crvalue = np->crvalue;
	unsigned int old_linkok = np->linkok;

	if (debug)
		printk(KERN_DEBUG "%s: Media selection timer tick, status %8.8lx "
		       "config %8.8lx.\n", dev->name, readl(ioaddr + ISR),
		       readl(ioaddr + TCRRCR));

	if (np->flags == HAS_MII_XCVR) {
		getlinkstatus(dev);
		if ((old_linkok == 0) && (np->linkok == 1)) {	/* we need to detect the media type again */
			getlinktype(dev);
			if (np->crvalue != old_crvalue) {
				stop_nic_tx(ioaddr, np->crvalue);
				stop_nic_rx(ioaddr, np->crvalue & (~0x40000));
				writel(np->crvalue, ioaddr + TCRRCR);
			}
		}
	}

	allocate_rx_buffers(dev);

	np->timer.expires = RUN_AT(next_tick);
	add_timer(&np->timer);
}


static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	printk(KERN_WARNING "%s: Transmit timed out, status %8.8lx,"
	       " resetting...\n", dev->name, readl(ioaddr + ISR));

	{

		printk(KERN_DEBUG "  Rx ring %p: ", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int) np->rx_ring[i].status);
		printk("\n" KERN_DEBUG "  Tx ring %p: ", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", np->tx_ring[i].status);
		printk("\n");
	}

	/* Reinit. Gross */

	/* Reset the chip's Tx and Rx processes. */
	stop_nic_tx(ioaddr, 0);
	reset_rx_descriptors(dev);

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0x0000, ioaddr + IMR);

	/* Reset the chip to erase previous misconfiguration. */
	writel(0x00000001, ioaddr + BCR);

	/* Ueimor: wait for 50 PCI cycles (and flush posted writes btw). 
	   We surely wait too long (address+data phase). Who cares ? */
	for (i = 0; i < 50; i++) {
		readl(ioaddr + BCR);
		rmb();
	}

	writel((np->cur_tx - np->tx_ring)*sizeof(struct fealnx_desc) +
		np->tx_ring_dma, ioaddr + TXLBA);
	writel((np->cur_rx - np->rx_ring)*sizeof(struct fealnx_desc) +
		np->rx_ring_dma, ioaddr + RXLBA);

	writel(np->bcrvalue, ioaddr + BCR);

	writel(0, dev->base_addr + RXPDR);
	set_rx_mode(dev);
	/* Clear and Enable interrupts by setting the interrupt mask. */
	writel(FBE | TUNF | CNTOVF | RBU | TI | RI, ioaddr + ISR);
	writel(np->imrvalue, ioaddr + IMR);

	writel(0, dev->base_addr + TXPDR);

	dev->trans_start = jiffies;
	np->stats.tx_errors++;

	return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	int i;

	/* initialize rx variables */
	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->cur_rx = &np->rx_ring[0];
	np->lack_rxbuf = NULL;
	np->really_rx_count = 0;

	/* initial rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].status = 0;
		np->rx_ring[i].control = np->rx_buf_sz << RBSShift;
		np->rx_ring[i].next_desc = np->rx_ring_dma +
			(i + 1)*sizeof(struct fealnx_desc);
		np->rx_ring[i].next_desc_logical = &np->rx_ring[i + 1];
		np->rx_ring[i].skbuff = NULL;
	}

	/* for the last rx descriptor */
	np->rx_ring[i - 1].next_desc = np->rx_ring_dma;
	np->rx_ring[i - 1].next_desc_logical = np->rx_ring;

	/* allocate skb for rx buffers */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);

		if (skb == NULL) {
			np->lack_rxbuf = &np->rx_ring[i];
			break;
		}

		++np->really_rx_count;
		np->rx_ring[i].skbuff = skb;
		skb->dev = dev;	/* Mark as being used by this device. */
		np->rx_ring[i].buffer = pci_map_single(np->pci_dev, skb->tail,
			np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		np->rx_ring[i].status = RXOWN;
		np->rx_ring[i].control |= RXIC;
	}

	/* initialize tx variables */
	np->cur_tx = &np->tx_ring[0];
	np->cur_tx_copy = &np->tx_ring[0];
	np->really_tx_count = 0;
	np->free_tx_count = TX_RING_SIZE;

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_ring[i].status = 0;
		np->tx_ring[i].next_desc = np->tx_ring_dma +
			(i + 1)*sizeof(struct fealnx_desc);
		np->tx_ring[i].next_desc_logical = &np->tx_ring[i + 1];
		np->tx_ring[i].skbuff = NULL;
	}

	/* for the last tx descriptor */
	np->tx_ring[i - 1].next_desc = np->tx_ring_dma;
	np->tx_ring[i - 1].next_desc_logical = &np->tx_ring[0];

	return;
}


static int start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	np->cur_tx_copy->skbuff = skb;

#define one_buffer
#define BPT 1022
#if defined(one_buffer)
	np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
		skb->len, PCI_DMA_TODEVICE);
	np->cur_tx_copy->control = TXIC | TXLD | TXFD | CRCEnable | PADEnable;
	np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
	np->cur_tx_copy->control |= (skb->len << TBSShift);	/* buffer size */
// 89/12/29 add,
	if (np->pci_dev->device == 0x891)
		np->cur_tx_copy->control |= ETIControl | RetryTxLC;
	np->cur_tx_copy->status = TXOWN;
	np->cur_tx_copy = np->cur_tx_copy->next_desc_logical;
	--np->free_tx_count;
#elif defined(two_buffer)
	if (skb->len > BPT) {
		struct fealnx_desc *next;

		/* for the first descriptor */
		np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
			BPT, PCI_DMA_TODEVICE);
		np->cur_tx_copy->control = TXIC | TXFD | CRCEnable | PADEnable;
		np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
		np->cur_tx_copy->control |= (BPT << TBSShift);	/* buffer size */

		/* for the last descriptor */
		next = (struct fealnx *) np->cur_tx_copy.next_desc_logical;
		next->skbuff = skb;
		next->control = TXIC | TXLD | CRCEnable | PADEnable;
		next->control |= (skb->len << PKTSShift);	/* pkt size */
		next->control |= ((skb->len - BPT) << TBSShift);	/* buf size */
// 89/12/29 add,
		if (np->pci_dev->device == 0x891)
			np->cur_tx_copy->control |= ETIControl | RetryTxLC;
		next->buffer = pci_map_single(ep->pci_dev, skb->data + BPT,
                                skb->len - BPT, PCI_DMA_TODEVICE);

		next->status = TXOWN;
		np->cur_tx_copy->status = TXOWN;

		np->cur_tx_copy = next->next_desc_logical;
		np->free_tx_count -= 2;
	} else {
		np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
			skb->len, PCI_DMA_TODEVICE);
		np->cur_tx_copy->control = TXIC | TXLD | TXFD | CRCEnable | PADEnable;
		np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
		np->cur_tx_copy->control |= (skb->len << TBSShift);	/* buffer size */
// 89/12/29 add,
		if (np->pci_dev->device == 0x891)
			np->cur_tx_copy->control |= ETIControl | RetryTxLC;
		np->cur_tx_copy->status = TXOWN;
		np->cur_tx_copy = np->cur_tx_copy->next_desc_logical;
		--np->free_tx_count;
	}
#endif

	if (np->free_tx_count < 2)
		netif_stop_queue(dev);
	++np->really_tx_count;
	writel(0, dev->base_addr + TXPDR);
	dev->trans_start = jiffies;

	return 0;
}


void free_one_rx_descriptor(struct netdev_private *np)
{
	if (np->really_rx_count == RX_RING_SIZE)
		np->cur_rx->status = RXOWN;
	else {
		np->lack_rxbuf->skbuff = np->cur_rx->skbuff;
		np->lack_rxbuf->buffer = np->cur_rx->buffer;
		np->lack_rxbuf->status = RXOWN;
		++np->really_rx_count;
		np->lack_rxbuf = np->lack_rxbuf->next_desc_logical;
	}
	np->cur_rx = np->cur_rx->next_desc_logical;
}


void reset_rx_descriptors(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	stop_nic_rx(dev->base_addr, np->crvalue);

	while (!(np->cur_rx->status & RXOWN))
		free_one_rx_descriptor(np);

	allocate_rx_buffers(dev);

	writel(np->rx_ring_dma + (np->cur_rx - np->rx_ring),
		dev->base_addr + RXLBA);
	writel(np->crvalue, dev->base_addr + TCRRCR);
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct netdev_private *np = dev->priv;
	long ioaddr, boguscnt = max_interrupt_work;
	unsigned int num_tx = 0;

	writel(0, dev->base_addr + IMR);

	ioaddr = dev->base_addr;
	np = (struct netdev_private *) dev->priv;

	do {
		u32 intr_status = readl(ioaddr + ISR);

		/* Acknowledge all of the current interrupt sources ASAP. */
		writel(intr_status, ioaddr + ISR);

		if (debug)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n", dev->name,
			       intr_status);

		if (!(intr_status & np->imrvalue))
			break;

// 90/1/16 delete,
//
//      if (intr_status & FBE)
//      {   /* fatal error */
//          stop_nic_tx(ioaddr, 0);
//          stop_nic_rx(ioaddr, 0);
//          break;
//      };

		if (intr_status & TUNF)
			writel(0, ioaddr + TXPDR);

		if (intr_status & CNTOVF) {
			/* missed pkts */
			np->stats.rx_missed_errors += readl(ioaddr + TALLY) & 0x7fff;

			/* crc error */
			np->stats.rx_crc_errors +=
			    (readl(ioaddr + TALLY) & 0x7fff0000) >> 16;
		}

		if (intr_status & (RI | RBU)) {
			if (intr_status & RI)
				netdev_rx(dev);
			else
				reset_rx_descriptors(dev);
		}

		while (np->really_tx_count) {
			long tx_status = np->cur_tx->status;
			long tx_control = np->cur_tx->control;

			if (!(tx_control & TXLD)) {	/* this pkt is combined by two tx descriptors */
				struct fealnx_desc *next;

				next = np->cur_tx->next_desc_logical;
				tx_status = next->status;
				tx_control = next->control;
			}

			if (tx_status & TXOWN)
				break;

			if (!(np->crvalue & 0x02000000)) {
				if (tx_status & (CSL | LC | EC | UDF | HF)) {
					np->stats.tx_errors++;
					if (tx_status & EC)
						np->stats.tx_aborted_errors++;
					if (tx_status & CSL)
						np->stats.tx_carrier_errors++;
					if (tx_status & LC)
						np->stats.tx_window_errors++;
					if (tx_status & UDF)
						np->stats.tx_fifo_errors++;
					if ((tx_status & HF) && np->mii.full_duplex == 0)
						np->stats.tx_heartbeat_errors++;

				} else {
					np->stats.tx_bytes +=
					    ((tx_control & PKTSMask) >> PKTSShift);

					np->stats.collisions +=
					    ((tx_status & NCRMask) >> NCRShift);
					np->stats.tx_packets++;
				}
			} else {
				np->stats.tx_bytes +=
				    ((tx_control & PKTSMask) >> PKTSShift);
				np->stats.tx_packets++;
			}

			/* Free the original skb. */
			pci_unmap_single(np->pci_dev, np->cur_tx->buffer,
				np->cur_tx->skbuff->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(np->cur_tx->skbuff);
			np->cur_tx->skbuff = NULL;
			--np->really_tx_count;
			if (np->cur_tx->control & TXLD) {
				np->cur_tx = np->cur_tx->next_desc_logical;
				++np->free_tx_count;
			} else {
				np->cur_tx = np->cur_tx->next_desc_logical;
				np->cur_tx = np->cur_tx->next_desc_logical;
				np->free_tx_count += 2;
			}
			num_tx++;
		}		/* end of for loop */
		
		if (num_tx && np->free_tx_count >= 2)
			netif_wake_queue(dev);

		/* read transmit status for enhanced mode only */
		if (np->crvalue & 0x02000000) {
			long data;

			data = readl(ioaddr + TSR);
			np->stats.tx_errors += (data & 0xff000000) >> 24;
			np->stats.tx_aborted_errors += (data & 0xff000000) >> 24;
			np->stats.tx_window_errors += (data & 0x00ff0000) >> 16;
			np->stats.collisions += (data & 0x0000ffff);
		}

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
			       "status=0x%4.4x.\n", dev->name, intr_status);
			break;
		}
	} while (1);

	/* read the tally counters */
	/* missed pkts */
	np->stats.rx_missed_errors += readl(ioaddr + TALLY) & 0x7fff;

	/* crc error */
	np->stats.rx_crc_errors += (readl(ioaddr + TALLY) & 0x7fff0000) >> 16;

	if (debug)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4lx.\n",
		       dev->name, readl(ioaddr + ISR));

	writel(np->imrvalue, ioaddr + IMR);

	return;
}


/* This routine is logically part of the interrupt handler, but seperated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (!(np->cur_rx->status & RXOWN)) {
		s32 rx_status = np->cur_rx->status;

		if (np->really_rx_count == 0)
			break;

		if (debug)
			printk(KERN_DEBUG "  netdev_rx() status was %8.8x.\n", rx_status);

		if ((!((rx_status & RXFSD) && (rx_status & RXLSD)))
		    || (rx_status & ErrorSummary)) {
			if (rx_status & ErrorSummary) {	/* there was a fatal error */
				if (debug)
					printk(KERN_DEBUG
					       "%s: Receive error, Rx status %8.8x.\n",
					       dev->name, rx_status);

				np->stats.rx_errors++;	/* end of a packet. */
				if (rx_status & (LONG | RUNT))
					np->stats.rx_length_errors++;
				if (rx_status & RXER)
					np->stats.rx_frame_errors++;
				if (rx_status & CRC)
					np->stats.rx_crc_errors++;
			} else {
				int need_to_reset = 0;
				int desno = 0;

				if (rx_status & RXFSD) {	/* this pkt is too long, over one rx buffer */
					struct fealnx_desc *cur;

					/* check this packet is received completely? */
					cur = np->cur_rx;
					while (desno <= np->really_rx_count) {
						++desno;
						if ((!(cur->status & RXOWN))
						    && (cur->status & RXLSD))
							break;
						/* goto next rx descriptor */
						cur = cur->next_desc_logical;
					}
					if (desno > np->really_rx_count)
						need_to_reset = 1;
				} else	/* RXLSD did not find, something error */
					need_to_reset = 1;

				if (need_to_reset == 0) {
					int i;

					np->stats.rx_length_errors++;

					/* free all rx descriptors related this long pkt */
					for (i = 0; i < desno; ++i)
						free_one_rx_descriptor(np);
					continue;
				} else {	/* something error, need to reset this chip */
					reset_rx_descriptors(dev);
				}
				break;	/* exit the while loop */
			}
		} else {	/* this received pkt is ok */

			struct sk_buff *skb;
			/* Omit the four octet CRC from the length. */
			short pkt_len = ((rx_status & FLNGMASK) >> FLNGShift) - 4;

#ifndef final_version
			if (debug)
				printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d"
				       " status %x.\n", pkt_len, rx_status);
#endif
			pci_dma_sync_single(np->pci_dev, np->cur_rx->buffer,
				np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			pci_unmap_single(np->pci_dev, np->cur_rx->buffer,
				np->rx_buf_sz, PCI_DMA_FROMDEVICE);

			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak &&
			    (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				/* Call copy + cksum if available. */

#if ! defined(__alpha__)
				eth_copy_and_sum(skb, 
					np->cur_rx->skbuff->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					np->cur_rx->skbuff->tail, pkt_len);
#endif
			} else {
				skb_put(skb = np->cur_rx->skbuff, pkt_len);
				np->cur_rx->skbuff = NULL;
				if (np->really_rx_count == RX_RING_SIZE)
					np->lack_rxbuf = np->cur_rx;
				--np->really_rx_count;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			np->stats.rx_packets++;
			np->stats.rx_bytes += pkt_len;
		}

		if (np->cur_rx->skbuff == NULL) {
			struct sk_buff *skb;

			skb = dev_alloc_skb(np->rx_buf_sz);

			if (skb != NULL) {
				skb->dev = dev;	/* Mark as being used by this device. */
				np->cur_rx->buffer = pci_map_single(np->pci_dev, skb->tail,
					np->rx_buf_sz, PCI_DMA_FROMDEVICE);
				np->cur_rx->skbuff = skb;
				++np->really_rx_count;
			}
		}

		if (np->cur_rx->skbuff != NULL)
			free_one_rx_descriptor(np);
	}			/* end of while loop */

	/*  allocate skb for rx buffers */
	allocate_rx_buffers(dev);

	return 0;
}


static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;

	/* The chip only need report frame silently dropped. */
	if (netif_running(dev)) {
		np->stats.rx_missed_errors += readl(ioaddr + TALLY) & 0x7fff;
		np->stats.rx_crc_errors += (readl(ioaddr + TALLY) & 0x7fff0000) >> 16;
	}

	return &np->stats;
}

static void set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	u32 mc_filter[2];	/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {	/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = PROM | AB | AM;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = AB | AM;
	} else {
		struct dev_mc_list *mclist;
		int i;

		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			set_bit((ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26) ^ 0x3F,
				mc_filter);
		}
		rx_mode = AB | AM;
	}

	stop_nic_tx(ioaddr, np->crvalue);
	stop_nic_rx(ioaddr, np->crvalue & (~0x40000));

	writel(mc_filter[0], ioaddr + MAR0);
	writel(mc_filter[1], ioaddr + MAR1);
	np->crvalue &= ~RxModeMask;
	np->crvalue |= rx_mode;
	writel(np->crvalue, ioaddr + TCRRCR);
}

static int netdev_ethtool_ioctl (struct net_device *dev, void *useraddr)
{
	struct netdev_private *np = dev->priv;
	u32 ethcmd;

	if (copy_from_user (&ethcmd, useraddr, sizeof (ethcmd)))
		return -EFAULT;

	switch (ethcmd) {
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
		strcpy (info.driver, DRV_NAME);
		strcpy (info.version, DRV_VERSION);
		strcpy (info.bus_info, np->pci_dev->slot_name);
		if (copy_to_user (useraddr, &info, sizeof (info)))
			return -EFAULT;
		return 0;
	}

	/* get settings */
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = { ETHTOOL_GSET };
		spin_lock_irq(&np->lock);
		mii_ethtool_gset(&np->mii, &ecmd);
		spin_unlock_irq(&np->lock);
		if (copy_to_user(useraddr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	/* set settings */
	case ETHTOOL_SSET: {
		int r;
		struct ethtool_cmd ecmd;
		if (copy_from_user(&ecmd, useraddr, sizeof(ecmd)))
			return -EFAULT;
		spin_lock_irq(&np->lock);
		r = mii_ethtool_sset(&np->mii, &ecmd);
		spin_unlock_irq(&np->lock);
		return r;
	}
	/* restart autonegotiation */
	case ETHTOOL_NWAY_RST: {
		return mii_nway_restart(&np->mii);
	}
	/* get link status */
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};
		edata.data = mii_link_ok(&np->mii);
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}

	/* get message-level */
	case ETHTOOL_GMSGLVL: {
		struct ethtool_value edata = {ETHTOOL_GMSGLVL};
		edata.data = debug;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
	/* set message-level */
	case ETHTOOL_SMSGLVL: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		debug = edata.data;
		return 0;
	}
	default:
		break;
	}

	return -EOPNOTSUPP;
}


static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = dev->priv;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *) & rq->ifr_data;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	if (cmd == SIOCETHTOOL)
		rc = netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);

	else {
		spin_lock_irq(&np->lock);
		rc = generic_mii_ioctl(&np->mii, data, cmd, NULL);
		spin_unlock_irq(&np->lock);
	}

	return rc;
}


static int netdev_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = dev->priv;
	int i;

	netif_stop_queue(dev);

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0x0000, ioaddr + IMR);

	/* Stop the chip's Tx and Rx processes. */
	stop_nic_tx(ioaddr, 0);
	stop_nic_rx(ioaddr, 0);

	del_timer_sync(&np->timer);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = np->rx_ring[i].skbuff;

		np->rx_ring[i].status = 0;
		if (skb) {
			pci_unmap_single(np->pci_dev, np->rx_ring[i].buffer,
				np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
			np->rx_ring[i].skbuff = NULL;
		}
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = np->tx_ring[i].skbuff;

		if (skb) {
			pci_unmap_single(np->pci_dev, np->tx_ring[i].buffer,
				skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			np->tx_ring[i].skbuff = NULL;
		}
	}

	return 0;
}

static struct pci_device_id fealnx_pci_tbl[] __devinitdata = {
	{0x1516, 0x0800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1516, 0x0803, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{0x1516, 0x0891, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{} /* terminate list */
};
MODULE_DEVICE_TABLE(pci, fealnx_pci_tbl);


static struct pci_driver fealnx_driver = {
	.name		= "fealnx",
	.id_table	= fealnx_pci_tbl,
	.probe		= fealnx_init_one,
	.remove		= __devexit_p(fealnx_remove_one),
};

static int __init fealnx_init(void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk (version);
#endif

	return pci_module_init(&fealnx_driver);
}

static void __exit fealnx_exit(void)
{
	pci_unregister_driver(&fealnx_driver);
}

module_init(fealnx_init);
module_exit(fealnx_exit);
