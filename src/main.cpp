#include "mbed.h"

// デバッグ出力用
Serial pc(USBTX, USBRX);
// UDP受信スレッド
extern Thread udpRecvThread;
// UDP送信スレッド
extern Thread udpSendThread;
// UDP通信用NIC初期化
void udpNicInit();
// UDP受信スレッド関数
void udpRecvFunc();
// UDP送信スレッド関数
void udpSendFunc();
// ロボットアーム制御
void robot_arm_ctrl(void);

// メイン関数
int main()
{
    pc.baud(115200);
    pc.printf("EtherCAT-IP Gateway\n");
    
    // UDP通信用NIC初期化
    udpNicInit();
    // 受信スレッド開始
    udpRecvThread.start(udpRecvFunc);
    // 送信スレッド開始
    udpSendThread.start(udpSendFunc);
    
    // ロボットアーム制御
    robot_arm_ctrl();
}
