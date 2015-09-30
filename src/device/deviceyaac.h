/*
 * boblight
 * Copyright (C) tim.helloworld 2013
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

#ifndef CDEVICEYAAC
#define CDEVICEYAAC

#include "deviceusb.h"
#include <libusb-1.0/libusb.h>

class CDeviceYAAC : public CDeviceUsb
{
  public:
    CDeviceYAAC(CClientsHandler& clients);
    void Sync();
		bool SetDelay(int delay);

  protected:

    bool SetupDevice();
    bool WriteOutput();
    void CloseDevice();


    const char* UsbErrorName(int errcode);

    CSignalTimer          m_timer;
    libusb_context*       m_usbcontext;
    libusb_device_handle* m_devicehandle;
		unsigned int m_buffsize;
    unsigned int m_headerSize, m_footerSize;
    uint8_t*        m_buff;
		uint8_t m_delay;
};

#endif //CDEVICEYAAC
