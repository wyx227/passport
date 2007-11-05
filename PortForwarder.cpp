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
#include "DebugLog.h"

#include <stdio.h>
#include <ws2tcpip.h>
#include <Winsock2.h>

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
	//if xmlload will not fail on old configfile formats, this will make it compatible
}

PortForwarder::~PortForwarder(void)
{
}


void PortForwarder::Init(){
	
	//EventLog ^log = gcnew EventLog("Application");

	XmlDocument ^xd = gcnew XmlDocument();

	//log->WriteEntry(LOG_SOURCE, "PortForwarder::Init", EventLogEntryType::Information);

	try {
		xd->Load(String::Concat(AppDomain::CurrentDomain->SetupInformation->ApplicationBase, 
			gcnew String(CONFIG_FILE)));
	} catch (System::IO::FileNotFoundException ^e) {
		//There is no configuration file. Don't need to load anything. 
		//log->WriteEntry(LOG_SOURCE, "No configuration file found. No socket to bind!", EventLogEntryType::Information);
		debug("No configuration file found. No socket to bind!");
		return;
	}

	h_Shutdown_Event = CreateEvent(0,TRUE,FALSE,0);

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
	debug("Passport started");
}

void PortForwarder::ShutDown(){

	debug("Service Stop received, terminating all threads");
	::SetEvent(h_Shutdown_Event); //issue shutdown event


//wait for all the writer and reader threads to close
	List<int>::Enumerator t = oldThreads->GetEnumerator();	
	while (t.MoveNext()) {		
		::WaitForSingleObject((HANDLE)t.Current,INFINITE);
		CloseHandle((HANDLE)t.Current);
	}
	oldThreads->Clear();


//wait threads to close, no need to force
	List<Thread^>::Enumerator e = forwarders->GetEnumerator();	
	while (e.MoveNext()) {		
		e.Current->Join();
	}
	forwarders->Clear();

	debug("Passport stopped");
}

}//namespace PassPort

struct Udp_param
{
	SOCKET m_socket_src,m_socket_dst;
	sockaddr_in m_addr;
};

struct Tcp_param
{
	SOCKET m_socket_src,m_socket_dst;
	HANDLE m_h_read;
};

static DWORD WINAPI reader(LPVOID lpParameter)
{

	WSAEVENT l_Events[2];

	char buf[65536];
	int n;
	DWORD Event;

	l_Events[0] = PassPort::PortForwarder::h_Shutdown_Event;//event for shutdown
	l_Events[1] = WSACreateEvent();//event for data received


	Tcp_param *p_Tcp_param = (Tcp_param*)lpParameter;
	debug("Started reader thread");



	::WSAEventSelect(p_Tcp_param->m_socket_src, l_Events[1],  FD_READ | FD_CLOSE );

	while(true)
	{
		debug("reader: waiting for events");

		if ((Event = WSAWaitForMultipleEvents(2, l_Events, FALSE,WSA_INFINITE, FALSE)) == WSA_WAIT_FAILED)
		{
			debug("tcp reader: WSAWaitForMultipleEvents failed with error {0:D}", WSAGetLastError());
			return 1;
		}

		debug("reader: EVENT!!");

		if (Event == WSA_WAIT_EVENT_0)
		{
			//shutdown event, close sockets and exit thread
			shutdown(p_Tcp_param->m_socket_src,SD_BOTH);
			closesocket(p_Tcp_param->m_socket_src);

			debug("Tcp reader thread received shutdown, shutting down");
			break;
		}
		else
		{
			//READ or CLOSE
			debug("reader: event read or close");

			WSANETWORKEVENTS NetworkEvents;

			if (WSAEnumNetworkEvents(p_Tcp_param->m_socket_src ,l_Events[1],	&NetworkEvents) == SOCKET_ERROR)
			{
				debug("WSAEnumNetworkEvents failed with error {0:D}", WSAGetLastError());
				return 1;
			}
			debug("reader : NetworkEvents.lNetworkEvents :{0:D} , FD_READ : {1:D} FD_CLOSE: {1:D}",NetworkEvents.lNetworkEvents,FD_READ,FD_CLOSE);
			if (NetworkEvents.lNetworkEvents & FD_READ ) 
			{
				if ((n = recv(p_Tcp_param->m_socket_src, buf, sizeof(buf), 0)) >0 )
				{
					debug("reader: Received : {0:s}",gcnew String(buf));
					send(p_Tcp_param->m_socket_dst, buf, n, 0);
				}
				debug("reader: Received bytes {0:d}",n);

			}

			if (NetworkEvents.lNetworkEvents & FD_CLOSE ) 
			{
				shutdown(p_Tcp_param->m_socket_src,SD_BOTH);
				closesocket(p_Tcp_param->m_socket_src); 

				shutdown(p_Tcp_param->m_socket_dst,SD_BOTH);
				closesocket(p_Tcp_param->m_socket_dst); 

			}
		}//socket event
	
	}//while(true)

	/* 
	wait for read thread to close only then we can delete 
	*/
	debug("End reader thread");

	return 0;
	
}


static DWORD WINAPI reader_udp(LPVOID lpParameter)
{

	WSAEVENT l_Events[2];

	char buf[65536];
	int n;
	DWORD Event;

	l_Events[0] = PassPort::PortForwarder::h_Shutdown_Event;//event for shutdown
	l_Events[1] = WSACreateEvent();//event for data received

	sockaddr_in client_source;
	int client_source_length = sizeof(client_source);

	Udp_param *p_Udp_param = (Udp_param*)lpParameter;


	debug("Started reading_udp thread");
	
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
 
	debug("Closed udp reading thread");
    return 0;
}

static DWORD WINAPI writer(LPVOID lpParameter)
{

	WSAEVENT l_Events[2];

	char buf[65536];
	int n;
	DWORD Event;

	l_Events[0] = PassPort::PortForwarder::h_Shutdown_Event;//event for shutdown
	l_Events[1] = WSACreateEvent();//event for data received


	Tcp_param *p_Tcp_param = (Tcp_param*)lpParameter;
	debug("Started writer thread");



	::WSAEventSelect(p_Tcp_param->m_socket_dst, l_Events[1],  FD_READ | FD_CLOSE );

	while(true)
	{
		debug("writer: waiting for events");

		if ((Event = WSAWaitForMultipleEvents(2, l_Events, FALSE,WSA_INFINITE, FALSE)) == WSA_WAIT_FAILED)
		{
			debug("tcp writer: WSAWaitForMultipleEvents failed with error {0:D}", WSAGetLastError());
			return 1;
		}
		debug("writer: event");
		if (Event == WSA_WAIT_EVENT_0)
		{
			debug("Tcp writer thread received shutdown, shutting down");
			//shutdown event, close sockets and exit thread
			shutdown(p_Tcp_param->m_socket_dst,SD_BOTH);
			closesocket(p_Tcp_param->m_socket_dst);

			debug("Tcp writer thread received shutdown, done ");
			break;
		}
		else
		{
			debug("writer: not shutdown");
			//READ or CLOSE

			WSANETWORKEVENTS NetworkEvents;

			if (WSAEnumNetworkEvents(p_Tcp_param->m_socket_dst ,l_Events[1],	&NetworkEvents) == SOCKET_ERROR)
			{
				debug("WSAEnumNetworkEvents failed with error {0:D}", WSAGetLastError());
				return 1;
			}
			debug("NetworkEvents.lNetworkEvents :{0:D} , FD_READ : {1:D}",NetworkEvents.lNetworkEvents,FD_READ);

			if (NetworkEvents.lNetworkEvents & FD_READ ) 
			{
				debug("Read event surely");
				if ( (n = recv(p_Tcp_param->m_socket_dst, buf, sizeof(buf), 0)) >0 )
				{
					debug("writer: Received : {0:s}",gcnew String(buf));
					send(p_Tcp_param->m_socket_src, buf, n, 0);
				}
				debug("writer: Received bytes {0:d}",n);
	
			}

			if (NetworkEvents.lNetworkEvents & FD_CLOSE ) 
			{
				shutdown(p_Tcp_param->m_socket_dst,SD_BOTH);
				closesocket(p_Tcp_param->m_socket_dst); //this sends the FD_CLOSE to reader

			}
		}//socket event
	
	}//while(true)

	/* 
	wait for read thread to close only then we can delete 
	*/
	::WaitForSingleObject(p_Tcp_param->m_h_read,INFINITE);

	delete lpParameter;
	debug("End writer thread");

	return 0;
	
}


static int forward_tcp(const char *srcAddr, const int srcPort, const char *trgAddr, const int trgPort)
{
    WORD wVersionRequested;
    WSADATA wsaData;

	debug("Start PortForwarder tcp thread");

	EventLog ^log = gcnew EventLog("Application");
 
    wVersionRequested = MAKEWORD( 2, 2 );
    WSAStartup( wVersionRequested, &wsaData );

    SOCKET ListeningSocket = socket(AF_INET, SOCK_STREAM, 0);

	struct hostent *hent = NULL;	

	if ((hent = strlen(srcAddr) > 0 ? gethostbyname(srcAddr) : gethostbyname("127.0.0.1")) == NULL) {
		debug("Unknown host: {0}", gcnew String(srcAddr) );
        return 1;
	}	

	sockaddr_in sin;
    //sin.sin_addr.S_un.S_addr = inet_addr(srcAddr);	
	memcpy(&sin.sin_addr, hent->h_addr_list[0], sizeof(sin.sin_addr));
	//sin.sin_family = AF_INET;
	sin.sin_family = hent->h_addrtype;
    sin.sin_port = htons(srcPort);
	
    if (bind(ListeningSocket, (sockaddr*)&sin, sizeof(sin)) != 0) {
		//log->WriteEntry(LOG_SOURCE, String::Format("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()), EventLogEntryType::Error);
		debug("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()  );
		//maybe retry cuple of times
        return 1;
    }
	debug("Binded to port {0}:{1} tcp successful ", gcnew String(srcAddr), (new Int32(srcPort))->ToString()  );


	HANDLE rt = 0;
	HANDLE wt = 0;
	DWORD Event;
	//SOCKET *socks = new SOCKET[2];

//	try {
		listen(ListeningSocket, 5);
		int ss = sizeof(sin);
		WSAEVENT l_Events[2];
		SOCKET ClientSocket_from ;//= socket(AF_INET, SOCK_STREAM, 0);

		l_Events[0] = PassPort::PortForwarder::h_Shutdown_Event;//event for shutdown
		l_Events[1] = WSACreateEvent();//event for new connection

		::WSAEventSelect(ListeningSocket, l_Events[1],  FD_ACCEPT);

		while(1) {
		//while ((n = accept(s, (sockaddr*)&sin, &ss)) != -1) {
			debug("forward tcp : Waiting for ACCEPT EVENTS");
			if ((Event = WSAWaitForMultipleEvents(2, l_Events, FALSE,WSA_INFINITE, FALSE)) == WSA_WAIT_FAILED)
			{
				 debug("WSAWaitForMultipleEvents failed with error {0:D}", WSAGetLastError());
      			 return 1;
			}
			debug("forward tcp : EVENT ");
			
			if (Event == WSA_WAIT_EVENT_0)
			{
				//shutdown event, close sockets and exit thread
				shutdown(ListeningSocket,SD_BOTH);
				closesocket(ListeningSocket);
				debug("Tcp forward thread received shutdown, shutting down");
				return(0);
			}
			else
			{
				debug("forward tcp : Not shutdown event ");
				//most probably if not shutdown then accept but just be sure check

				WSANETWORKEVENTS NetworkEvents;
				//::WSAEnumNetworkEvents(ListeningSocket, l_Events[1], &NetworkEvents);

				if (WSAEnumNetworkEvents(ListeningSocket ,l_Events[1],	&NetworkEvents) == SOCKET_ERROR)
				{
					debug("WSAEnumNetworkEvents failed with error {0:D}", WSAGetLastError());
  					return 1;
				}

				debug("NetworkEvents.lNetworkEvents :{0:D} , FD_ACCEPT : {1:D}",NetworkEvents.lNetworkEvents,FD_ACCEPT);

				if (NetworkEvents.lNetworkEvents & FD_ACCEPT ) //&& NetworkEvents.iErrorCode[FD_ACCEPT_BIT] == 0
				{
					//realy accept 
					debug("forward tcp : realy accept");

					if ( (ClientSocket_from = accept(ListeningSocket, (sockaddr*)&sin, &ss)) ==-1 )
					{
						debug("tcp forward thread: cannot accept connectin");
						return(1); // ?
					}
					debug("forward tcp : Connection accepted ");

					SOCKET ClientSocket_to = socket(AF_INET, SOCK_STREAM, 0);

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
					if (connect(ClientSocket_to, (sockaddr*)&sin, sizeof(sin)) != 0) {
						debug("Received a connection but can't connect to {0}:{1}", gcnew String(trgAddr), (gcnew Int32(trgPort))->ToString());
						shutdown(ClientSocket_from,SD_BOTH);
						closesocket(ClientSocket_from);
					} else {

						Tcp_param *p_Tcp_param = new Tcp_param;

						p_Tcp_param->m_socket_src = ClientSocket_from;
						p_Tcp_param->m_socket_dst = ClientSocket_to;


						DWORD id;
						rt = CreateThread(NULL, 0, reader, p_Tcp_param, 0, &id);
						p_Tcp_param->m_h_read = rt; //send handle to other thread so it can wait for it to finish
						wt = CreateThread(NULL, 0, writer, p_Tcp_param, 0, &id);

						PassPort::PortForwarder::oldThreads->Add((int)rt);
						PassPort::PortForwarder::oldThreads->Add((int)wt);

						debug("Established connection to: {0}:{1}", gcnew String(trgAddr), (gcnew Int32(trgPort))->ToString() );
					}

				}//realy accept
			}//not shutdown

		}//while


}//forward_tcp



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
 
	debug("Start PortForwarder udp thread");

    wVersionRequested = MAKEWORD( 2, 2 );
    WSAStartup( wVersionRequested, &wsaData );

    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);

	struct hostent *hent = NULL;	

	if ((hent = strlen(srcAddr) > 0 ? gethostbyname(srcAddr) : gethostbyname("127.0.0.1")) == NULL) {
		//log->WriteEntry(LOG_SOURCE, String::Format("Unknown host: {0}", gcnew String(srcAddr)), EventLogEntryType::Error);
		debug("End PortForwarder thread");
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
		//log->WriteEntry(LOG_SOURCE, String::Format("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString()), EventLogEntryType::Error);
		debug("Cannot bind to port {0}:{1}", gcnew String(srcAddr), (new Int32(srcPort))->ToString() );
        return 1;
    }

	debug("Binded to port {0}:{1} udp successful ", gcnew String(srcAddr), (new Int32(srcPort))->ToString()  );

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

	debug("End PortForwarder udp thread");
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
