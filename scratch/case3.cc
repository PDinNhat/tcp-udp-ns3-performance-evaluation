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

NS_LOG_COMPONENT_DEFINE ("CaseStudy3_HTTP2_vs_HTTP3_QUIC");

int main (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  Time::SetResolution (Time::NS);

  // Cấu hình cơ chế TCP Cubic tiêu chuẩn cho HTTP/2
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpCubic"));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1400));

  // Tạo các Node:
  // Node 0: Web Client (Browser)
  // Node 1: Wi-Fi / 4G Base Station & Router
  // Node 2: Web Server (Hỗ trợ HTTP/2 và HTTP/3)
  NodeContainer nodes;
  nodes.Create (3);

  // Kênh truyền Client -> Router (Last-mile Access)
  PointToPointHelper accessLink;
  accessLink.SetDeviceAttribute ("DataRate", StringValue ("50Mbps"));
  accessLink.SetChannelAttribute ("Delay", StringValue ("15ms"));

  NetDeviceContainer d01 = accessLink.Install (nodes.Get (0), nodes.Get (1));

  // Kênh truyền Router -> Server (Internet Backbone / Bottleneck)
  PointToPointHelper backboneLink;
  backboneLink.SetDeviceAttribute ("DataRate", StringValue ("20Mbps"));
  backboneLink.SetChannelAttribute ("Delay", StringValue ("35ms"));

  // Thiết lập tỷ lệ rớt gói 1.5% (mô phỏng mạng di động suy hao gây nghẽn HOL)
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (0.015));
  em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));

  NetDeviceContainer d12 = backboneLink.Install (nodes.Get (1), nodes.Get (2));
  d12.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  // Cài đặt Internet Stack
  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.3.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if01 = address.Assign (d01);

  address.SetBase ("10.3.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if12 = address.Assign (d12);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // ================= 1. Mô hình HTTP/2 over TCP (Single Connection Multiplexing) =================
  // HTTP/2 dồn tất cả dữ liệu (HTML, CSS, Script, Images) vào 1 luồng TCP duy nhất
  uint16_t tcpPort = 8443;
  PacketSinkHelper sinkTcp ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), tcpPort));
  ApplicationContainer serverAppTcp = sinkTcp.Install (nodes.Get (2));
  serverAppTcp.Start (Seconds (0.5));
  serverAppTcp.Stop (Seconds (12.0));

  OnOffHelper clientTcp ("ns3::TcpSocketFactory", InetSocketAddress (if12.GetAddress (1), tcpPort));
  clientTcp.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientTcp.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientTcp.SetAttribute ("DataRate", StringValue ("12Mbps"));
  clientTcp.SetAttribute ("PacketSize", UintegerValue (1400));
  ApplicationContainer clientAppTcp = clientTcp.Install (nodes.Get (0));
  // TCP cần thêm thời gian 3-way Handshake + TLS Handshake (trễ khởi tạo)
  clientAppTcp.Start (Seconds (1.2));
  clientAppTcp.Stop (Seconds (12.0));

  // ================= 2. Mô hình HTTP/3 over QUIC/UDP (Independent Streams & 0-RTT/1-RTT) =================
  // HTTP/3 chạy trên QUIC/UDP, kết nối thiết lập nhanh hơn và các luồng độc lập không bị chặn chéo
  uint16_t udpPort = 4433;
  PacketSinkHelper sinkUdp ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), udpPort));
  ApplicationContainer serverAppUdp = sinkUdp.Install (nodes.Get (2));
  serverAppUdp.Start (Seconds (0.5));
  serverAppUdp.Stop (Seconds (12.0));

  OnOffHelper clientUdp ("ns3::UdpSocketFactory", InetSocketAddress (if12.GetAddress (1), udpPort));
  clientUdp.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  clientUdp.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  clientUdp.SetAttribute ("DataRate", StringValue ("12Mbps"));
  clientUdp.SetAttribute ("PacketSize", UintegerValue (1400));
  ApplicationContainer clientAppUdp = clientUdp.Install (nodes.Get (0));
  // QUIC tích hợp bắt tay TLS 1.3 nhanh chóng, bắt đầu truyền dữ liệu sớm hơn
  clientAppUdp.Start (Seconds (0.7));
  clientAppUdp.Stop (Seconds (12.0));

  // Xuất file NetAnim XML (case3.xml)
  AnimationInterface anim ("case3.xml");
  anim.SetConstantPosition (nodes.Get (0), 10.0, 30.0);
  anim.SetConstantPosition (nodes.Get (1), 45.0, 30.0);
  anim.SetConstantPosition (nodes.Get (2), 80.0, 30.0);

  // Thu thập dữ liệu FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (12.5));
  Simulator::Run ();

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  monitor->SerializeToXmlFile ("case3_flowmon.xml", true, true);

  mkdir ("results", 0777);
  std::ofstream csvFile ("results/case3.csv");
  csvFile << "FlowId,Service,Protocol,Source,Destination,TxPackets,RxPackets,LostPackets,LossRate(%),Throughput(Mbps),MeanDelay(ms),MeanJitter(ms)\n";

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      if (t.destinationPort == tcpPort || t.destinationPort == udpPort)
        {
          std::string serviceName = (t.destinationPort == tcpPort) ? "HTTP/2 (over TCP)" : "HTTP/3 (over QUIC/UDP)";
          std::string proto = (t.destinationPort == tcpPort) ? "TCP" : "UDP (QUIC)";

          double lossRate = 0.0;
          if (i->second.txPackets > 0)
            {
              lossRate = ((double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets) * 100.0;
            }

          // Thời gian truyền tải đo đạc
          double duration = 10.8;
          double throughputMbps = (i->second.rxBytes * 8.0) / (duration * 1000000.0);
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
                  << throughputMbps << ","
                  << meanDelay << ","
                  << meanJitter << "\n";
        }
    }

  csvFile.close ();
  Simulator::Destroy ();
  return 0;
}
