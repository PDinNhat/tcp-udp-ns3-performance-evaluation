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

NS_LOG_COMPONENT_DEFINE ("CaseStudy2_LongFatNetworks");

int main (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  Time::SetResolution (Time::NS);

  // Cấu hình TCP NewReno (đại diện cho TCP truyền thống nhạy cảm với packet loss)
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpNewReno"));
  // Mở rộng bộ đệm TCP socket để thử nghiệm LFN
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (131072000)); // 128 MB
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (131072000));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1440));

  // Tạo các Node:
  // Node 0: Trạm phát trắc địa/vệ tinh (Sender)
  // Node 1: Vệ tinh chuyển tiếp / Gateway
  // Node 2: Trạm mặt đất tiếp nhận (Receiver)
  NodeContainer nodes;
  nodes.Create (3);

  // Kênh truyền vệ tinh LFN: 1 Gbps, Delay 1 chiều 300ms (RTT = 600ms)
  PointToPointHelper satelliteLink;
  satelliteLink.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  satelliteLink.SetChannelAttribute ("Delay", StringValue ("300ms"));

  NetDeviceContainer d01 = satelliteLink.Install (nodes.Get (0), nodes.Get (1));
  NetDeviceContainer d12 = satelliteLink.Install (nodes.Get (1), nodes.Get (2));

  // Gây nhiễu ngẫu nhiên (Random Bit/Packet Error do thời tiết/vệ tinh, không phải tắc nghẽn)
  // Tỷ lệ lỗi 0.5% (0.005)
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (0.005));
  em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
  d12.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  // Cài đặt Internet Stack
  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.2.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if01 = address.Assign (d01);

  address.SetBase ("10.2.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if12 = address.Assign (d12);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // ================= Dịch vụ 1: FTP truyền thống trên nền TCP =================
  uint16_t tcpPort = 9001;
  Address sinkLocalAddressTcp (InetSocketAddress (Ipv4Address::GetAny (), tcpPort));
  PacketSinkHelper sinkHelperTcp ("ns3::TcpSocketFactory", sinkLocalAddressTcp);
  ApplicationContainer serverAppTcp = sinkHelperTcp.Install (nodes.Get (2));
  serverAppTcp.Start (Seconds (1.0));
  serverAppTcp.Stop (Seconds (15.0));

  // BulkSendApplication bơm file dung lượng lớn qua TCP
  BulkSendHelper sourceTcp ("ns3::TcpSocketFactory", InetSocketAddress (if12.GetAddress (1), tcpPort));
  sourceTcp.SetAttribute ("MaxBytes", UintegerValue (0)); // Không giới hạn (bơm liên tục)
  sourceTcp.SetAttribute ("SendSize", UintegerValue (1440));
  ApplicationContainer clientAppTcp = sourceTcp.Install (nodes.Get (0));
  clientAppTcp.Start (Seconds (2.0));
  clientAppTcp.Stop (Seconds (15.0));

  // ================= Dịch vụ 2: LFN-Optimized Transfer (Tương tự Aspera/UDT trên UDP) =================
  uint16_t udpPort = 9002;
  PacketSinkHelper sinkHelperUdp ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), udpPort));
  ApplicationContainer serverAppUdp = sinkHelperUdp.Install (nodes.Get (2));
  serverAppUdp.Start (Seconds (1.0));
  serverAppUdp.Stop (Seconds (15.0));

  // Bơm luồng UDP với tốc độ cao dựa trên băng thông vật lý ước lượng (~600Mbps)
  OnOffHelper sourceUdp ("ns3::UdpSocketFactory", InetSocketAddress (if12.GetAddress (1), udpPort));
  sourceUdp.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  sourceUdp.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
  sourceUdp.SetAttribute ("DataRate", StringValue ("600Mbps"));
  sourceUdp.SetAttribute ("PacketSize", UintegerValue (1440));
  ApplicationContainer clientAppUdp = sourceUdp.Install (nodes.Get (0));
  clientAppUdp.Start (Seconds (2.0));
  clientAppUdp.Stop (Seconds (15.0));

  // ================= NetAnim =================
  AnimationInterface anim ("case2.xml");
  anim.SetConstantPosition (nodes.Get (0), 10.0, 30.0);
  anim.SetConstantPosition (nodes.Get (1), 45.0, 30.0);
  anim.SetConstantPosition (nodes.Get (2), 80.0, 30.0);

  // ================= FlowMonitor =================
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (15.5));
  Simulator::Run ();

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  monitor->SerializeToXmlFile ("case2_flowmon.xml", true, true);

  mkdir ("results", 0777);
  std::ofstream csvFile ("results/case2.csv");
  csvFile << "FlowId,Service,Protocol,Source,Destination,TxPackets,RxPackets,LostPackets,LossRate(%),Throughput(Mbps),MeanDelay(ms),MeanJitter(ms)\n";

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      if (t.destinationPort == tcpPort || t.destinationPort == udpPort)
        {
          std::string serviceName = (t.destinationPort == tcpPort) ? "Traditional FTP" : "LFN Optimized Transfer";
          std::string proto = (t.destinationPort == tcpPort) ? "TCP (NewReno)" : "UDP (Aspera/UDT Model)";

          double lossRate = 0.0;
          if (i->second.txPackets > 0)
            {
              lossRate = ((double)(i->second.txPackets - i->second.rxPackets) / i->second.txPackets) * 100.0;
            }

          // Thời gian truyền thực tế: 15s - 2s = 13s
          double throughputMbps = (i->second.rxBytes * 8.0) / (13.0 * 1000000.0);
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
