# GR-MANGO(ベータ版)でEtherCATマスター/IPv4ブリッジ

GR-MANGO(ベータ版)の2個のLANポートを活用して、片方でEtherCATマスター、もう片方でUDP/IP通信をするデモ。

<!-- 動画 -->
<!-- 全体構成図 -->

GR-MANGO(RZ/A2Mマイコン)のEthernetポートのCH0でUDP/IP、CH1でEtherCATマスターの通信をおこないます。Ethernetドライバ(r_ether_rza2.c)とMbedのRZ_A2_EMACクラス(rza2_emac.cpp)のコールバックに関する処理の都合で、Ethernetドライバを少しハックしました。

## EtherCATによるロボットアーム制御
このプロジェクトは下記のプロジェクトをベースにしています。EtherCATに関してはこちらを参照してください。

- [ArduinoでEtherCATのデモ](https://github.com/lipoyang/Arduino_de_EtherCAT)

## ファイル構成
- `WinMangoCAT/` : Windows用モニタ/コントロールアプリ
- `src/` : GR-MANGOソフトウェアのソース
	- `main.cpp` : メイン関数
	- `ethercat.cpp` : EtherCAT通信処理とロボットアーム制御
	- `udpip.cpp` : UDP/IP通信処理
	- `SOEM/` : SOEM(EtherCATマスター)のGR-MANGO移植版
	- `mbed-os/` : Ethernetドライバのハック版
