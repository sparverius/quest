/*                    The Quest Operating System
 *  Copyright (C) 2005-2010  Richard West, Boston University
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <drivers/usb/usb.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ftdi.h>
#include <arch/i386.h>
#include <util/printf.h>
#include <kernel.h>

#define DEBUG_FTDI

#ifdef DEBUG_FTDI
#define DLOG(fmt,...) DLOG_PREFIX("ftdi",fmt,##__VA_ARGS__)
#else
#define DLOG(fmt,...) ;
#endif

/* The Base Clock of the chip. It is either 12000000 or 48000000. */
#define FTDI_BASE_CLOCK    48000000

static USB_DEVICE_INFO ftdi_dev;

bool usb_ftdi_driver_init (void);
void usb_ftdi_putc (char);
int usb_ftdi_write (unsigned char *, uint32_t);
int usb_ftdi_read (unsigned char *, uint32_t);

/*
 * Reset FTDI chip. Always set port to 0? The Linux driver
 * says this is 0 for FT232/245. For now, we are supposed to
 * support only FT232.
 */
static int
ftdi_reset (USB_DEVICE_INFO * dev, uint16_t port)
{
  USB_DEV_REQ req;
  req.bmRequestType = 0x40;
  req.bRequest = USB_FTDI_RESET;
  /* wValue = 0  :  Reset
   * wValue = 1  :  Purge RX buffer
   * wValue = 2  :  Purge TX buffer
   */
  req.wValue = 0;
  req.wIndex = port;
  req.wLength = 0;

  return usb_control_transfer (dev, (addr_t) & req,
      sizeof (USB_DEV_REQ), 0, 0);
}

/* Get the divisor which is later used to set the baud rate */
/* This function is from the Linux FTDI driver */
static uint32_t
ftdi_baud_base_to_divisor (uint32_t baud, uint32_t base)
{
  static const unsigned char divfrac[8] = { 0, 3, 2, 4, 1, 5, 6, 7 };
  uint32_t divisor;
  /* divisor shifted 3 bits to the left */
  int divisor3 = base / 2 / baud;
  divisor = divisor3 >> 3;
  divisor |= (uint32_t)divfrac[divisor3 & 0x7] << 14;
  /* Deal with special cases for highest baud rates. */
  if (divisor == 1)
    divisor = 0;
  else if (divisor == 0x4001)
    divisor = 1;
  return divisor;
}

static int
ftdi_set_baud (USB_DEVICE_INFO * dev, uint32_t baud, uint16_t port)
{
  USB_DEV_REQ req;
  uint32_t divisor = ftdi_baud_base_to_divisor (baud, FTDI_BASE_CLOCK);

  req.bmRequestType = 0x40;
  req.bRequest = USB_FTDI_SET_BAUD_RATE;
  req.wValue = divisor;
  req.wIndex = port;
  req.wLength = 0;

  return usb_control_transfer (dev, (addr_t) & req,
      sizeof (USB_DEV_REQ), 0, 0);
}

/* Setting FTDI data characteristics */
static int
ftdi_set_data (
    USB_DEVICE_INFO * dev,
    uint8_t bits, /* Number of data bits */
    uint8_t parity, /* 0 = None, 1 = Odd, 2 = Even, 3 = Mark, 4 = Space */
    uint8_t stb, /* Stop Bits 0 = 1, 1 = 1.5, 2 = 2 */
    uint8_t tx, /* 1 = TX ON (break), 0 = TX OFF (normal state) */
    uint16_t port)
{
  USB_DEV_REQ req;
  uint16_t dc = (tx & 0x1) << 14 |
    (stb & 0x7) << 11 | (parity & 0x7) << 8 | bits;

  req.bmRequestType = 0x40;
  req.bRequest = USB_FTDI_SET_DATA;
  req.wValue = dc;
  req.wIndex = port;
  req.wLength = 0;

  return usb_control_transfer (dev, (addr_t) & req,
      sizeof (USB_DEV_REQ), 0, 0);
}

static bool
ftdi_init (USB_DEVICE_INFO *dev, USB_CFG_DESC *cfg, USB_IF_DESC *ifd)
{
  // TODO
  ftdi_dev = *(dev);

  /* Reset the chip */
  DLOG ("Resetting FTDI chip ...");
  if (ftdi_reset (dev, 0)) {
    DLOG ("Chip reset failed!");
    return FALSE;
  }

  /* Set the baud rate for the serial port */
  DLOG ("Setting baud rate ...");
  if (ftdi_set_baud (dev, 38400, 0)) {
    DLOG ("Setting baud rate failed!");
    return FALSE;
  }

  /* Set data characteristics */
  DLOG ("Setting data characteristics ...");
  if (ftdi_set_data (dev, 8, 0, 0, 0, 0)) {
    DLOG ("Setting data characteristics failed!");
    return FALSE;
  }

  return TRUE;
}

static void
ftdi_test (void)
{
  DLOG ("FTDI Tests");
#if 0
  unsigned char buf[10];
  int act_len = 0, i = 0;
  act_len = usb_ftdi_read (buf, 5);
  DLOG ("%d bytes read", act_len);
  for (i = 0; i < act_len; i++) {
    DLOG ("Byte %d : 0x%X", i + 1, buf[i]);
  }
#endif
  usb_ftdi_putc ('Q');
  usb_ftdi_putc ('u');
  usb_ftdi_putc ('e');
  usb_ftdi_putc ('s');
  usb_ftdi_putc ('t');
}

static bool
ftdi_probe (USB_DEVICE_INFO *dev, USB_CFG_DESC *cfg, USB_IF_DESC *ifd)
{
  if (dev->devd.idVendor == 0x0403) {
    DLOG ("An FTDI chip is detected");

    if (dev->devd.idProduct != 0x6001) {
      DLOG ("FT232 not found. Product ID is: 0x%X", dev->devd.idProduct);
    }
  }

  if (!ftdi_init(dev, cfg, ifd)) {
    DLOG("Initialization failed!");
    return FALSE;
  }

  DLOG ("FTDI Serial Converter configured");
  ftdi_test();

  return TRUE;
}

void
usb_ftdi_putc (char c)
{
  unsigned char buf[2];
  int count = 0, buf_size = 0;

#if 0
  /* This is not necessary for newer chips */
  /* First byte is reserved, it contains length and Port ID */
  /* B0 = 1, B1 = 0, B2..7 = Length of Message */
  buf[0] = (1 << 2) | 1;
  /* Actual data to be transfered */
  buf[1] = c;
  buf_size = 2;
#else
  buf[0] = c;
  buf_size = 1;
#endif

  if ((count = usb_ftdi_write (buf, buf_size)) != buf_size)
    DLOG ("usb_ftdi_putc failed, %d bytes sent", count);
  else
    DLOG ("usb_ftdi_putc done, %d bytes sent", count);
}

int
usb_ftdi_write (unsigned char * buf, uint32_t len)
{
  uint32_t act_len = 0;
  uint8_t endp = 2;

  usb_bulk_transfer (&ftdi_dev, endp, (addr_t) buf, len,
      64, DIR_OUT, &act_len);
  
  return act_len;
}

int
usb_ftdi_read (unsigned char * buf, uint32_t len)
{
  uint32_t act_len = 0;
  uint8_t endp = 1;

  usb_bulk_transfer (&ftdi_dev, endp, (addr_t) buf, len,
      64, DIR_IN, &act_len);
  
  return act_len;
}

static USB_DRIVER ftdi_driver = {
  .probe = ftdi_probe
};

bool
usb_ftdi_driver_init (void)
{
  return usb_register_driver (&ftdi_driver);
}

/*
 * Local Variables:
 * indent-tabs-mode: nil
 * mode: C
 * c-file-style: "gnu"
 * c-basic-offset: 2
 * End:
 */

/* vi: set et sw=2 sts=2: */
