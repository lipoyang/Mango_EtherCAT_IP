#include <stdio.h>
#include <string.h>

#include "mbed.h"
#include "EthernetInterface.h"

// グローバル変数
Mutex g_mutex;
uint8_t  g_servo[4];    // サーボ目標角度     (MANGO→PC)
uint16_t g_volume[4];   // ボリューム入力値   (MANGO→PC)
int16_t  g_gamepad[4];  // ゲームパッド入力値 (MANGO←PC)
bool g_remoteOn = false; // 遠隔操作ONフラグ
int g_init_cnt = 0;      // 初期動作カウンタ
bool g_init_flag = true; // 初期動作フラグ

// UDP/IP用のネットワークインターフェース
EthernetInterface eth;

// UDP送信スレッド
Thread udpSendThread;
// UDP受信スレッド
Thread udpRecvThread;

// 自ホスト(マイコン)のIPアドレスとポート番号
static const char *IPadrees    = "192.168.10.2";
static const char *NetworkMask = "255.255.255.0";
static const char *Gateway     = "192.168.10.1";
static const uint16_t LocalPort = 4002;

// 相手ホスト(PC)のIPアドレスとポート番号
static const char *RemoteAddress = "192.168.10.1";
static const uint16_t RemotePort = 4001;

// 生存応答コマンド送信リクエストフラグ
static bool reqAliveCommand = false;

// UDP通信用NIC初期化
void udpNicInit()
{
    // ネットワークインターフェース接続
    printf("Initializing NIC for UDP\n");
    eth.set_dhcp(false);
    eth.set_network(IPadrees, NetworkMask, Gateway);
    if(0 != eth.connect()) {
        printf("Error: NIC connecting failed\n");
        while(true);
    }
    printf("NIC connected\n");
    // IPアドレス確認
    const char *ip = eth.get_ip_address();
    printf("IP address is: %s\n", ip ? ip : "No IP");
}

// UDP通信用NIC終了処理
void udpNicClose()
{
    eth.disconnect();
    printf("NIC disconnected\n");
}

// UDP送信スレッド関数
void udpSendFunc()
{
    // 送信用UDPソケット
    UDPSocket sendSock;
    sendSock.open(&eth);

    // メインループ
    while(true){
        // 送信
        static int cnt = 0;
        cnt++;
        if(cnt>=10){
            cnt = 0;
            // Dコマンド送信
            uint8_t send_buf[15];
            send_buf[0] = '#';
            send_buf[1] = 'D';
            g_mutex.lock();
            for(int i=0;i<4;i++){
                send_buf[2 + i*2] = (uint8_t)(g_volume[i] >> 8);
                send_buf[3 + i*2] = (uint8_t)(g_volume[i] & 0xFF);
                send_buf[10 + i] = g_servo[i];
            }
            g_mutex.unlock();
            send_buf[14] = '$';
            //printf("send D\n");
            int sentSize = sendSock.sendto(RemoteAddress, RemotePort, send_buf, sizeof(send_buf));
            if(sentSize < 0) {
                printf("Error sending D Command\n");
            }
        }
        if(reqAliveCommand){
            reqAliveCommand = false;
            // Aコマンド送信
            uint8_t send_buf[3];
            send_buf[0] = '#';
            send_buf[1] = 'A';
            send_buf[2] = '$';
            printf("send A\n");
            int sentSize = sendSock.sendto(RemoteAddress, RemotePort, send_buf, sizeof(send_buf));
            if(sentSize < 0) {
                printf("Error sending A Command\n");
            }
        }
        wait_ms(10);
    }
    // ソケットを閉じる
    sendSock.close();
}

// UDP受信スレッド関数
void udpRecvFunc()
{
    // 受信用UDPソケット
    UDPSocket recvSock;
    recvSock.open(&eth);
    int bind = recvSock.bind(LocalPort);
    printf("UDP receive socket bind return: %d\n", bind);

    while(true)
    {
        // 受信
        uint8_t recv_buf[256];
        SocketAddress remoteHost; // 相手ホストの情報取得用
        int n = recvSock.recvfrom(&remoteHost, &recv_buf, sizeof(recv_buf));
        // printf("PC Address: %s Port: %d\n\r", remoteHost.get_ip_address(), remoteHost.get_port());
        // 受信があったら
        if(n > 0)
        {
            //printf("received %d byte\n", n);
            if(recv_buf[0] == '#')
            {
                switch(recv_buf[1]){
                    case 'A':
                        if(n < 3){
                            printf("A: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[2] == '$'){
                            reqAliveCommand = true;
                            printf("A: Received!\n");
                        }else{
                            printf("A: Bad Tail Code %02X\n", recv_buf[2]);
                        }
                        break;
                    case 'R':
                        if(n < 4){
                            printf("R: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[3] == '$'){
                            uint8_t remoteOn = recv_buf[2];
                            g_mutex.lock();
                            if(remoteOn == 0){
                                g_remoteOn = false;
                                g_init_cnt = 0;
                                g_init_flag = true;
                                //printf("R: OFF!\n");
                            }else if(remoteOn == 1)
                            {
                                g_remoteOn = true;
                                //g_init_cnt = 0;
                                //g_init_flag = true;
                                //printf("R: ON!\n");
                            }else{
                                printf("R: Bad Parameter %02X\n", remoteOn);
                            }
                            g_mutex.unlock();
                        }else{
                            printf("R: Bad Tail Code %02X\n", recv_buf[3]);
                        }
                        break;
                    case 'C':
                        if(n < 11){
                            printf("C: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[10] == '$'){
                            g_mutex.lock();
                            for(int i=0;i<4;i++){
                                g_gamepad[i] = (int16_t)(
                                    ((uint16_t)recv_buf[2 + 2*i] << 8) | (uint16_t)recv_buf[3 + 2*i]
                                );
                            }
                            printf("C: %4d, %4d, %4d, %4d\n", g_gamepad[0], g_gamepad[1], g_gamepad[2], g_gamepad[3]);
                            g_mutex.unlock();
                        }else{
                            printf("C: Bad Tail Code %02X\n", recv_buf[10]);
                        }
                        break;
                    default:
                        printf("Unknown Command %02X\n", recv_buf[1]);
                        break;
                }
            }else{
                printf("Bad Head Code %02X", recv_buf[0]);
            }
        }
    }
    
    // ソケットを閉じる
    recvSock.close();
}
