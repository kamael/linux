/*
 *  hosts.h Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995, 1998, 1999 Eric Youngdale
 *
 *  mid to low-level SCSI driver interface header
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *	 Modified by Eric Youngdale eric@andante.org to
 *	 add scatter-gather, multiple outstanding request, and other
 *	 enhancements.
 *
 *  Further modified by Eric Youngdale to support multiple host adapters
 *  of the same type.
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#ifndef _HOSTS_H
#define _HOSTS_H

#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/types.h>

#include <scsi/scsi_host.h>

extern void scsi_report_bus_reset(struct Scsi_Host *, int);
extern void scsi_report_device_reset(struct Scsi_Host *, int, int);

struct scsi_driver {
	struct module		*owner;
	struct device_driver	gendrv;

	int (*init_command)(struct scsi_cmnd *);
	void (*rescan)(struct device *);
};
#define to_scsi_driver(drv) \
	container_of((drv), struct scsi_driver, gendrv)

extern int scsi_register_driver(struct device_driver *);
#define scsi_unregister_driver(drv) \
	driver_unregister(drv);

extern int scsi_register_interface(struct class_interface *);
#define scsi_unregister_interface(intf) \
	class_interface_unregister(intf)


/**
 * scsi_find_device - find a device given the host
 * @shost:	SCSI host pointer
 * @channel:	SCSI channel (zero if only one channel)
 * @pun:	SCSI target number (physical unit number)
 * @lun:	SCSI Logical Unit Number
 **/
static inline struct scsi_device *scsi_find_device(struct Scsi_Host *shost,
                                            int channel, int pun, int lun) {
        struct scsi_device *sdev;

	list_for_each_entry (sdev, &shost->my_devices, siblings)
                if (sdev->channel == channel && sdev->id == pun
                   && sdev->lun ==lun)
                        return sdev;
        return NULL;
}

#endif
