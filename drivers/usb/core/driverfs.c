/*
 * drivers/usb/core/driverfs.c
 *
 * (C) Copyright 2002 David Brownell
 * (C) Copyright 2002 Greg Kroah-Hartman
 * (C) Copyright 2002 IBM Corp.
 *
 * All of the driverfs file attributes for usb devices and interfaces.
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb.h"

/* Active configuration fields */
#define usb_actconfig_attr(field, format_string)			\
static ssize_t								\
show_##field (struct device *dev, char *buf, size_t count, loff_t off)	\
{									\
	struct usb_device *udev;					\
									\
	if (off)							\
		return 0;						\
									\
	udev = to_usb_device (dev);					\
	return sprintf (buf, format_string, udev->actconfig->desc.field); \
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_actconfig_attr (bNumInterfaces, "%2d\n")
usb_actconfig_attr (bConfigurationValue, "%2d\n")
usb_actconfig_attr (bmAttributes, "%2x\n")
usb_actconfig_attr (bMaxPower, "%3dmA\n")

/* String fields */
static ssize_t show_product (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = to_usb_device (dev);

	len = usb_string(udev, udev->descriptor.iProduct, buf, PAGE_SIZE);
	if (len < 0)
		return 0;
	buf[len] = '\n';
	buf[len+1] = 0;
	return len+1;
}
static DEVICE_ATTR(product,S_IRUGO,show_product,NULL);

static ssize_t
show_manufacturer (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = to_usb_device (dev);

	len = usb_string(udev, udev->descriptor.iManufacturer, buf, PAGE_SIZE);
	if (len < 0)
		return 0;
	buf[len] = '\n';
	buf[len+1] = 0;
	return len+1;
}
static DEVICE_ATTR(manufacturer,S_IRUGO,show_manufacturer,NULL);

static ssize_t
show_serial (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	int len;

	if (off)
		return 0;
	udev = to_usb_device (dev);

	len = usb_string(udev, udev->descriptor.iSerialNumber, buf, PAGE_SIZE);
	if (len < 0)
		return 0;
	buf[len] = '\n';
	buf[len+1] = 0;
	return len+1;
}
static DEVICE_ATTR(serial,S_IRUGO,show_serial,NULL);

static ssize_t
show_speed (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct usb_device *udev;
	char *speed;

	if (off)
		return 0;
	udev = to_usb_device (dev);

	switch (udev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}
	return sprintf (buf, "%s\n", speed);
}
static DEVICE_ATTR(speed, S_IRUGO, show_speed, NULL);

/* Descriptor fields */
#define usb_descriptor_attr(field, format_string)			\
static ssize_t								\
show_##field (struct device *dev, char *buf, size_t count, loff_t off)	\
{									\
	struct usb_device *udev;					\
									\
	if (off)							\
		return 0;						\
									\
	udev = to_usb_device (dev);					\
	return sprintf (buf, format_string, udev->descriptor.field);	\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_descriptor_attr (idVendor, "%04x\n")
usb_descriptor_attr (idProduct, "%04x\n")
usb_descriptor_attr (bcdDevice, "%04x\n")
usb_descriptor_attr (bDeviceClass, "%02x\n")
usb_descriptor_attr (bDeviceSubClass, "%02x\n")
usb_descriptor_attr (bDeviceProtocol, "%02x\n")


void usb_create_driverfs_dev_files (struct usb_device *udev)
{
	struct device *dev = &udev->dev;

	device_create_file (dev, &dev_attr_bNumInterfaces);
	device_create_file (dev, &dev_attr_bConfigurationValue);
	device_create_file (dev, &dev_attr_bmAttributes);
	device_create_file (dev, &dev_attr_bMaxPower);
	device_create_file (dev, &dev_attr_idVendor);
	device_create_file (dev, &dev_attr_idProduct);
	device_create_file (dev, &dev_attr_bcdDevice);
	device_create_file (dev, &dev_attr_bDeviceClass);
	device_create_file (dev, &dev_attr_bDeviceSubClass);
	device_create_file (dev, &dev_attr_bDeviceProtocol);
	device_create_file (dev, &dev_attr_speed);

	if (udev->descriptor.iManufacturer)
		device_create_file (dev, &dev_attr_manufacturer);
	if (udev->descriptor.iProduct)
		device_create_file (dev, &dev_attr_product);
	if (udev->descriptor.iSerialNumber)
		device_create_file (dev, &dev_attr_serial);
}

/* Interface fields */
#define usb_intf_attr(field, format_string)				\
static ssize_t								\
show_##field (struct device *dev, char *buf, size_t count, loff_t off)	\
{									\
	struct usb_interface *intf;					\
	int alt;							\
									\
	if (off)							\
		return 0;						\
									\
	intf = to_usb_interface (dev);					\
	alt = intf->act_altsetting;					\
									\
	return sprintf (buf, format_string, intf->altsetting[alt].desc.field); \
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_intf_attr (bAlternateSetting, "%2d\n")
usb_intf_attr (bInterfaceClass, "%02x\n")
usb_intf_attr (bInterfaceSubClass, "%02x\n")
usb_intf_attr (bInterfaceProtocol, "%02x\n")

void usb_create_driverfs_intf_files (struct usb_interface *intf)
{
	device_create_file (&intf->dev, &dev_attr_bAlternateSetting);
	device_create_file (&intf->dev, &dev_attr_bInterfaceClass);
	device_create_file (&intf->dev, &dev_attr_bInterfaceSubClass);
	device_create_file (&intf->dev, &dev_attr_bInterfaceProtocol);
}
