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
	

PortForwarder::PortForwarder(String ^srcAddr, String ^srcPort, String ^trgAddr, String ^trgPort,String ^proto)
{ 
	this->srcAddr = srcAddr;
	this->srcPort = srcPort;
	this->trgAddr = trgAddr;
	this->trgPort = trgPort;
	if (proto != nullptr)
		this->proto = proto;
	else
		this->proto = "tcp";
}

PortForwarder::~PortForwarder(void)
{
}


void PortForwarder::Init(){
	
	EventLog ^log = gcnew EventLog("Application");

	XmlDocument ^xd = gcnew XmlDocument();

	log->WriteEntry(LOG_SOURCE, "PortForwarder::Init", EventLogEntryType::Information);

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
		XmlElement ^proto = static_cast<XmlElement^>(fw->GetElementsByTagName("Protocol")->Item(0));

		PortForwarder ^pf = gcnew PortForwarder(src->GetAttribute("address"), src->GetAttribute("port"), 
			trg->GetAttribute("address"), trg->GetAttribute("port"), proto->GetAttribute("type"));
		
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

struct Udp_param
{
	sockaddr_in m_addr;
	SOCKET m_socket_src,m_socket_dst;
};

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
	delete[] socks;
    return 0;
}


static DWORD WINAPI reader_udp(LPVOID lpParameter)
{
    //SOCKET *socks = (SOCKET*)lpParameter;
	Udp_param *p_Udp_param = (Udp_param*)lpParameter;
/*
struct Udp_param
{
	sockaddr_in m_addr;
	SOCKET m_socket_src,m_socket_dst;
};
*/

	//debug("Started reading_udp thread");
	int n;
	sockaddr_in client_source;
	int client_source_length = sizeof(client_source);
	try {
		char buf[65536];
		

		//A TIMEOUT could be usefull,because the udp will persist until service is stoped, udp doesn't generate
		//exception because it's stateless...

		//while ((n = recv(socks[0], buf, sizeof(buf), 0)) > 0) {
		while ((n = recvfrom(p_Udp_param->m_socket_src, buf, sizeof(buf), 0,(sockaddr*)&client_source, &client_source_length )) > 0) {
			//debug("READER_UDP ::RECEIVED ON THE OTHER END!!! RECEND");
		
			//send(socks[1], buf, n, 0);
			sendto(p_Udp_param->m_socket_dst,buf,n,0,(sockaddr*)&(p_Udp_param->m_addr),sizeof(p_Udp_param->m_addr));
		}
	} catch (...) {};

	closesocket(p_Udp_param->m_socket_src);//close only this, other is still in used!!
	delete lpParameter;
 
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
	delete[] socks;

    return 0;
}


static int forward_tcp(const char *srcAddr, const int srcPort, const char *trgAddr, const int trgPort)
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
	SOCKET *socks = new SOCKET[2];

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
		delete[] socks;
		}
	};

    closesocket(s);

    return 0;
}



SOCKET Create_socket_and_bind_udp() 
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);

	
	sockaddr_in t_socket_addr;

	t_socket_addr.sin_family = AF_INET;
	t_socket_addr.sin_addr.S_un.S_addr =htonl(INADDR_ANY);

	t_socket_addr.sin_port = htons(0);


	if (bind(s, (sockaddr*)&t_socket_addr, sizeof(t_socket_addr)) != 0) {
		//log->WriteEntry(LOG_SOURCE, String::Format("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()), EventLogEntryType::Error);
        return 0;
    }

	return(s);

}




static int forward_udp(const char *srcAddr, const int srcPort, const char *trgAddr, const int trgPort)
{
    WORD wVersionRequested;
    WSADATA wsaData;

	EventLog ^log = gcnew EventLog("Application");
 
    wVersionRequested = MAKEWORD( 2, 2 );
    WSAStartup( wVersionRequested, &wsaData );

    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);

	struct hostent *hent = NULL;	

	if ((hent = strlen(srcAddr) > 0 ? gethostbyname(srcAddr) : gethostbyname("127.0.0.1")) == NULL) {
		log->WriteEntry(LOG_SOURCE, String::Format("Unknown host: {0}", gcnew String(srcAddr)), EventLogEntryType::Error);
        return 1;
	}	

	sockaddr_in sin;
	sockaddr_in client_source;
	sockaddr_in client_dest;


    //sin.sin_addr.S_un.S_addr = inet_addr(srcAddr);	
	memcpy(&sin.sin_addr, hent->h_addr_list[0], sizeof(sin.sin_addr));
	//sin.sin_family = AF_INET;
	sin.sin_family = hent->h_addrtype;
    sin.sin_port = htons(srcPort);
	
    if (bind(s, (sockaddr*)&sin, sizeof(sin)) != 0) {
		log->WriteEntry(LOG_SOURCE, String::Format("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()), EventLogEntryType::Error);
        return 1;
    }

	//Sleep(1000*30);
	//debug("After sleep, should bind ok..");
	HANDLE rt = 0;


//	try {
		char buf[65536];
		int n;
		int client_source_length=sizeof(client_source);

		//set destionation to send to, this is the target addr and port
		memset(&client_dest,0,sizeof(client_dest));
		client_dest.sin_family = AF_INET;
		client_dest.sin_addr.S_un.S_addr = inet_addr(trgAddr);
		client_dest.sin_port = htons(trgPort);


		while ((n = recvfrom(s, buf, sizeof(buf), 0,(sockaddr*)&client_source, &client_source_length )) > 0) 
		{
	
			int create_thread=0;
			//debug("Received some bytes");

			SortedDictionary<u_short,SOCKET> ^port_list;
			SOCKET ts;

			if (PassPort::PortForwarder::udp_hosts->TryGetValue (client_source.sin_addr.S_un.S_addr ,port_list))
			{
				if (port_list->TryGetValue(client_source.sin_port,ts))
				{
					//found socket, thread and write the buff?
					//debug("found this connnection, should receive bytes");
					
				}
				else //found host,not port
				{
					ts = Create_socket_and_bind_udp();
					port_list->Add(client_source.sin_port, ts );
					create_thread=1;
				}

			}//not found host
			else
			{
				port_list = gcnew SortedDictionary<u_short,SOCKET>;
				ts = Create_socket_and_bind_udp();
				port_list->Add(client_source.sin_port, ts );
				PassPort::PortForwarder::udp_hosts->Add(client_source.sin_addr.S_un.S_addr,port_list);
				create_thread=1;
				//debug("Not found, added to list(host and ip)");
			}

			sendto(ts,buf,n,0,(sockaddr*)&client_dest,sizeof(client_dest));

			if (create_thread)
			{
				try {

/*
				SOCKET *socks = new SOCKET[2];
				socks[0] = ts;
				socks[1] = s;
*/			
				Udp_param *p_Udp_param = new Udp_param;

				p_Udp_param->m_addr=client_source;
				p_Udp_param->m_socket_src = ts;
				p_Udp_param->m_socket_dst = s;
				
				DWORD id;
				rt = CreateThread(NULL, 0, reader_udp, p_Udp_param, 0, &id);

				PassPort::PortForwarder::oldThreads->Add((int)rt);

				log->WriteEntry(LOG_SOURCE, String::Format("UDP : Established connection to: {0}:{1}", gcnew String(trgAddr), (gcnew Int32(trgPort))->ToString()), EventLogEntryType::Information);
	

				} catch (ThreadAbortException ^tae) {
					if (rt != 0) {
						TerminateThread(rt, 0);
					}
				};

			}//create_thread

			

		}//while
//	} catch (...) {};

	
	//debug("exited while already");
    closesocket(s);

    return 0;

	
}





namespace PassPort {
void PortForwarder::Run() {

	if (this->proto=="tcp") {
	forward_tcp((const char *)(void*)Marshal::StringToHGlobalAnsi(this->srcAddr), 
		    Int32::Parse(this->srcPort), 
			(const char *)(void*)Marshal::StringToHGlobalAnsi(this->trgAddr), 
			Int32::Parse(this->trgPort));
	}
	else {  
	forward_udp((const char *)(void*)Marshal::StringToHGlobalAnsi(this->srcAddr), 
		    Int32::Parse(this->srcPort), 
			(const char *)(void*)Marshal::StringToHGlobalAnsi(this->trgAddr), 
			Int32::Parse(this->trgPort));

	}
}//Run

}//namesapce Passport
