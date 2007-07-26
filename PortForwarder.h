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

#include <winsock2.h>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Threading;

namespace PassPort {

public ref class PortForwarder
{
public:
	PortForwarder(String ^srcAddr, String ^srcPort, String ^trgAddr, String ^trgPort);
	~PortForwarder(void);
	void Run();
	static void Init();
	static void ShutDown();
	static List<int> ^oldThreads = gcnew List<int>;
private: 
	static List<Thread^> ^forwarders = gcnew List<Thread^>;	
	
private:
	String ^ srcAddr;
	String ^ srcPort;
	String ^ trgAddr;
	String ^ trgPort;
};

}