#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/error-model.h"

#include <iostream>
#include <fstream>
#include <sys/stat.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("CaseStudy1_VideoStreamingVsConference");

int main (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  Time::SetResolution (Time::NS);

  // Tạo các Node
  NodeContainer clients, routers, servers;
  clients.Create (2);
  routers.Create (1);
  servers.Create (1);

  // Cấu hình link Client -> Router
  PointToPointHelper clientToRouter;
  clientToRouter.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  clientToRouter.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer d0r = clientToRouter.Install (clients.Get (0), routers.Get (0));
  NetDeviceContainer d1r = clientToRouter.Install (clients.Get (1), routers.Get (0));

  // Cấu hình bottleneck Router -> Server với tỷ lệ mất gói 2% (0.02)
  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  bottleneckLink.SetChannelAttribute ("Delay", StringValue ("20ms"));

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (0.02));
  em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));

  NetDeviceContainer drs = bottleneckLink.Install (routers.Get (0), servers.Get (0));
  drs.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  // Cài đặt Internet Stack
  InternetStackHelper stack;
  stack.Install (clients);
  stack.Install (routers);
  stack.Install (servers);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if0r = address.Assign (d0r);

  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if1r = address.Assign (d1r);

  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer ifrs = address.Assign (drs);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // Dịch vụ 1: Video Conference (UDP - Port 5000)
  uint16_t udpPort = 5000;
  UdpServerHelper udpServer (udpPort);
  ApplicationContainer serverAppUdp = udpServer.Install (servers.Get (0));
  serverAppUdp.Start (Seconds (1.0));
  serverAppUdp.Stop (Seconds (10.0));

  UdpClientHelper udpClient (ifrs.GetAddress (1), udpPort);
  udpClient.SetAttribute ("MaxPackets", UintegerValue (100000));
  udpClient.SetAttribute ("Interval", TimeValue (MilliSeconds (20)));
  udpClient.SetAttribute ("PacketSize", UintegerValue (1000));
  ApplicationContainer clientAppUdp = udpClient.Install (clients.Get (0));
  clientAppUdp.Start (Seconds (2.0));
  clientAppUdp.Stop (Seconds (10.0));

  // Dịch vụ 2: On-Demand Streaming (TCP - Port 8080)
  uint16_t tcpPort = 8080;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), tcpPort));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  ApplicationContainer serverAppTcp = sinkHelper.Install (servers.Get (0));
  serverAppTcp.Start (Seconds (1.0));
  serverAppTcp.Stop (Seconds (10.0));

  OnOffHelper onoff ("ns3::TcpSocketFactory", InetSocketAddress (ifrs.GetAddress (1), tcpPort));
  onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  onoff.SetAttribute ("DataRate", StringValue ("5Mbps"));
  onoff.SetAttribute ("PacketSize", UintegerValue (1400));
  ApplicationContainer clientAppTcp = onoff.Install (clients.Get (1));
  clientAppTcp.Start (Seconds (2.0));
  clientAppTcp.Stop (Seconds (10.0));

  // Xuất file NetAnim XML (case1.xml)
  AnimationInterface anim ("case1.xml");
  anim.SetConstantPosition (clients.Get (0), 10.0, 20.0);
  anim.SetConstantPosition (clients.Get (1), 10.0, 40.0);
  anim.SetConstantPosition (routers.Get (0), 40.0, 30.0);
  anim.SetConstantPosition (servers.Get (0), 70.0, 30.0);

  // Thu thập dữ liệu FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (10.5));
  Simulator::Run ();

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  monitor->SerializeToXmlFile ("case1_flowmon.xml", true, true);

  mkdir ("results", 0777);
  std::ofstream csvFile ("results/case1.csv");
  csvFile << "FlowId,Service,Protocol,Source,Destination,TxPackets,RxPackets,LostPackets,LossRate(%),Throughput(Kbps),MeanDelay(ms),MeanJitter(ms)\n";

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      if (t.destinationPort == udpPort || t.destinationPort == tcpPort)
        {
          std::string serviceName = (t.destinationPort == udpPort) ? "Video Conference" : "On-Demand Streaming";
          std::string proto = (t.destinationPort == udpPort) ? "UDP" : "TCP";

          double lossRate = 0.0;
          if (i->second.txPackets > 0)
            {
              lossRate = ((double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets) * 100.0;
            }

          double throughput = (i->second.rxBytes * 8.0) / (8.0 * 1000.0);
          double meanDelay = (i->second.rxPackets > 0) ? (i->second.delaySum.GetSeconds () / i->second.rxPackets) * 1000.0 : 0;
          double meanJitter = (i->second.rxPackets > 0) ? (i->second.jitterSum.GetSeconds () / i->second.rxPackets) * 1000.0 : 0;

          csvFile << i->first << ","
                  << serviceName << ","
                  << proto << ","
                  << t.sourceAddress << ","
                  << t.destinationAddress << ","
                  << i->second.txPackets << ","
                  << i->second.rxPackets << ","
                  << (i->second.txPackets - i->second.rxPackets) << ","
                  << lossRate << ","
                  << throughput << ","
                  << meanDelay << ","
                  << meanJitter << "\n";
        }
    }

  csvFile.close ();
  Simulator::Destroy ();
  return 0;
}
