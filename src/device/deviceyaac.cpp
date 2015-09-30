/*
 * boblight
 * Copyright (C) tim.helloworld  2013
 * 
 * boblight is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * boblight is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "deviceyaac.h"
#include "util/log.h"
#include "util/misc.h"
#include "util/timeutils.h"

#define YAAC_VID       0x04D8
#define YAAC_PID       0x0042
#define YAAC_INTERFACE 0
#define YAAC_TIMEOUT   100

CDeviceYAAC::CDeviceYAAC(CClientsHandler& clients) : CDeviceUsb(clients)
{
  m_usbcontext    = NULL;
  m_devicehandle  = NULL;
	m_delay = 0;
  memset(m_serial, 0, sizeof(m_serial));
}

void CDeviceYAAC::Sync()
{
  if (m_allowsync)
    m_timer.Signal();
}

bool CDeviceYAAC::SetupDevice()
{
  int error;
  if ((error = libusb_init(&m_usbcontext)) != LIBUSB_SUCCESS)
  {
    LogError("%s: error setting up usb context, error:%i %s", m_name.c_str(), error, UsbErrorName(error));
    m_usbcontext = NULL;
    return false;
  }

  if (m_debug)
    libusb_set_debug(m_usbcontext, 3);

  libusb_device** devicelist;
  ssize_t         nrdevices = libusb_get_device_list(m_usbcontext, &devicelist);

  for (ssize_t i = 0; i < nrdevices; i++)
  {
    libusb_device_descriptor descriptor;
    error = libusb_get_device_descriptor(devicelist[i], &descriptor);
    if (error != LIBUSB_SUCCESS)
    {
      LogError("%s: error getting device descriptor for device %zi, error %i %s", m_name.c_str(), i, error, UsbErrorName(error));
      continue;
    }

    //try to find a usb device with the YAAC vendor and product ID
    if ((descriptor.idVendor == YAAC_VID && descriptor.idProduct == YAAC_PID))
    {
      int busnumber = libusb_get_bus_number(devicelist[i]);
      int deviceaddress = libusb_get_device_address(devicelist[i]);

			Log("%s: found YAAC at bus %d address %d", m_name.c_str(), busnumber, deviceaddress);

      if (m_devicehandle == NULL &&
          (( m_busnumber == -1 || m_deviceaddress == -1 || ( m_busnumber == busnumber && m_deviceaddress == deviceaddress ))))
      {
        libusb_device_handle *devhandle;

        error = libusb_open(devicelist[i], &devhandle);
        if (error != LIBUSB_SUCCESS)
        {
          LogError("%s: error opening device, error %i %s", m_name.c_str(), error, UsbErrorName(error));
          return false;
        }

        if ((error=libusb_detach_kernel_driver(devhandle, YAAC_INTERFACE)) != LIBUSB_SUCCESS && error != LIBUSB_ERROR_NOT_FOUND) {
          LogError("%s: error detaching interface %i, error:%i %s", m_name.c_str(), YAAC_INTERFACE, error, UsbErrorName(error));
          return false;
        }

        if ((error = libusb_claim_interface(devhandle, YAAC_INTERFACE)) != LIBUSB_SUCCESS)
        {
          LogError("%s: error claiming interface %i, error:%i %s", m_name.c_str(), YAAC_INTERFACE, error, UsbErrorName(error));
          return false;
        }

        m_devicehandle = devhandle;

        Log("%s: YAAC is initialized, bus %d device address %d", m_name.c_str(), busnumber, deviceaddress);
      }
    }
  }

  libusb_free_device_list(devicelist, 1);

  if (m_devicehandle == NULL)
  {
    if(m_busnumber == -1 || m_deviceaddress == -1)
      LogError("%s: no YAAC device with vid %04x and pid %04x found", m_name.c_str(), YAAC_VID, YAAC_PID);
    else
      LogError("%s: no YAAC device with vid %04x and pid %04x found at bus %i, address %i", m_name.c_str(), YAAC_VID, YAAC_PID, m_busnumber, m_deviceaddress);

    return false;
  }

  m_timer.SetInterval(m_interval);

  // four byte header (0x00 x 4)
  m_headerSize = 4;
  
  // footer length is at least n/2 bits of "1".
  m_footerSize = ((m_channels.size() / 3 + 1) / 2 + 7) / 8;

  m_buffsize = 3 + m_headerSize + m_footerSize + 4 * (m_channels.size() / 3);
  m_buff = new uint8_t[m_buffsize];

  return true;
}

bool CDeviceYAAC::WriteOutput()
{
  //get the channel values from the clientshandler
  int64_t now = GetTimeUs();
  m_clients.FillChannels(m_channels, now, this);

  m_buff[0] = 'D';
	m_buff[1] = (m_buffsize - 3) % 0xFF;
	m_buff[2] = (m_buffsize - 3) / 0xFF;

  int idx = 3;

  for (int i = 0; i < m_headerSize; i++) {
    m_buff[idx++] = 0x00;
  }

  //put the values in the buffer
  for (int i = 0; i < m_channels.size(); i += 3) {
    // this very much assumes that channels are in order RGB for each channel
    double r = m_channels[i].GetValue(now);
    double g = m_channels[i + 1].GetValue(now);
    double b = m_channels[i + 2].GetValue(now);
    // get the overall amplitude value (a / 31)
    uint8_t a = (uint8_t) ceil(std::max(std::max(r, g), b) * 31);
    // normalise by the overall amplitude
    r *= 31.0 / a;
    g *= 31.0 / a;
    b *= 31.0 / a;
    // chip expects BGR order, because it was created by satan
    m_buff[idx++] = 0xE0 | a;
    m_buff[idx++] = (uint8_t) Clamp(b * 0xFF, 0, 0xFF);
    m_buff[idx++] = (uint8_t) Clamp(g * 0xFF, 0, 0xFF);
    m_buff[idx++] = (uint8_t) Clamp(r * 0xFF, 0, 0xFF);
  }

  for (int i = 0; i < m_footerSize; i++) {
    m_buff[idx++] = 0xFF;
  }

	int bytes_transferred;
	int result = libusb_interrupt_transfer(
				m_devicehandle,
				0x01,
				m_buff,
				3,
				&bytes_transferred,
				1000);
	int result2 = libusb_interrupt_transfer(
				m_devicehandle,
				0x01,
				m_buff + 3,
				m_buffsize - 3,
				&bytes_transferred,
				1000);
  m_timer.Wait();

  return result == LIBUSB_SUCCESS && result2 == LIBUSB_SUCCESS;
}

void CDeviceYAAC::CloseDevice()
{
  if (m_devicehandle != NULL)
  {
    libusb_release_interface(m_devicehandle, YAAC_INTERFACE);
    libusb_attach_kernel_driver(m_devicehandle, YAAC_INTERFACE);
    libusb_close(m_devicehandle);

    m_devicehandle = NULL;
  }

  if (m_usbcontext)
  {
    libusb_exit(m_usbcontext);
    m_usbcontext = NULL;
  }
}

bool CDeviceYAAC::SetDelay(int delay) {
	if (delay < 0 || delay > 0xFF) return false;
	m_delay = (uint8_t) delay;
}

const char* CDeviceYAAC::UsbErrorName(int errcode)
{
#ifdef HAVE_LIBUSB_ERROR_NAME
  return libusb_error_name(errcode);
#else
  return "";
#endif
}


