/*
	Copyrignt (C) 2007 Piotr J. Socko (psocko@gmail.com)

	This file is part of PassPort.

    PassPort is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    PassPort is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PassPort; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#define LOG_SOURCE	"PassPort (port forwarding)"
#define SRV_NAME	"PassPort (port forwarding)"
#define CONFIG_FILE	"\\PassPortConfig.xml"
#define LOG_FILE	"\\PassPort.log"



#ifdef _DEBUG
	#define UDP_TIMEOUT 1000*60*1;//1min
#else
	#define UDP_TIMEOUT 1000*60*5;//5min
#endif
