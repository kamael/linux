/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ipv4 proc support
 *
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2002/10/10
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License as
 *		published by the Free Software Foundation; version 2 of the
 *		License
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

extern int raw_get_info(char *, char **, off_t, int);
extern int snmp_get_info(char *, char **, off_t, int);
extern int netstat_get_info(char *, char **, off_t, int);
extern int afinet_get_info(char *, char **, off_t, int);
extern int tcp_get_info(char *, char **, off_t, int);

#ifdef CONFIG_PROC_FS
#ifdef CONFIG_AX25

/* ------------------------------------------------------------------------ */
/*
 *	ax25 -> ASCII conversion
 */
static char *ax2asc2(ax25_address *a, char *buf)
{
	char c, *s;
	int n;

	for (n = 0, s = buf; n < 6; n++) {
		c = (a->ax25_call[n] >> 1) & 0x7F;

		if (c != ' ') *s++ = c;
	}
	
	*s++ = '-';

	if ((n = ((a->ax25_call[6] >> 1) & 0x0F)) > 9) {
		*s++ = '1';
		n -= 10;
	}
	
	*s++ = n + '0';
	*s++ = '\0';

	if (*buf == '\0' || *buf == '-')
	   return "*";

	return buf;

}
#endif /* CONFIG_AX25 */

struct arp_iter_state {
	int is_pneigh, bucket;
};

static __inline__ struct neighbour *neigh_get_bucket(struct seq_file *seq,
						     loff_t *pos)
{
	struct neighbour *n = NULL;
	struct arp_iter_state* state = seq->private;
	loff_t l = *pos;
	int i;

	for (; state->bucket <= NEIGH_HASHMASK; ++state->bucket)
		for (i = 0, n = arp_tbl.hash_buckets[state->bucket]; n;
		     ++i, n = n->next)
			/* Do not confuse users "arp -a" with magic entries */
			if ((n->nud_state & ~NUD_NOARP) && !l--) {
				*pos = i;
				goto out;
			}
out:
	return n;
}

static __inline__ struct pneigh_entry *pneigh_get_bucket(struct seq_file *seq,
							 loff_t *pos)
{
	struct pneigh_entry *n = NULL;
	struct arp_iter_state* state = seq->private;
	loff_t l = *pos;
	int i;

	for (; state->bucket <= PNEIGH_HASHMASK; ++state->bucket)
		for (i = 0, n = arp_tbl.phash_buckets[state->bucket]; n;
		     ++i, n = n->next)
			if (!l--) {
				*pos = i;
				goto out;
			}
out:
	return n;
}

static __inline__ void *arp_get_bucket(struct seq_file *seq, loff_t *pos)
{
	void *rc = neigh_get_bucket(seq, pos);

	if (!rc) {
		struct arp_iter_state* state = seq->private;

		read_unlock_bh(&arp_tbl.lock);
		state->is_pneigh = 1;
		state->bucket	 = 0;
		*pos		 = 0;
		rc = pneigh_get_bucket(seq, pos);
	}
	return rc;
}

static void *arp_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&arp_tbl.lock);
	return *pos ? arp_get_bucket(seq, pos) : (void *)1;
}

static void *arp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	void *rc;
	struct arp_iter_state* state;

	if (v == (void *)1) {
		rc = arp_get_bucket(seq, pos);
		goto out;
	}

	state = seq->private;
	if (!state->is_pneigh) {
		struct neighbour *n = v;

		rc = n = n->next;
		if (n)
			goto out;
		*pos = 0;
		++state->bucket;
		rc = neigh_get_bucket(seq, pos);
		if (rc)
			goto out;
		read_unlock_bh(&arp_tbl.lock);
		state->is_pneigh = 1;
		state->bucket	 = 0;
		*pos		 = 0;
		rc = pneigh_get_bucket(seq, pos);
	} else {
		struct pneigh_entry *pn = v;

		pn = pn->next;
		if (!pn) {
			++state->bucket;
			*pos = 0;
			pn   = pneigh_get_bucket(seq, pos);
		}
		rc = pn;
	}
out:
	++*pos;
	return rc;
}

static void arp_seq_stop(struct seq_file *seq, void *v)
{
	struct arp_iter_state* state = seq->private;

	if (!state->is_pneigh)
		read_unlock_bh(&arp_tbl.lock);
}

#define HBUFFERLEN 30

static __inline__ void arp_format_neigh_entry(struct seq_file *seq,
					      struct neighbour *n)
{
	char hbuffer[HBUFFERLEN];
	const char hexbuf[] = "0123456789ABCDEF";
	int k, j;
	char tbuf[16];
	struct net_device *dev = n->dev;
	int hatype = dev->type;

	read_lock(&n->lock);
	/* Convert hardware address to XX:XX:XX:XX ... form. */
#ifdef CONFIG_AX25
	if (hatype == ARPHRD_AX25 || hatype == ARPHRD_NETROM)
		ax2asc2((ax25_address *)n->ha, hbuffer);
	else {
#endif
	for (k = 0, j = 0; k < HBUFFERLEN - 3 && j < dev->addr_len; j++) {
		hbuffer[k++] = hexbuf[(n->ha[j] >> 4) & 15];
		hbuffer[k++] = hexbuf[n->ha[j] & 15];
		hbuffer[k++] = ':';
	}
	hbuffer[--k] = 0;
#ifdef CONFIG_AX25
	}
#endif
	sprintf(tbuf, "%u.%u.%u.%u", NIPQUAD(*(u32*)n->primary_key));
	seq_printf(seq, "%-16s 0x%-10x0x%-10x%s     *        %s\n",
		   tbuf, hatype, arp_state_to_flags(n), hbuffer, dev->name);
	read_unlock(&n->lock);
}

static __inline__ void arp_format_pneigh_entry(struct seq_file *seq,
					       struct pneigh_entry *n)
{

	struct net_device *dev = n->dev;
	int hatype = dev ? dev->type : 0;
	char tbuf[16];

	sprintf(tbuf, "%u.%u.%u.%u", NIPQUAD(*(u32*)n->key));
	seq_printf(seq, "%-16s 0x%-10x0x%-10x%s     *        %s\n",
		   tbuf, hatype, ATF_PUBL | ATF_PERM, "00:00:00:00:00:00",
		   dev ? dev->name : "*");
}

static int arp_seq_show(struct seq_file *seq, void *v)
{
	if (v == (void *)1)
		seq_puts(seq, "IP address       HW type     Flags       "
			      "HW address            Mask     Device\n");
	else {
		struct arp_iter_state* state = seq->private;

		if (state->is_pneigh)
			arp_format_pneigh_entry(seq, v);
		else
			arp_format_neigh_entry(seq, v);
	}

	return 0;
}

/* ------------------------------------------------------------------------ */

static struct seq_operations arp_seq_ops = {
	.start  = arp_seq_start,
	.next   = arp_seq_next,
	.stop   = arp_seq_stop,
	.show   = arp_seq_show,
};

static int arp_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct arp_iter_state *s = kmalloc(sizeof(*s), GFP_KERNEL);
       
	if (!s)
		goto out;

	rc = seq_open(file, &arp_seq_ops);
	if (rc)
		goto out_kfree;

	seq	     = file->private_data;
	seq->private = s;
	memset(s, 0, sizeof(*s));
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

int ip_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *)file->private_data;

	kfree(seq->private);
	seq->private = NULL;

	return seq_release(inode, file);
}

static struct file_operations arp_seq_fops = {
	.open           = arp_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= ip_seq_release,
};

/* ------------------------------------------------------------------------ */

int __init ipv4_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = 0;

	if (!proc_net_create("raw", 0, raw_get_info))
		goto out_raw;

	if (!proc_net_create("netstat", 0, netstat_get_info))
		goto out_netstat;

	if (!proc_net_create("snmp", 0, snmp_get_info))
		goto out_snmp;

	if (!proc_net_create("sockstat", 0, afinet_get_info))
		goto out_sockstat;

	if (!proc_net_create("tcp", 0, tcp_get_info))
		goto out_tcp;

	p = create_proc_entry("arp", S_IRUGO, proc_net);
	if (!p)
		goto out_arp;
	p->proc_fops = &arp_seq_fops;

out:
	return rc;
out_arp:
	proc_net_remove("tcp");
out_tcp:
	proc_net_remove("sockstat");
out_sockstat:
	proc_net_remove("snmp");
out_snmp:
	proc_net_remove("netstat");
out_netstat:
	proc_net_remove("raw");
out_raw:
	rc = -ENOMEM;
	goto out;
}
#else /* CONFIG_PROC_FS */
int __init ipv4_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */
