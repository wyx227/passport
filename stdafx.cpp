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


// stdafx.cpp : source file that includes just the standard includes
// PassPort.pch will be the pre-compiled header
// stdafx.obj will contain the pre-compiled type information

#include "stdafx.h"
using namespace System::Diagnostics;
using namespace System::Text;
using namespace System;
using namespace System::Xml;
using namespace System::Collections::Generic;
using namespace System::Threading;
using namespace System::Runtime::InteropServices;

#include "constants.h"

void debug(String ^p_s)
{
	/*
	System::IO::StreamWriter ^sw = gcnew System::IO::StreamWriter("c:\passport.log",1);
	sw->WriteLine(p_s);
	sw->Close();
	*/
	//log file not working...would be great for debug

	EventLog ^log = gcnew EventLog("Application");

	log->WriteEntry(LOG_SOURCE, p_s, EventLogEntryType::Information);

}