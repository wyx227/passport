/*
	Copyrignt (C) 2007 Piotr J. Socko (psocko@gmail.com), QuantumG (gq@biodome.org)

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


#include "StdAfx.h"
#include "PortForwarder.h"
#include "constants.h"

#include <stdio.h>
#include <ws2tcpip.h>

using namespace System::Diagnostics;
using namespace System::Text;
using namespace System;
using namespace System::Xml;
using namespace System::Collections::Generic;
using namespace System::Threading;
using namespace System::Runtime::InteropServices;

namespace PassPort {


PortForwarder::PortForwarder(String ^srcAddr, String ^srcPort, String ^trgAddr, String ^trgPort)
{ 
	this->srcAddr = srcAddr;
	this->srcPort = srcPort;
	this->trgAddr = trgAddr;
	this->trgPort = trgPort;
}

PortForwarder::~PortForwarder(void)
{
}


void PortForwarder::Init(){
	EventLog ^log = gcnew EventLog("Application");
	XmlDocument ^xd = gcnew XmlDocument();
	try {
		xd->Load(String::Concat(AppDomain::CurrentDomain->SetupInformation->ApplicationBase, 
			gcnew String(CONFIG_FILE)));
	} catch (System::IO::FileNotFoundException ^e) {
		//There is no configuration file. Don't need to load anything. 
		log->WriteEntry(LOG_SOURCE, "No configuration file found. No socket to bind!", EventLogEntryType::Information);
		return;
	}
	XmlElement ^pp = xd->DocumentElement;
	XmlNodeList ^fwds = pp->GetElementsByTagName("Forward");

	for (int i = 0; i < fwds->Count; i++) {
		XmlElement ^fw = static_cast<XmlElement^>(fwds->Item(i));
		XmlElement ^src = static_cast<XmlElement^>(fw->GetElementsByTagName("Source")->Item(0));
		XmlElement ^trg = static_cast<XmlElement^>(fw->GetElementsByTagName("Target")->Item(0));
		PortForwarder ^pf = gcnew PortForwarder(src->GetAttribute("address"), src->GetAttribute("port"), 
			trg->GetAttribute("address"), trg->GetAttribute("port"));
		
		Thread ^ t = gcnew Thread(gcnew ThreadStart(pf, &PortForwarder::Run));
		forwarders->Add(t);
		t->Start();		
	}
}

void PortForwarder::ShutDown(){

	List<int>::Enumerator t = oldThreads->GetEnumerator();	
	while (t.MoveNext()) {		
		TerminateThread((HANDLE)t.Current, 0);
		CloseHandle((HANDLE)t.Current);
	}
	oldThreads->Clear();

	List<Thread^>::Enumerator e = forwarders->GetEnumerator();	
	while (e.MoveNext()) {		
		e.Current->Abort();
	}
	forwarders->Clear();
}
}

static DWORD WINAPI reader(LPVOID lpParameter)
{
    SOCKET *socks = (SOCKET*)lpParameter;

	try {
		char buf[65536];
		int n;
		while ((n = recv(socks[0], buf, sizeof(buf), 0)) > 0) {
			send(socks[1], buf, n, 0);
		}
	} catch (...) {};

    closesocket(socks[0]);
    closesocket(socks[1]);

    return 0;
}

static DWORD WINAPI writer(LPVOID lpParameter)
{
	SOCKET *socks = (SOCKET*)lpParameter;
	try {
		char buf[65536];
		int n;
		while ((n = recv(socks[1], buf, sizeof(buf), 0)) > 0) {
			send(socks[0], buf, n, 0);
		}
	} catch (...) {}

	closesocket(socks[0]);
    closesocket(socks[1]);

    return 0;
}


static int forward(const char *srcAddr, const int srcPort, const char *trgAddr, const int trgPort)
{
    WORD wVersionRequested;
    WSADATA wsaData;

	EventLog ^log = gcnew EventLog("Application");
 
    wVersionRequested = MAKEWORD( 2, 2 );
    WSAStartup( wVersionRequested, &wsaData );

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

	struct hostent *hent = NULL;	

	if ((hent = strlen(srcAddr) > 0 ? gethostbyname(srcAddr) : gethostbyname("127.0.0.1")) == NULL) {
		log->WriteEntry(LOG_SOURCE, String::Format("Unknown host: {0}", gcnew String(srcAddr)), EventLogEntryType::Error);
        return 1;
	}	

	sockaddr_in sin;
    //sin.sin_addr.S_un.S_addr = inet_addr(srcAddr);	
	memcpy(&sin.sin_addr, hent->h_addr_list[0], sizeof(sin.sin_addr));
	//sin.sin_family = AF_INET;
	sin.sin_family = hent->h_addrtype;
    sin.sin_port = htons(srcPort);
	
    if (bind(s, (sockaddr*)&sin, sizeof(sin)) != 0) {
		log->WriteEntry(LOG_SOURCE, String::Format("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()), EventLogEntryType::Error);
        return 1;
    }

	HANDLE rt = 0;
	HANDLE wt = 0;


	try {
		listen(s, 5);
		int ss = sizeof(sin);
		SOCKET n;
		while ((n = accept(s, (sockaddr*)&sin, &ss)) != -1) {
			SOCKET d = socket(AF_INET, SOCK_STREAM, 0);

			//Retrieve address of the target: 
			if ((hent = gethostbyname(trgAddr)) == NULL) {
				log->WriteEntry(LOG_SOURCE, String::Format("Unknown host: {0}", gcnew String(trgAddr)), EventLogEntryType::Error);
				return 1;
			}		

			//sin.sin_family = AF_INET;
			//sin.sin_addr.S_un.S_addr = inet_addr(trgAddr);
			memcpy(&sin.sin_addr, hent->h_addr_list[0], sizeof(sin.sin_addr));
			sin.sin_family = hent->h_addrtype;
			sin.sin_port = htons(trgPort);
			if (connect(d, (sockaddr*)&sin, sizeof(sin)) != 0) {
				log->WriteEntry(LOG_SOURCE, String::Format("Received a connection but can't connect to {0}:{1}", gcnew String(trgAddr), (gcnew Int32(trgPort))->ToString()), EventLogEntryType::Error);
				closesocket(n);
			} else {
				SOCKET *socks = new SOCKET[2];
				socks[0] = n;
				socks[1] = d;

				DWORD id;
				rt = CreateThread(NULL, 0, reader, socks, 0, &id);
				wt = CreateThread(NULL, 0, writer, socks, 0, &id);

				PassPort::PortForwarder::oldThreads->Add((int)rt);
				PassPort::PortForwarder::oldThreads->Add((int)wt);

				log->WriteEntry(LOG_SOURCE, String::Format("Established connection to: {0}:{1}", gcnew String(trgAddr), (gcnew Int32(trgPort))->ToString()), EventLogEntryType::Information);
			}
		}

	} catch (ThreadAbortException ^tae) {
		if (rt != 0) {
			TerminateThread(rt, 0);
		}
		if (wt != 0) {
			TerminateThread(wt, 0);
		}
	};

    closesocket(s);

    return 0;
}

namespace PassPort {
void PortForwarder::Run() {
	forward((const char *)(void*)Marshal::StringToHGlobalAnsi(this->srcAddr), 
		    Int32::Parse(this->srcPort), 
			(const char *)(void*)Marshal::StringToHGlobalAnsi(this->trgAddr), 
			Int32::Parse(this->trgPort));
}
}
