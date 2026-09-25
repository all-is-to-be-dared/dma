#ifdef __APPLE__

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <errno.h>
#include <termios.h>
#define _POSIX_C_SOURCE 199309L
#include <time.h>

#include <mach/mach_error.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <CoreFoundation/CFUUID.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <IOKit/usb/USBSpec.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOTypes.h>
#include <IOKit/usb/IOUSBHostFamilyDefinitions.h>
#include <IOKit/usb/USB.h>

#include "pup/host.h"
#include "pup/common.h"




static struct termios orig_termios;




static int
open_serial_dev(const char *path, uint32_t baud)
{
  int fd;
  struct termios tios;
  speed_t speed;
  unsigned long latency;

  opts.dev_path = strdup(path);
  speed = baud;

  // want open to be nonblocking (but not subsequent operations), so we clear O_NONBLOCK later on
  fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd == -1) {
    fprintf(stderr, "%s: failed to open %s: %s (%d)\n", opts.self, path, strerror(errno), errno);
    goto error;
  }

  if (ioctl(fd, TIOCEXCL) == -1) {
    fprintf(stderr, "%s: failed to lock %s: %s (%d)\n", opts.self, path, strerror(errno), errno);
    goto error;
  }

  if (fcntl(fd, F_SETFL, 0) == -1) {
    fprintf(
      stderr, "%s: failed to make %s block: %s (%d)\n", opts.self, path, strerror(errno), errno);
    goto error;
  }

  if (tcgetattr(fd, &orig_termios) == -1) {
    fprintf(stderr,
            "%s: failed to get tty attrs for %s: %s (%d)\n",
            opts.self,
            path,
            strerror(errno),
            errno);
    goto error;
  }

  tios = orig_termios;

  // Noncanonical mode:
  //  input bytes are not assembled into lines, and erase and kill processing does not occur.
  // Writing data and output processing:
  //  when a process writes one or more bytes to a terminal device file, they are processed
  //  according to the c_oflag field. The implementation may provide a buffering mechanism; as
  //  such, when a call to write() completes, all of the bytes written have been scheduled for
  //  transmission to the device, but the transmission will not necessarily have been
  //  completed.
  // Special characters:
  //  INTR    if ISIG enabled, generates SIGINT. (disable)
  //  QUIT    if ISIG enabled, generates SIGQUIT. (disable)
  //  ERASE   if ICANON set, erases last character in current line. (disable)
  //  KILL    if ICANON set, deletes the entire line. (disable)
  //  EOF     if ICANON set, (disable)
  //  CR      if ICANON set
  //  NL      if ICANON set
  //  EOL     if ICANON set
  //  SUSP    if ISIG enabled
  //  STOP    if IXON or IXOFF is set
  //  START   if IXON or IXOFF is set
  //  EOL2        same as EOL
  //  WERASE  if ICANON
  //  REPRINT if ICANON
  //  DSUSP       similar SUSP
  //  LNEXT   if IEXTEN set ; receipt of this character causes the next character to be taken
  //          literally
  //  DISCARD if IEXTEN set ; recept of this character toggles the flushing of terminal output
  //  STATUS  if ICANON
  // General Terminal Interface:
  //  Last process to close a terminal device file causes any output to be sent to the device
  //  and any input to be discarded.


  // INPUT MODES:
  //  c_iflag
  //      IGNBRK  = ignore BREAK condition
  //      BRKINT  = map BREAK to SIGINTR
  //      IGNPAR  = discard parity errors
  //      PARMRK  = mark parity and framing errors
  //      INPCK   = enable checking of parity errors
  //      ISTRIP  = strip 8th bit off chars
  //      INLCR   = map NL into CR
  //      IGNCR   = ignore CR
  //      ICRNL   = map CR to NL (aka CRMOD)
  //      IXON    = enable output flow control
  //      IXOFF   = enable input flow control
  //      IXANY   = any char will restart after stop
  //      IMAXBEL = ring bell on input queue full
  //      IUCLC   = translate upper case to lower case

  // ignore breaks (staff code had this wrong)
  tios.c_iflag |= IGNBRK;
  // Disable XON/XOFF flow control in both directions
  tios.c_iflag &= ~(IXON | IXOFF | IXANY);
  // Noncanonical mode
  tios.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);

  // OUTPUT MODES:
  //  c_oflag
  //      OPOST   = enable following ouptut processing
  //      ONLCR   = map NL to CR-NL (aka CRMOD)
  //      OXTABS  = expand tabs to spaces
  //      ONOEOT  = discard EOT's (^D) on output
  //      OCRNL   = map CR to NL
  //      OLCUC   = translate lower case to upper case
  //      ONOCR   = no CR output at column 0
  //      ONLRET  = NL performs CR function
  tios.c_oflag = 0;

  // CONTROL MODES:
  //  c_cflag
  //      CSIZE   = character size mask
  //      CS8
  //      CSTOPB  = send 2 stop bits
  //      CREAD   = enable receiver
  //      PARENB  = parity enable
  //      PARODD  = odd parity, else even
  //      HUPCL   = hang up on last close
  //      CLOCAL  = ignore modem status lines
  //      CCTS_OFLOW  = CTS flow control of output
  //      CRTSCTS = same as CCTS_OFLOW
  //      CRTS_IFLOW  = RTS flow control of input
  //      MDMBUF  = flow control output via character
  tios.c_cflag &= ~(CSIZE);
  tios.c_cflag |= CS8;
  tios.c_cflag &= ~(PARENB);
  tios.c_cflag &= ~(CSTOPB);
  // disable hardware flow control
  tios.c_cflag &= ~(CRTSCTS);
  // enable receiver & ignore modem control lines
  tios.c_cflag |= CREAD | CLOCAL;

  // LOCAL MODES:
  //  c_lflag
  //      ECHOKE  = visual erase for line kill
  //      ECHOE   = visually erase chars
  //      ECHO    = enable echoing
  //      ECHNOL  = echo NL even if ECHO is off
  //      ECHOPRT = visual eerase mode for hardcopy
  //      ECHOCTL = echo control chars as ^Char
  //      ISIG    = enable signals INTR QUIT [D]SUSP
  //      ICANON  = canonicalize input lines
  //      ALTWERASE   = use alternate WERASE algorithm
  //      IEXTEN  = enable DISCARD and LNEXT
  //      EXTPROC = external processing
  //      TOSTOP  = stop background jobs from output
  //      FLUSHO  = output being flushed (state)
  //      NOKERNINFO  = no kernel output from VSTATUS
  //      PENDIN  = XXX retype pending input (state)
  //      NOFLSH  = don't flush after interrupt
  tios.c_lflag = 0;

  // mode: MIN=0 TIME=0:
  //  minimum of either the number of bytes requested or the number of bytes currently
  //  available is returned without waiting for more bytes to be input. If no characters are
  //  available, read returns a value of zero, having read no data.
  tios.c_cc[VMIN] = 0;
  tios.c_cc[VTIME] = 0;

  tios.c_ospeed = B115200;
  tios.c_ispeed = B115200;

  if (tcsetattr(fd, TCSANOW, &tios) == -1) {
    fprintf(stderr,
            "%s: failed to set tty attrs for %s: %s (%d)\n",
            opts.self,
            path,
            strerror(errno),
            errno);
    goto error;
  }

  if (!opts.is_pty) {
    if (ioctl(fd, IOSSIOSPEED, &speed) == -1) {
      fprintf(stderr,
              "%s: failed to set baud rate for %s to %d: %s (%d)\n",
              opts.self,
              path,
              baud,
              strerror(errno),
              errno);
      goto error;
    }
    latency = 1UL;
    if (ioctl(fd, IOSSDATALAT, &latency) == -1) {
      fprintf(stderr,
              "%s: failed to set input latency for %s to %d: %s (%d)\n",
              opts.self,
              path,
              baud,
              strerror(errno),
              errno);
      goto error;
    }
  }

  return fd;

error:
  if (fd != -1) {
    close(fd);
  }

  return -1;
}




static kern_return_t
discover_serial_ports(io_iterator_t *matching_services)
{
  kern_return_t kr;
  CFMutableDictionaryRef matching_dict;

  // Serial devices are instances of IOSerialBSDClient (aka the value of kIOSerialBSDServiceValue),
  // so we match on such services.
  matching_dict = IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matching_dict) {
    fprintf(stderr, "IOServiceMatching returned a NULL dictionary.\n");
    return KERN_FAILURE;
  }
  CFDictionarySetValue(matching_dict, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDModemType));

  kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching_dict, matching_services);
  if (KERN_SUCCESS != kr) {
    fprintf(stderr, "IOServiceGetMatchingServices: error %d: %s\n", kr, mach_error_string(kr));
  }

  return kr;
}




static enum find_status
find_serial_device_auto(int *fd, uint32_t baud)
{
  // Enumeration behavior: the first serial modem that is also a IOUSBHostDevice

  io_iterator_t iter;
  char pbuf[PATH_MAX], serialbuf[128];
  io_object_t service;
  kern_return_t kr = KERN_FAILURE;
  Boolean found = false, result;
  CFTypeRef callout_dev, serial;
  io_registry_entry_t curr, next, device;
  int r;

  discover_serial_ports(&iter);

  pbuf[0] = '\0';

  while ((service = IOIteratorNext(iter)) && !found) {
    callout_dev =
      IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
    if (callout_dev) {
      result = CFStringGetCString(callout_dev, pbuf, PATH_MAX, kCFStringEncodingUTF8);
      CFRelease(callout_dev);

      if (result) {
        // now walk up through the registry tree to find the IOUSBHostDevice
        device = 0;
        curr = service;
        while (IORegistryEntryGetParentEntry(curr, kIOServicePlane, &next) == KERN_SUCCESS) {
          if (curr != service)
            (void)IOObjectRelease(curr);
          curr = next;
          if (IOObjectConformsTo(curr, "IOUSBHostDevice")) {
            device = curr;
            break;
          }
        }

        if (device) {
          serial = IORegistryEntryCreateCFProperty(
            device, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);
          if (serial) {
            result = CFStringGetCString(serial, serialbuf, sizeof serialbuf, kCFStringEncodingUTF8);
            if (result) {
              host_printf(
                DBG_MIN, HOST "Found matching serial device: %s, serial no. %s\n", pbuf, serialbuf);
              found = true;
            }
          }

          if (device != service)
            (void)IOObjectRelease(device);
        }
      }
    }

    (void)IOObjectRelease(service);
  }
  if (!found)
    return FIND_NXDEV;

  r = open_serial_dev(pbuf, baud);

  if (r == -1) {
    return FIND_OPEN_FAIL;
  } else {
    *fd = r;
    return FIND_OK;
  }
}




static enum find_status
find_serial_device_fuzzy(const char *string, int *fd, uint32_t baud)
{
  io_iterator_t iter;
  char pbuf[PATH_MAX], serialbuf[128];
  io_object_t service;
  kern_return_t kr = KERN_FAILURE;
  Boolean found = false, result;
  CFTypeRef callout_dev, serial;
  io_registry_entry_t curr, next, device;
  int r;

  discover_serial_ports(&iter);

  pbuf[0] = '\0';

  while ((service = IOIteratorNext(iter)) && !found) {
    callout_dev =
      IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
    if (callout_dev) {
      result = CFStringGetCString(callout_dev, pbuf, PATH_MAX, kCFStringEncodingUTF8);
      CFRelease(callout_dev);

      if (result) {
        // now walk up through the registry tree to find the IOUSBHostDevice
        device = 0;
        curr = service;
        while (IORegistryEntryGetParentEntry(curr, kIOServicePlane, &next) == KERN_SUCCESS) {
          if (curr != service)
            (void)IOObjectRelease(curr);
          curr = next;
          if (IOObjectConformsTo(curr, kIOUSBHostDeviceClassName)) {
            device = curr;
            break;
          }
        }

        if (device) {
          serial = IORegistryEntryCreateCFProperty(
            device, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, 0);
          if (serial) {
            result = CFStringGetCString(serial, serialbuf, sizeof serialbuf, kCFStringEncodingUTF8);
            if (result) {
              if (!strcmp(serialbuf, string)) {
                host_printf(DBG_MIN, HOST "Found matching serial device: %s\n", pbuf);
                found = true;
              }
            }
          }

          if (device != service)
            (void)IOObjectRelease(device);
        }
      }
    }

    (void)IOObjectRelease(service);
  }
  if (!found)
    return FIND_NXDEV;

  r = open_serial_dev(pbuf, baud);

  if (r == -1) {
    return FIND_OPEN_FAIL;
  } else {
    *fd = r;
    return FIND_OK;
  }
}




static enum find_status
find_serial_device_with_path(const char *dev, int *fd, uint32_t baud)
{
  int r;
  struct stat buf;

  r = stat(dev, &buf);
  if (!r) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-octal-literals"
    if (!S_ISCHR(buf.st_mode)) {
      return FIND_NOT_CHR;
    }
#pragma GCC diagnostic pop
  }
  r = open_serial_dev(dev, baud);

  if (r == -1) {
    return FIND_OPEN_FAIL;
  } else {
    *fd = r;
    return FIND_OK;
  }
}




enum find_status
find_serial_device(const char *dev, int *fd, uint32_t baud)
{
  if (!dev) {
    return find_serial_device_auto(fd, baud);
  } else if (*dev == '@') {
    return find_serial_device_fuzzy(dev + 1, fd, baud);
  } else {
    return find_serial_device_with_path(dev, fd, baud);
  }
}




enum reset_status
reset_tianleboard(int fd)
{
  CFMutableDictionaryRef matching_dict;
  kern_return_t kr;
  char callup_path[MAXPATHLEN];
  io_service_t serial_service;
  io_registry_entry_t device, curr_device, next_device;
  device = curr_device;

  if (-1 == fcntl(fd, F_GETPATH, callup_path)) {
    fprintf(stderr,
            "%s: failed to get path to serial device: %s (%d)\n",
            opts.self,
            strerror(errno),
            errno);
    return RS_FAIL;
  }

  matching_dict = IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matching_dict) {
    fprintf(stderr, "IOServiceMatching returned a NULL dictionary.\n");
    return KERN_FAILURE;
  }
  CFDictionarySetValue(
    matching_dict,
    CFSTR(kIOCalloutDeviceKey),
    CFStringCreateWithCString(kCFAllocatorDefault, callup_path, kCFStringEncodingUTF8));

  serial_service = IOServiceGetMatchingService(kIOMainPortDefault, matching_dict);
  if (serial_service == IO_OBJECT_NULL) {
    fprintf(stderr, "%s: device path %s not found in IOKit registry.\n", opts.self, callup_path);
    return RS_FAIL;
  }

  curr_device = serial_service;

  while (IORegistryEntryGetParentEntry(curr_device, kIOServicePlane, &next_device) ==
         KERN_SUCCESS) {
    if (curr_device != serial_service)
      (void)IOObjectRelease(curr_device);
    curr_device = next_device;
    if (IOObjectConformsTo(curr_device, kIOUSBHostDeviceClassName)) {
      device = curr_device;
      break;
    }
  }

  (void)IOObjectRelease(serial_service);

  // perform control transfer

  IOCFPlugInInterface **plugin_interface = NULL;
  IOUSBDeviceInterface320 **device_interface = NULL;
  int32_t score;

  kr = IOCreatePlugInInterfaceForService(
    device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin_interface, &score);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr,
            "%s: IOCreatePlugInInterfaceForService: %s (%d)\n",
            opts.self,
            mach_error_string(kr),
            kr);
    return RS_FAIL;
  }

  HRESULT res = (*plugin_interface)
                  ->QueryInterface(plugin_interface,
                                   CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
                                   (LPVOID *)&device_interface);
  if(res != S_OK || !device_interface) {
    fprintf(stderr, "%s: Failed to get device interface: %s (%d)\n", opts.self, mach_error_string(kr), kr);
    return RS_FAIL;
  }
  kr = IODestroyPlugInInterface(plugin_interface);
  assert(kr == KERN_SUCCESS);

  kr = (*device_interface)->USBDeviceOpenSeize(device_interface);
  if(kr != KERN_SUCCESS) {
    fprintf(stderr, "%s: Could not seize USB device: %s (%d)\n", opts.self, mach_error_string(kr), kr);
    (*device_interface)->Release(device_interface);
    return RS_FAIL;
  }

  // #define CP210X_VENDOR_SPECIFIC 0xff
  // #define CP210X_WRITE_LATCH 0x37e1
  // #define GPIO0_MASK 0x0001
  // #define GPIO0_HIGH 0x0101
  // #define GPIO0_LOW 0x0001
  // uint16_t wIndex = high ? GPIO0_HIGH : GPIO0_LOW;
  // int rc = libusb_control_transfer(
  //     cp210x_handle,
  //     LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
  //     CP210X_VENDOR_SPECIFIC,
  //     CP210X_WRITE_LATCH,
  //     wIndex,
  //     NULL,
  //     0,
  //     1000);
  // libusb_control_transfer (
  //  libusb_device_handle *dev_handle,
  //  uint8_t bmRequestType,
  //  uint8_t bRequest,
  //  uint16_t wValue,
  //  uint16_t wIndex,
  //  unsigned char *data,
  //  uint16_t wLength,
  //  unsigned int timeout)

  IOUSBDevRequest req;

  req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
  req.bRequest = 0xff; // cp210x vendor specific
  req.wValue = 0x37e1; // cp210x write latch
  req.wIndex = 0x0001; // GPIO0_LOW
  req.pData = NULL;
  req.wLength = 0;
  kr = (*device_interface)->DeviceRequest(device_interface, &req);
  if(kr != KERN_SUCCESS) {
    fprintf(stderr, "%s: low-latch transfer failed: %s (%d)\n", opts.self, mach_error_string(kr), kr);
    return RS_FAIL;
  }

  sleep(1);

  req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
  req.bRequest = 0xff; // cp210x vendor specific
  req.wValue = 0x37e1; // cp210x write latch
  req.wIndex = 0x0101; // GPIO0_LOW
  req.pData = NULL;
  req.wLength = 0;
  kr = (*device_interface)->DeviceRequest(device_interface, &req);
  if(kr != KERN_SUCCESS) {
    fprintf(stderr, "%s: low-latch transfer failed: %s (%d)\n", opts.self, mach_error_string(kr), kr);
    return RS_FAIL;
  }

  (*device_interface)->USBDeviceClose(device_interface);
  (*device_interface)->Release(device_interface);

  return RS_OK;
}




#endif
