/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:  Tom Henderson (tomhend@u.washington.edu)
 */
/*
 * RRAJ - Find the diffs between SocketRead and SocketAccept
 */
#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/node-container.h"

#include <climits>
#include "mdc-sink.h"
#include "mdc-header.h"
using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MdcSink");
NS_OBJECT_ENSURE_REGISTERED (MdcSink);

TypeId 
MdcSink::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MdcSink")
    .SetParent<Application> ()
    .AddConstructor<MdcSink> ()
    .AddAttribute ("MdcLocal", "The Address on which to Bind the rx socket for MDCs.",
                   AddressValue (),
                   MakeAddressAccessor (&MdcSink::m_mdcLocal),
                   MakeAddressChecker ())
    .AddAttribute ("SensorLocal", "The Address on which to Bind the rx socket for sensors.",
                   AddressValue (),
                   MakeAddressAccessor (&MdcSink::m_sensorLocal),
                   MakeAddressChecker ())
    .AddTraceSource ("Rx", "A packet has been received",
                     MakeTraceSourceAccessor (&MdcSink::m_rxTrace))
  ;
  return tid;
}

MdcSink::MdcSink ()
{
  NS_LOG_FUNCTION (this);
  m_sensorSocket = 0;
  m_mdcSocket = 0;
  m_totalRx = 0;
  m_waypointRouting = true;
}

MdcSink::~MdcSink()
{
  NS_LOG_FUNCTION (this);
}

uint32_t MdcSink::GetTotalRx () const
{
  return m_totalRx;
}

void MdcSink::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_sensorSocket = 0;
  m_mdcSocket = 0;
  m_acceptedSockets.clear ();
  m_mobilityModels.clear ();

  // chain up
  Application::DoDispose ();
}


// Application Methods
/*
 * Here we create the two sockets, the MDC socket and the Sensor socket.
 * Establish the callback functions on these sockets
 */
void MdcSink::StartApplication ()    // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);
  // Create the sockets if not already
  if (!m_mdcSocket)
    {
      m_mdcSocket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
      m_mdcSocket->Bind (m_mdcLocal);
      m_mdcSocket->Listen ();
    }

  if (!m_sensorSocket)
    {
      m_sensorSocket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
      m_sensorSocket->Bind (m_sensorLocal);
      m_sensorSocket->Listen ();
      m_sensorSocket->ShutdownSend (); // The Sensor socket is listen-only. The sink does not send anything to the sensor
    }

  m_mdcSocket->SetRecvCallback (MakeCallback (&MdcSink::HandleRead, this));
  m_mdcSocket->SetAcceptCallback (
    MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
    MakeCallback (&MdcSink::HandleAccept, this));
  m_mdcSocket->SetCloseCallbacks (
    MakeCallback (&MdcSink::HandlePeerClose, this),
    MakeCallback (&MdcSink::HandlePeerError, this));

  m_sensorSocket->SetRecvCallback (MakeCallback (&MdcSink::HandleRead, this));
}

/*
 * Clean up before stopping
 */
void MdcSink::StopApplication ()     // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  for (std::map<uint32_t, Ptr<Socket> >::iterator itr = m_acceptedSockets.begin ();
       itr != m_acceptedSockets.end (); itr++)
    {
      Ptr<Socket> acceptedSocket = (*itr).second;
      acceptedSocket->Close ();
      m_acceptedSockets.erase (itr);
    }
  if (m_mdcSocket) 
    {
      m_mdcSocket->Close ();
      m_mdcSocket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
  if (m_sensorSocket) 
    {
      m_sensorSocket->Close ();
      m_sensorSocket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    }
}


/*
 * This is a socket callback function that gets called.
 * The functionality changes depending upon whether the socket is a sensorSocket or a MdcSocket
 * If this is an MDC socket, we look for a previously held partial packet and process it accordingly.
 * Previously held packets are stored in a map called m_partialPacket and is indexed by the socket id.
 */
void MdcSink::HandleRead (Ptr<Socket> socket)
{
	NS_LOG_FUNCTION_NOARGS();
	if (socket == m_sensorSocket)
    {
    	return;
    }

	// We index all the packets from the MDCs so that the packet segments even if they arrive mixed up, we are able to handle it.
	Ptr<Packet> fullPacket = m_partialPacket[socket];
	Ptr<Packet> packet; // a temp variable holding the segment coming from the sender.
	Address from;
	// a boolean that simply triggers a logic to send traces or not.
	bool alreadyNotified;

	NS_LOG_LOGIC ("*SINK Node#" << GetNode ()->GetId () <<
		  " Partial Packet Size for this socket was=" << fullPacket->GetSize ());

	if (fullPacket->GetSize () > 0)
	{
		NS_LOG_LOGIC ("*SINK Node#" << GetNode ()->GetId ()
			  << " From a PriorRecv already has " << fullPacket->GetSize ()
			  << " B from this socket on Node. "  << socket->GetNode()->GetId());
	}


	// Prepare to format a new header here.
	MdcHeader head;
	head.SetData (0);  // for if condition for removing completed packets

	if (fullPacket->GetSize () >= MDC_HEADER_SIZE)
	// If you have a partial header in the buffer, this may give you seg faults
	{
		fullPacket->PeekHeader (head);
		alreadyNotified = true;
	}
	else
	{
		// Could not have forwarded to trace as there was no full header so far
		alreadyNotified = false;
	}


	// compute the packetsize of the forwarding packet
	uint32_t packetSize;
	packetSize = (head.GetData () ? head.GetData () + MDC_HEADER_SIZE : UINT_MAX);
	if (packetSize == UINT_MAX)
		NS_LOG_LOGIC ("*SINK Node#" << GetNode ()->GetId ()
		  << " PacketSize is unknown. First Segment expected."
		  );

	// Repeat this until the socket has no more data to send.
	while (packet = socket->RecvFrom (from)) // Repeat for each segment recd on the socket
    {
		NS_LOG_LOGIC ("Reading packet from socket.");

		if (InetSocketAddress::IsMatchingType (from))
        {
			fullPacket->AddAtEnd (packet);
			// Now your fullPacket should contain the partialPacket for the socket if any + this new segment added to it.
			NS_LOG_LOGIC("**3** FullPktSize=" << fullPacket->GetSize () << " ExpectedPktSize=" << packetSize << " CurrentPktSize=" << packet->GetSize());

			// Which means that there may be more segments to process
			Ipv4Address source = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
			fullPacket->PeekHeader (head);
			packetSize = (head.GetData () ? head.GetData () + MDC_HEADER_SIZE : UINT_MAX);

			if (!alreadyNotified)
			{
	            m_rxTrace (packet, from); // This writes a trace output that the first segment of a message was received and processed.
				alreadyNotified = true;
			}

			NS_LOG_LOGIC("**4** FullPktSize=" << fullPacket->GetSize () << " ExpectedPktSize=" << packetSize << " CurrentPktSize=" << packet->GetSize());
			if (fullPacket->GetSize () >= packetSize)
			{

				NS_LOG_LOGIC ("****SINK Node#" << GetNode ()->GetId ()
						<< " received a FULL packet TOTAL=" << fullPacket->GetSize()
						<< "B and FRAGMENT Size=" << packet->GetSize ()
						<< "B of [type="<< head.GetPacketType ()
						<< "] from [" << source << "] destined for [" << head.GetDest ()
						<< "]");
				// The fullPacket has a complete packet now.


				m_rxTrace (fullPacket, from); // This writes a trace output that the first segment of a message was received and processed.
				//    but there may be some more from the next pkt
				// extract just the portion of the packet that and leave the rest in the fullPacket... that was one idea.

				fullPacket->RemoveAtStart(packetSize);
				alreadyNotified = false; // So that a next packet from this socket gets traced appropriately.
				if (fullPacket->GetSize() >= MDC_HEADER_SIZE )
				{
					fullPacket->PeekHeader (head); // Reset the packetSize
					packetSize = (head.GetData () ? head.GetData () + MDC_HEADER_SIZE : UINT_MAX);
				}
				else
					packetSize = 0;

				NS_LOG_LOGIC("**5** FullPktSize=" << fullPacket->GetSize () << " ExpectedPktSize=" << packetSize << " CurrentPktSize=" << packet->GetSize());
				if (fullPacket->GetSize () > packetSize)
				{
					NS_LOG_LOGIC ("****SINK Node#" << GetNode ()->GetId ()
						<< " Segment recd from " << source
						<< " contained info beyond the boundary of one packet. Ignoring the rest... FULLPACKETSIZE=" << fullPacket->GetSize()
						<< " B and ExpectedPACKETSIZE=" << packetSize << "B. Cleaning up the partialPacket buffer.");
				}
			}

			// Now that the segment is ready, just forward it.
			// Note that the packet contains a part of the next segment but we will forward it anyway.
			// IF YOU NEED TO FORWARD THE PACKET TO ANOTHER NODE/NETWORK, This is the place to do it.
			// ForwardPacket (packet);

			NS_LOG_LOGIC("**6** FullPktSize=" << fullPacket->GetSize () << " ExpectedPktSize=" << packetSize << " CurrentPktSize=" << packet->GetSize());
			// A segment has been forwarded... Now it looks like there are more segments
			if (fullPacket->GetSize () > packetSize)
				NS_LOG_LOGIC ("*SINK Node#" << GetNode ()->GetId ()
					  << "Expecting more data... PacketBufferSize=" << m_partialPacket[socket]->GetSize()
					  << "  CompletePacketSize=" << packetSize
					  << "."
					  );

		}// end of if
    } // end of while socket has no more data
}

/*
{
  NS_LOG_FUNCTION (this << socket);
  // fullPacket is set to blank if it is a sensor socket as there is no likelihood of a partial packet ever
  // recd from sensor.
  // By our def. the sensor packet is always small enough to fit into a TCP segment.
  Ptr<Packet> fullPacket = (socket == m_sensorSocket ? Create<Packet> () : m_partialPacket[socket]);
  Ptr<Packet> packet;
  Address from;

  // If we already had a full header, we already fired traces about it
  // We fire a trace of a packet recd when the first segment of the packet arrives.
  bool alreadyNotified = fullPacket->GetSize () >= MDC_HEADER_SIZE;

  MdcHeader head;
  head.SetData (0);  // for if condition for removing completed packets

  if (alreadyNotified)
    {
      fullPacket->PeekHeader (head);
    }
  
  uint32_t packetSize;
  packetSize = (head.GetData () ? head.GetData () + MDC_HEADER_SIZE : UINT_MAX);
  
  while ((packet = socket->RecvFrom (from))) // Read a TCP segment from the socket
    {
      fullPacket->AddAtEnd (packet);

      if (packet->GetSize () == 0) // This was just an empty segment somehow. So ignore it.
        { //EOF
          break;
        }
          
      while (fullPacket->GetSize () >= MDC_HEADER_SIZE)
        {
          Ipv4Address source = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
            

          if (!alreadyNotified) // Start of a new segment
            //if (m_expectedBytes[socket] == 0)
            {
              fullPacket->PeekHeader (head);
              //m_expectedBytes[socket] = head.GetData () + head.GetSerializedSize () - packet->GetSize ();
              alreadyNotified = true;
              m_rxTrace (packet, from); // This writes a trace output that the first segment of a message was received and processed.

              if (InetSocketAddress::IsMatchingType (from))
                {
                  NS_LOG_INFO ("SINK-Time " << Simulator::Now ().GetSeconds ()
                               << "s Received "
                               <<  packet->GetSize () << "B from "
                               << InetSocketAddress::ConvertFrom(from).GetIpv4 ()
                               << " port " << InetSocketAddress::ConvertFrom (from).GetPort ()
                               << " total Rx " << fullPacket->GetSize() << "B");
                }
              else if (Inet6SocketAddress::IsMatchingType (from))
                {
                  NS_LOG_INFO ("SINK-Time " << Simulator::Now ().GetSeconds ()
                               << "s Received "
                               <<  packet->GetSize () << "B from "
                               << Inet6SocketAddress::ConvertFrom(from).GetIpv6 ()
                               << " port " << Inet6SocketAddress::ConvertFrom (from).GetPort ()
                               << " total Rx " << fullPacket->GetSize()  << "B");
                }
            }
            
          // Remove completed transfers
          if (fullPacket->GetSize () >= packetSize)            
            {
              fullPacket->RemoveAtStart (packetSize);
              alreadyNotified = false;

              if 	( (head.GetFlags () == MdcHeader::sensorFullData) ||
            		  (head.GetFlags () == MdcHeader::sensorDataReply))
                m_totalRx += packetSize;
      
              NS_LOG_INFO ("SINK " << GetNode ()->GetId () << " completed forwarding packet from " << source );
            }
          else
            break;
        }
    }
}
*/

/*
 * This is a normal socket close callback function registered on the socket
 */
void MdcSink::HandlePeerClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 
/*
 * This is a socket close callback function registered when there is an error
 */
void MdcSink::HandlePeerError (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}
 
/*
 * The socket callback function registered and invoked when a connection from the 'from' ip address is received.
 * A new secket is created and the HandleRead callback function is attached to it.
 */
void MdcSink::HandleAccept (Ptr<Socket> s, const Address& from)
{
  NS_LOG_FUNCTION (this->GetTypeId() << s << from << Seconds (Simulator::Now ()));
  s->SetRecvCallback (MakeCallback (&MdcSink::HandleRead, this));

  // Get the mobility model associated with the MDC so that we know all their locations
  NodeContainer allNodes = NodeContainer::GetGlobal ();
  Ptr<Node> destNode;
  Ipv4Address addr = InetSocketAddress::ConvertFrom (from).GetIpv4 ();

  for (NodeContainer::Iterator i = allNodes.Begin (); i != allNodes.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      if (ipv4->GetInterfaceForAddress (addr) != -1)
        {
          destNode = node;
          break;
        }
    }

  if (!destNode)
    {
      NS_LOG_ERROR ("Couldn't find dest node given the IP" << addr);
      return;
    }
  
  uint32_t id = destNode->GetId ();
  m_acceptedSockets[id] = s;
  //m_expectedBytes[s] = 0;
  m_partialPacket[s] = Create<Packet> ();

  Ptr<WaypointMobilityModel> mobility = DynamicCast <WaypointMobilityModel> (destNode->GetObject <MobilityModel> ());
  if (mobility)
    {
      m_mobilityModels[id] = mobility;
    }
  else
    {
      m_waypointRouting = false;
    }
}

} // Namespace ns3
