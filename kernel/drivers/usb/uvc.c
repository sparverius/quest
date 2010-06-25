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
#include <drivers/usb/uvc.h>
#include <arch/i386.h>
#include <util/printf.h>
#include <kernel.h>

#define DEBUG_UVC

#ifdef DEBUG_UVC

#define DLOG(fmt,...) DLOG_PREFIX("uvc",fmt,##__VA_ARGS__)

#define printf com1_printf
#define print com1_puts
#define putx  com1_putx
#define putchar com1_putc

#else

#define DLOG(fmt,...) ;
#endif

struct iso_data_source
{
  uint8_t endp;
  uint16_t max_packet;
  uint32_t sample_size;
};

typedef struct iso_data_source ISO_DATA_SRC;

static ISO_DATA_SRC iso_src;

bool usb_uvc_driver_init (void);
static bool uvc_probe (USB_DEVICE_INFO *, USB_CFG_DESC *, USB_IF_DESC *);
static void desc_dump (USB_DEVICE_INFO *, USB_CFG_DESC *);
static bool uvc_init(USB_DEVICE_INFO *, USB_CFG_DESC *);
static int video_probe_controls (USB_DEVICE_INFO *, uint8_t, uint8_t,
    UVC_VS_CTL_PAR_BLOCK *);
static int video_commit_controls (USB_DEVICE_INFO *, uint8_t, uint8_t,
    UVC_VS_CTL_PAR_BLOCK *);
static int video_error_code (USB_DEVICE_INFO *, uint8_t, uint8_t);
static void para_block_dump (UVC_VS_CTL_PAR_BLOCK *);
int uvc_get_frame (USB_DEVICE_INFO *, ISO_DATA_SRC *, addr_t, uint32_t);

int
uvc_get_frame (
    USB_DEVICE_INFO * dev,
    ISO_DATA_SRC * iso_src,
    addr_t data,
    uint32_t data_len)
{
  return uhci_isochronous_transfer (dev->address, iso_src->endp,
      data, iso_src->max_packet, 100, DIR_IN, 0);
}

static bool
uvc_init (USB_DEVICE_INFO * dev, USB_CFG_DESC * cfg)
{
  USB_SPD_CFG_DESC *scfgd;
  UVC_VS_CTL_PAR_BLOCK par;
  uint8_t tmp[1300];

  DLOG("Configuring UVC device ...");

  /* Set data source info manually for now */
  iso_src.endp = 1;
  iso_src.max_packet = 512;
  iso_src.sample_size = 0;

  memset(tmp, 0, 1300);
  memset(&par, 0, sizeof(UVC_VS_CTL_PAR_BLOCK));

  DLOG("Now, getting other speed configuration.");
  if (usb_get_descriptor(dev, USB_TYPE_SPD_CFG_DESC, 0, 0, 9, (addr_t)tmp)) {
    DLOG("Other Speed Configuration is not presented.");
  } else {
    scfgd = (USB_SPD_CFG_DESC *)tmp;
    DLOG("Other Speed Configuration:");
    DLOG("  bLength : 0x%x  bDescriptorType : 0x%x  wTotalLength : 0x%x",
        scfgd->bLength, scfgd->bDescriptorType, scfgd->wTotalLength);
    DLOG("  bNumInterfaces : 0x%x  bConfigurationValue : 0x%x",
        scfgd->bNumInterfaces, scfgd->bConfigurationValue);
  }

  /* There is only one configuration in Logitech Webcam Pro 9000 */
  DLOG("Set configuration to %d.", cfg->bConfigurationValue);
  usb_set_configuration(dev, cfg->bConfigurationValue);

  /* Now, negotiate with VS interface for streaming parameters */

  /* Manually set parameters */
#if 0
  par.bmHint = 1; // Frame Interval Fixed
  par.bFormatIndex = 2;
  par.bFrameIndex = 2;
  par.dwFrameInterval = 0x61A80; // 25FPS
  par.wCompQuality = 5000; // 1 - 10000, with 1 the lowest
  //par.dwMaxPayloadTransferSize = 512;
#endif
  if (video_probe_controls (dev, SET_CUR, 1, &par)) {
    DLOG("Initial negotiation failed during probe");
  }
  delay(500);
  DLOG("Error Code : 0x%x", video_error_code (dev, GET_INFO, 1));
  para_block_dump (&par);
  memset(&par, 0, sizeof(UVC_VS_CTL_PAR_BLOCK));

  if (video_probe_controls (dev, GET_CUR, 1, &par)) {
    DLOG("Getting current state during probe failed");
  }
  delay(500);
  DLOG("Error Code : 0x%x", video_error_code (dev, GET_INFO, 1));
  para_block_dump (&par);
  memset(&par, 0, sizeof(UVC_VS_CTL_PAR_BLOCK));

  if (video_probe_controls (dev, GET_CUR, 1, &par)) {
    DLOG("Getting current state during probe failed");
  }
  delay(500);
  DLOG("Error Code : 0x%x", video_error_code (dev, GET_INFO, 1));
  para_block_dump (&par);

  par.bFormatIndex = 2;
  par.bFrameIndex = 2;

  if (video_commit_controls (dev, SET_CUR, 1, &par)) {
    DLOG("Setting device state during probe failed");
  }
  delay(500);
  DLOG("Error Code : 0x%x", video_error_code (dev, GET_INFO, 1));
  para_block_dump (&par);
  memset(&par, 0, sizeof(UVC_VS_CTL_PAR_BLOCK));

  if (video_commit_controls (dev, GET_CUR, 1, &par)) {
    DLOG("Setting device state during probe failed");
  }
  delay(500);
  DLOG("Error Code : 0x%x", video_error_code (dev, GET_INFO, 1));
  para_block_dump (&par);

  /* Select Alternate Setting 3 for interface 1 (Std VS interface) */
  /* This is for an isochronous endpoint with MaxPacketSize 512 */
  DLOG("Select Alternate Setting 3 for VS interface");
  if (usb_set_interface(dev, 3, 1)) {
    DLOG("Cannot configure interface setting for Std VS interface");
    return FALSE;
  }

  return TRUE;
}

static bool
uvc_probe (USB_DEVICE_INFO *dev, USB_CFG_DESC *cfg, USB_IF_DESC *ifd)
{
  /* Avoid multi-entrance in uhci_enumerate(), should be removed soon */
  static int entrance = 0;
  if (entrance) return FALSE;

  /* For now, we only support device with multi video interface
   * collections. This is ugly. But let's make Logitech Webcam Pro
   * 9000 work first.
   */
  if(!(dev->devd.bDeviceClass == 0xEF) ||
     !(dev->devd.bDeviceSubClass == 0x02) ||
     !(dev->devd.bDeviceProtocol == 0x01)) {
    return FALSE;
  }

#if 0
  /* Dumping UVC device descriptors */
  desc_dump (dev, cfg);
#endif

  if (uvc_init(dev, cfg) == FALSE) {
    DLOG("Device configuration failed!");
    return FALSE;
  }

#if 1
  uint8_t data[512];
  int i = 0;
  uint32_t *dump = (uint32_t*)data;
  memset(data, 0, 512);
  uvc_get_frame (dev, &iso_src, (addr_t) data, 512);

  for(i = 1; i < 100; i++) {
    putx(*dump);
    dump++;
    putchar('\n');
  }
#endif

  /* Avoid multi-entrance in uhci_enumerate(), should be removed soon */
  entrance++;

  return TRUE;
}

static int
video_error_code (
    USB_DEVICE_INFO * dev,
    uint8_t request,
    uint8_t interface)
{
  USB_DEV_REQ setup_req;
  uint8_t code, status;

  setup_req.bmRequestType = 0xA1;
  setup_req.bRequest = request;
  setup_req.wValue = VC_REQUEST_ERROR_CODE_CONTROL;
  setup_req.wIndex = interface;
  setup_req.wLength = 1;

  status = usb_control_transfer (dev, (addr_t) & setup_req,
      sizeof (USB_DEV_REQ), (addr_t) & code, 1);

  if (status) {DLOG("Getting error code failed!"); return 0xFF;}

  return code;
}

static int
video_probe_controls (
    USB_DEVICE_INFO * dev,
    uint8_t request,
    uint8_t interface,
    UVC_VS_CTL_PAR_BLOCK * par)
{
  USB_DEV_REQ setup_req;
  if (request == SET_CUR)
    setup_req.bmRequestType = 0x21;
  else
    setup_req.bmRequestType = 0xA1;
  setup_req.bRequest = request;
  setup_req.wValue = VS_PROBE_CONTROL;
  setup_req.wIndex = interface;
  setup_req.wLength = sizeof(UVC_VS_CTL_PAR_BLOCK);
  DLOG("Size of Parameter Block : %d bytes",
      sizeof(UVC_VS_CTL_PAR_BLOCK));

  return usb_control_transfer (dev, (addr_t) & setup_req,
      sizeof (USB_DEV_REQ), (addr_t) par, sizeof(UVC_VS_CTL_PAR_BLOCK));
}

static int
video_commit_controls (
    USB_DEVICE_INFO * dev,
    uint8_t request,
    uint8_t interface,
    UVC_VS_CTL_PAR_BLOCK * par)
{
  USB_DEV_REQ setup_req;
  if (request == SET_CUR)
    setup_req.bmRequestType = 0x21;
  else
    setup_req.bmRequestType = 0xA1;
  setup_req.bRequest = request;
  setup_req.wValue = VS_COMMIT_CONTROL;
  setup_req.wIndex = interface;
  setup_req.wLength = sizeof(UVC_VS_CTL_PAR_BLOCK);
  DLOG("Size of Parameter Block : %d bytes",
      sizeof(UVC_VS_CTL_PAR_BLOCK));

  return usb_control_transfer (dev, (addr_t) & setup_req,
      sizeof (USB_DEV_REQ), (addr_t) par, sizeof(UVC_VS_CTL_PAR_BLOCK));
}

static USB_DRIVER uvc_driver = {
  .probe = uvc_probe
};

bool
usb_uvc_driver_init (void)
{
  return usb_register_driver (&uvc_driver);
}

static void
para_block_dump (UVC_VS_CTL_PAR_BLOCK * par)
{
  DLOG ("Parameter Block Dump: ");
  DLOG ("  bmHint : 0x%x", par->bmHint);
  DLOG ("  bFormatIndex : 0x%x", par->bFormatIndex);
  DLOG ("  bFrameIndex : 0x%x", par->bFrameIndex);
  DLOG ("  dwFrameInterval : 0x%x", par->dwFrameInterval);
  DLOG ("  wKeyFrameRate : 0x%x", par->wKeyFrameRate);
  DLOG ("  wPFrameRate : 0x%x", par->wPFrameRate);
  DLOG ("  wCompQuality : 0x%x", par->wCompQuality);
  DLOG ("  wCompWindowSize : 0x%x", par->wCompWindowSize);
  DLOG ("  wDelay : 0x%x", par->wDelay);
  DLOG ("  dwMaxVideoFrameSize : 0x%x", par->dwMaxVideoFrameSize);
  DLOG ("  dwMaxPayloadTransferSize : 0x%x", par->dwMaxPayloadTransferSize);
  DLOG ("  dwClockFrequency : 0x%x", par->dwClockFrequency);
  DLOG ("  bmFramingInfo : 0x%x", par->bmFramingInfo);
  DLOG ("  bPreferedVersion : 0x%x", par->bPreferedVersion);
  DLOG ("  bMinVersion : 0x%x", par->bMinVersion);
  DLOG ("  bMaxVersion : 0x%x", par->bMaxVersion);
}

static void
desc_dump (USB_DEVICE_INFO *dev, USB_CFG_DESC *cfg)
{
  /* Dump all the descriptors retreived from UVC device */
  uint8_t conf[1300];

  USB_DEV_DESC *desc;
  USB_CFG_DESC *cfgd;
  UVC_IA_DESC *iad;
  USB_IF_DESC *vcifd;
  UVC_CSVC_IF_HDR_DESC *csvcifd;

  DLOG("Dumping UVC device descriptors");

  desc = &(dev->devd);

#if 1
  print("Device Descriptor: \n");
  print("  bLength : ");
  putx(desc->bLength);
  putchar('\n');
  print("  bDescriptorType : ");
  putx(desc->bDescriptorType);
  putchar('\n');
  print("  bcdUSB : ");
  putx(desc->bcdUSB);
  putchar('\n');
  print("  bDeviceClass : ");
  putx(desc->bDeviceClass);
  putchar('\n');
  print("  bDeviceSubClass : ");
  putx(desc->bDeviceSubClass);
  putchar('\n');
  print("  bDeviceProtocol : ");
  putx(desc->bDeviceProtocol);
  putchar('\n');
  print("  bMaxPacketSize0 : ");
  putx(desc->bMaxPacketSize0);
  putchar('\n');
  print("  idVendor : ");
  putx(desc->idVendor);
  putchar('\n');
  print("  idProduct : ");
  putx(desc->idProduct);
  putchar('\n');
  print("  bNumConfigurations : ");
  putx(desc->bNumConfigurations);
  putchar('\n');
#endif

  cfgd = (USB_CFG_DESC*)cfg;

#if 1
  print("Configuration Descriptor: \n");
  print("  bLength : ");
  putx(cfgd->bLength);
  putchar('\n');
  print("  bDescriptorType : ");
  putx(cfgd->bDescriptorType);
  putchar('\n');
  print("  wTotalLength : ");
  putx(cfgd->wTotalLength);
  putchar('\n');
  print("  bNumInterfaces : ");
  putx(cfgd->bNumInterfaces);
  putchar('\n');
  print("  bConfigurationValue : ");
  putx(cfgd->bConfigurationValue);
  putchar('\n');
  print("  iConfiguration : ");
  putx(cfgd->iConfiguration);
  putchar('\n');
  print("  bmAttributes : ");
  putx(cfgd->bmAttributes);
  putchar('\n');
  print("  MaxPower : ");
  putx(cfgd->MaxPower);
  putchar('\n');
#endif

  memset(conf, 0, 1300);

  print("Getting all descriptors.\n");
  usb_get_descriptor(dev, USB_TYPE_CFG_DESC, 0, 0, cfgd->wTotalLength, (addr_t)conf);
  iad = (UVC_IA_DESC*)(&conf[cfgd->bLength]);

#if 1
  print("Interface Association Descriptor: \n");
  print("  bLength : ");
  putx(iad->bLength);
  putchar('\n');
  print("  bDescriptorType : ");
  putx(iad->bDescriptorType);
  putchar('\n');
  print("  bFirstInterface : ");
  putx(iad->bFirstInterface);
  putchar('\n');
  print("  bInterfaceCount : ");
  putx(iad->bInterfaceCount);
  putchar('\n');
  print("  bFunctionClass : ");
  putx(iad->bFunctionClass);
  putchar('\n');
  print("  bFunctionSubClass : ");
  putx(iad->bFunctionSubClass);
  putchar('\n');
  print("  bFunctionProtocol : ");
  putx(iad->bFunctionProtocol);
  putchar('\n');
  print("  iFunction : ");
  putx(iad->iFunction);
  putchar('\n');
#endif

  vcifd = (USB_IF_DESC*)(&conf[cfgd->bLength + iad->bLength]);

#if 1
  print("VC Interface Descriptor : \n");
  print("  bLength : ");
  putx(vcifd->bLength);
  putchar('\n');
  print("  bDescriptorType : ");
  putx(vcifd->bDescriptorType);
  putchar('\n');
  print("  bInterfaceNumber : ");
  putx(vcifd->bInterfaceNumber);
  putchar('\n');
  print("  bAlternateSetting : ");
  putx(vcifd->bAlternateSetting);
  putchar('\n');
  print("  bNumEndpoints : ");
  putx(vcifd->bNumEndpoints);
  putchar('\n');
  print("  bInterfaceClass : ");
  putx(vcifd->bInterfaceClass);
  putchar('\n');
  print("  bInterfaceSubClass : ");
  putx(vcifd->bInterfaceSubClass);
  putchar('\n');
  print("  bInterfaceProtocol : ");
  putx(vcifd->bInterfaceProtocol);
  putchar('\n');
  print("  iInterface : ");
  putx(vcifd->iInterface);
  putchar('\n');
#endif

  csvcifd = (UVC_CSVC_IF_HDR_DESC*)(&conf[cfgd->bLength + iad->bLength + vcifd->bLength]);

#if 1
  print("Class-Specific VC Interface Header Descriptor : \n");
  print("  bLength : ");
  putx(csvcifd->bLength);
  putchar('\n');
  print("  bDescriptorType : ");
  putx(csvcifd->bDescriptorType);
  putchar('\n');
  print("  bDescriptorSubType : ");
  putx(csvcifd->bDescriptorSubType);
  putchar('\n');
  print("  bcdUVC : ");
  putx(csvcifd->bcdUVC);
  putchar('\n');
  print("  wTotalLength : ");
  putx(csvcifd->wTotalLength);
  putchar('\n');
  print("  dwClockFrequency : ");
  putx(csvcifd->dwClockFrequency);
  putchar('\n');
  print("  bInCollection : ");
  putx(csvcifd->bInCollection);
  putchar('\n');
  print("  baInterface1 : ");
  putx(csvcifd->baInterface1);
  putchar('\n');
#endif

  DLOG("Dumping UVC device descriptors ends");

#if 1

  uint32_t *dump = (uint32_t*)conf;
  int i = 0;

  for(i = 1; i < 325; i++) {
    putx(*dump);
    dump++;
    putchar('\n');
  }
#endif
  
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
