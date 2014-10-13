/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 The Boeing Company
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
 */

// This example shows how to use the Kodo library in a broadcast scenario
// within a ns-3 simulation. The code below is based on the wifi_broadcast
// example, which can be found in this repository.

// In the script below the sender transmits encoded packets from a block of
// data to two receivers with the same packet erasure rate. The sender
// continues until all receivers have received all packets. Here the packets
// are sent using the binary field, GF(2) with a generation of 5 packets and
// 1000 (application) bytes to the other node.

// You can change any parameter, by running (for example with a different
// generation size):
// ./build/linux/wired_broadcast/wired_broadcast --generationSize=GENERATION_SIZE

// When you are done, you will notice four pcap trace files in your directory.
// You can review the files with Wireshark or tcpdump. If you have tcpdump
// installed, you can try (for example) this:
//
// tcpdump -r star-0-0.pcap -nn -tt

#include <ns3/point-to-point-star.h>
#include <ns3/internet-module.h>
#include <ns3/config-store-module.h>
#include <ns3/core-module.h>
#include <ns3/network-module.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <ctime>

#include <kodo/rlnc/full_rlnc_codes.hpp>
#include <kodo/trace.hpp>

#include "KodoSimulation.hpp"

using namespace ns3;

int main (int argc, char *argv[])
{
  uint32_t packetSize = 1000; // Application bytes per packet
  double interval = 1.0; // Time between events
  uint32_t generationSize = 5; // RLNC generation size
  double errorRate = 0.3; // Error rate for all the links

  Time interPacketInterval = Seconds (interval);

  CommandLine cmd;

  cmd.AddValue ("packetSize", "Size of application packet sent", packetSize);
  cmd.AddValue ("interval", "Interval (seconds) between packets", interval);
  cmd.AddValue ("generationSize", "Set the generation size to use",
                generationSize);
  cmd.AddValue ("errorRate", "Packet erasure rate for the links", errorRate);

  cmd.Parse (argc, argv);

  Time::SetResolution (Time::NS);

  // Set the basic helper for a single link
  PointToPointHelper pointToPoint;

  // Two receivers against a centralized hub. Note: DO NOT CHANGE THIS LINE
  PointToPointStarHelper star (2, pointToPoint);

  // Set error model for the net devices
  Config::SetDefault ("ns3::RateErrorModel::ErrorUnit",
                      StringValue ("ERROR_UNIT_PACKET"));

  Ptr<RateErrorModel> errorModel1 = CreateObject<RateErrorModel> ();
  errorModel1->SetAttribute ("ErrorRate", DoubleValue (errorRate));

  Ptr<RateErrorModel> errorModel2 = CreateObject<RateErrorModel> ();
  errorModel2->SetAttribute ("ErrorRate", DoubleValue (errorRate));

  star.GetSpokeNode (0)->GetDevice (0)->
    SetAttribute ("ReceiveErrorModel", PointerValue (errorModel1));
  star.GetSpokeNode (1)->GetDevice (0)->
    SetAttribute ("ReceiveErrorModel", PointerValue (errorModel2));
  errorModel1->Enable ();
  errorModel2->Enable ();

  // Setting IP protocol stack
  InternetStackHelper internet;
  star.InstallStack(internet);

  // Set IP addresses
  star.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"));

  // The encoder / decoder type we will use. Here we consider GF(2). For GF(2^8)
  // just change "binary" for "binary8"
  typedef kodo::full_rlnc_encoder<fifi::binary8,
                                  kodo::disable_trace> rlnc_encoder;
  typedef kodo::full_rlnc_decoder<fifi::binary8,
                                  kodo::disable_trace> rlnc_decoder;

  // Creation of RLNC encoder and decoder objects
  rlnc_encoder::factory encoder_factory(generationSize, packetSize);
  rlnc_decoder::factory decoder_factory(generationSize, packetSize);

  // The member build function creates differents instances of each object
  KodoSimulation kodoSimulator(encoder_factory.build(),
                               decoder_factory.build(),
                               decoder_factory.build());

  // Setting up application sockets for receivers and senders
  uint16_t port = 80;
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), port);

  // Receivers
  Ptr<Socket> recvSink1 = Socket::CreateSocket (star.GetSpokeNode (0), tid);
  recvSink1->Bind (local);
  recvSink1->SetRecvCallback (MakeCallback (&KodoSimulation::ReceivePacket1,
                                            &kodoSimulator));

  Ptr<Socket> recvSink2 = Socket::CreateSocket (star.GetSpokeNode (1), tid);
  recvSink2->Bind (local);
  recvSink2->SetRecvCallback (MakeCallback (&KodoSimulation::ReceivePacket2,
                                            &kodoSimulator));

  // Sender
  Ptr<Socket> source = Socket::CreateSocket (star.GetHub (), tid);
  InetSocketAddress remote = InetSocketAddress (Ipv4Address ("255.255.255.255"),
                                                port);
  source->SetAllowBroadcast (true);
  source->Connect (remote);

  // Turn on global static routing so we can actually be routed across the star
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Do pcap tracing on all point-to-point devices on all nodes
  pointToPoint.EnablePcapAll ("star");

  Simulator::ScheduleWithContext (source->GetNode ()->GetId (), Seconds (1.0),
                                  &KodoSimulation::GenerateTraffic,
                                  &kodoSimulator, source, interPacketInterval);
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
